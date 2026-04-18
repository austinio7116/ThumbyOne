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
    (void)lun; (void)power_condition; (void)start; (void)load_eject;
    /* We ack eject and otherwise ignore — all writes go synchronously
     * through thumbyone_disk_write so there's no deferred cache to
     * flush before an eject. */
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
    if (thumbyone_disk_read((uint8_t *)buffer, lba, count) != 0) return -1;
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
    if (thumbyone_disk_write(buffer, lba, count) != 0) return -1;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                         void *buffer, uint16_t bufsize) {
    (void)lun; (void)scsi_cmd; (void)buffer; (void)bufsize;
    /* SYNCHRONIZE_CACHE etc.: thumbyone_disk_write is already
     * synchronous, so no flush work needed. Return 0 to signal we
     * handled it without stalling — tinyUSB then ACKs to the host. */
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
