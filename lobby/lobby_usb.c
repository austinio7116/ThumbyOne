/*
 * ThumbyOne lobby — USB MSC implementation.
 *
 * Composite-less device: one MSC interface on EP3, no CDC.
 *
 * Descriptors are tightly scoped: VID/PID distinct from the slot
 * builds (0xCAFE:0x4020, vs NES/P8's 0xCAFE:0x4011) so Windows
 * treats this as its own device and doesn't reuse stale drive-
 * letter assignments when users had an older slot-era firmware
 * flashed.
 *
 * MSC callbacks route straight to thumbyone_disk_* — the same
 * shared-FAT driver the slots already use for local access.
 */
#include "lobby_usb.h"

#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/time.h"

#include "thumbyone_disk.h"

/* --- write-back cache --------------------------------------------- *
 *
 * Flash erase granularity is 4 KB (8 sectors). Without a cache, each
 * 512-byte host write incurs a full RMW: read 4 KB from XIP, splat
 * in the 512 bytes, erase 4 KB, program 4 KB, verify. Small-file
 * transfers pay this per-sector and end up dominated by flash erase
 * time, giving ~kB/s throughput.
 *
 * The cache holds one 4 KB erase block. Writes that target the
 * cached block overlay directly into the buffer; writes that target
 * a different block flush the cache first, then load the new block.
 * Reads that hit the cached block come from the buffer (so
 * host-side fsync round-trips see the pending writes). A background
 * drain fires whenever MSC has been quiet for >300 ms, committing
 * the dirty block so host eject / unplug doesn't strand data.
 *
 * Mirrors the ThumbyP8 / ThumbyNES MSC pattern — both of those
 * slots had this long before ThumbyOne's lobby did.
 */
#define WB_CACHE_SIZE THUMBYONE_DISK_ERASE_SIZE
#define WB_SPB        (THUMBYONE_DISK_ERASE_SIZE / THUMBYONE_DISK_SECTOR_SIZE) /* 8 */

static uint8_t  g_wb_buf[WB_CACHE_SIZE] __attribute__((aligned(4)));
static int32_t  g_wb_block = -1;    /* -1 = empty; else flash block idx  */
static bool     g_wb_dirty = false;

/* Load the 4 KB XIP block `block` into g_wb_buf. */
static void wb_load(uint32_t block) {
    uint32_t sector = block * WB_SPB;
    thumbyone_disk_read(g_wb_buf, sector, WB_SPB);
    g_wb_block = (int32_t)block;
    g_wb_dirty = false;
}

/* Commit g_wb_buf to flash if dirty. Clears the dirty flag; keeps
 * the buffer loaded so a subsequent hit still resolves locally. */
static int wb_flush(void) {
    if (!g_wb_dirty || g_wb_block < 0) return 0;
    uint32_t sector = (uint32_t)g_wb_block * WB_SPB;
    int r = thumbyone_disk_write(g_wb_buf, sector, WB_SPB);
    if (r == 0) g_wb_dirty = false;
    return r;
}

/* Write `count` sectors from `src` starting at `sector`. Hits the
 * cache where possible; flushes + reloads when the target block
 * changes. */
static int wb_write(const uint8_t *src, uint32_t sector, uint32_t count) {
    while (count > 0) {
        uint32_t block        = sector / WB_SPB;
        uint32_t sec_in_blk   = sector % WB_SPB;
        uint32_t sec_avail    = WB_SPB - sec_in_blk;
        uint32_t sec_this     = (count < sec_avail) ? count : sec_avail;

        if (g_wb_block != (int32_t)block) {
            /* Block miss: flush any dirty buffer, then load the
             * target block so subsequent reads to it still see
             * pre-existing contents for the un-overwritten sectors. */
            if (wb_flush() != 0) return -1;
            wb_load(block);
        }

        memcpy(g_wb_buf + sec_in_blk * THUMBYONE_DISK_SECTOR_SIZE,
               src, sec_this * THUMBYONE_DISK_SECTOR_SIZE);
        g_wb_dirty = true;

        src    += sec_this * THUMBYONE_DISK_SECTOR_SIZE;
        sector += sec_this;
        count  -= sec_this;
    }
    return 0;
}

/* Read `count` sectors into `dst` starting at `sector`. Sectors that
 * fall within the currently-cached block come out of the buffer so
 * the host sees its own pending writes; everything else goes to
 * XIP. This avoids the "host read-after-write returns stale data"
 * bug that kills FAT consistency when the OS writes the FAT then
 * immediately reads it back. */
static int wb_read(uint8_t *dst, uint32_t sector, uint32_t count) {
    while (count > 0) {
        uint32_t block        = sector / WB_SPB;
        uint32_t sec_in_blk   = sector % WB_SPB;
        uint32_t sec_avail    = WB_SPB - sec_in_blk;
        uint32_t sec_this     = (count < sec_avail) ? count : sec_avail;

        if (g_wb_block == (int32_t)block) {
            memcpy(dst,
                   g_wb_buf + sec_in_blk * THUMBYONE_DISK_SECTOR_SIZE,
                   sec_this * THUMBYONE_DISK_SECTOR_SIZE);
        } else {
            if (thumbyone_disk_read(dst, sector, sec_this) != 0) return -1;
        }

        dst    += sec_this * THUMBYONE_DISK_SECTOR_SIZE;
        sector += sec_this;
        count  -= sec_this;
    }
    return 0;
}

/* --- activity tracking --------------------------------------------- */

static volatile bool     g_mounted      = false;
static volatile uint64_t g_last_op_us   = 0;

static inline void op_touch(void) { g_last_op_us = (uint64_t)time_us_64(); }

/* --- tinyUSB state hooks ------------------------------------------- */

void tud_mount_cb(void)   { g_mounted = true;  }
void tud_umount_cb(void)  { g_mounted = false; }
void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    g_mounted = false;
}
void tud_resume_cb(void)  { g_mounted = true;  }

/* --- device descriptor --------------------------------------------- */

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    /* MSC-only (not composite) → plain MSC class descriptor. */
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0xCAFE,
    .idProduct          = 0x4020,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* --- configuration descriptor -------------------------------------- */

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL,
};

#define EPNUM_MSC_OUT     0x03
#define EPNUM_MSC_IN      0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

/* --- string descriptors -------------------------------------------- */

static char const *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},   /* 0: language id — English */
    "ThumbyOne",                   /* 1: manufacturer */
    "ThumbyOne Storage",           /* 2: product */
    NULL,                          /* 3: serial — filled at runtime */
    "ThumbyOne Shared FAT",        /* 4: MSC interface */
};

static uint16_t _desc_str[32];
/* Serial prefix distinguishes this device from any slot-era
 * ThumbyNES / ThumbyP8 that the PC might remember. */
static char _serial[4 + 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1] = {0};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == 3) {
        if (_serial[0] == 0) {
            pico_unique_board_id_t bid;
            pico_get_unique_board_id(&bid);
            static const char hex[] = "0123456789ABCDEF";
            _serial[0] = 'O';
            _serial[1] = 'N';
            _serial[2] = 'E';
            _serial[3] = '-';
            for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
                _serial[4 + i * 2 + 0] = hex[(bid.id[i] >> 4) & 0xF];
                _serial[4 + i * 2 + 1] = hex[bid.id[i] & 0xF];
            }
            _serial[4 + 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES] = 0;
        }
        chr_count = (uint8_t)strlen(_serial);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = _serial[i];
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *s = string_desc_arr[index];
        if (!s) return NULL;
        chr_count = (uint8_t)strlen(s);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = s[i];
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

/* --- MSC class callbacks ------------------------------------------- */

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                         uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    const char vid[] = "ThumbyOne";
    const char pid[] = "Shared FAT     ";
    const char rev[] = "1.0 ";
    memcpy(vendor_id,  vid, 8);
    memcpy(product_id, pid, 16);
    memcpy(product_rev, rev, 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = thumbyone_disk_sector_count();
    *block_size  = (uint16_t)thumbyone_disk_sector_size();
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                            bool start, bool load_eject) {
    (void)lun; (void)power_condition; (void)start;
    /* Host eject signal: flush the write-back cache so pending
     * sectors land in flash before the drive goes away. Happens at
     * Windows "Safely Remove", macOS drag-to-trash, etc. */
    if (load_eject) wb_flush();
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           void *buffer, uint32_t bufsize) {
    (void)lun;
    op_touch();
    if (offset != 0) return -1;
    uint32_t sector_size = thumbyone_disk_sector_size();
    if (bufsize % sector_size != 0) return -1;
    uint32_t count = bufsize / sector_size;
    if (wb_read((uint8_t *)buffer, lba, count) != 0) return -1;
    return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                            uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    op_touch();
    if (offset != 0) return -1;
    uint32_t sector_size = thumbyone_disk_sector_size();
    if (bufsize % sector_size != 0) return -1;
    uint32_t count = bufsize / sector_size;
    if (wb_write(buffer, lba, count) != 0) return -1;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                         void *buffer, uint16_t bufsize) {
    (void)lun; (void)scsi_cmd; (void)buffer; (void)bufsize;
    /* SYNCHRONIZE_CACHE (0x35) would be the natural place to flush,
     * but flushing 4 KB takes ~70 ms with IRQs off — that stalls
     * tud_task() past the host's MSC timeout. Return 0 here and let
     * the main-loop drain fire when MSC goes quiet instead. */
    return 0;
}

/* --- public API ---------------------------------------------------- */

void lobby_usb_init(void) {
    tusb_init();
}

void lobby_usb_task(void) {
    tud_task();
}

bool lobby_usb_mounted(void) {
    return g_mounted;
}

uint64_t lobby_usb_last_op_us(void) {
    return g_last_op_us;
}

bool lobby_usb_cache_dirty(void) {
    return g_wb_dirty;
}

void lobby_usb_drain(void) {
    wb_flush();
}
