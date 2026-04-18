/*
 * ThumbyOne shared-FAT flash block device — direct ops, no cache.
 *
 * Reads: pull straight from the XIP-mapped FAT region. ATRANS[1]
 * (identity mapping for physical 0x400000..0x800000, configured
 * in thumbyone_slot_init for slots and inherent for the lobby)
 * makes this just a memcpy from XIP_BASE + offset.
 *
 * Writes: full erase-program cycles. Each 4 KB touched flash
 * sector is RMW'd — read current contents from XIP into a stack
 * buffer, splat in the sectors being written, erase the flash
 * block, program back. Interrupts are disabled around each
 * flash op (SDK's flash_range_erase / flash_range_program do
 * it internally) and ATRANS + fast-XIP config are restored
 * after each op (the bootrom's flash routines reset both).
 *
 * No caching here — the lobby's MSC path can layer a write-back
 * cache on top for burst-write smoothing. Slots and the lobby's
 * own mount-time reads use this directly.
 */
#include "thumbyone_disk.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"
#include "slot_layout.h"
#include "thumbyone_handoff.h"    /* for thumbyone_xip_fast_setup */

#define XIP_BASE_ADDR 0x10000000u

/* Sectors per flash erase block — 4096 / 512 = 8. */
#define SECTORS_PER_ERASE   (THUMBYONE_DISK_ERASE_SIZE / THUMBYONE_DISK_SECTOR_SIZE)


uint32_t thumbyone_disk_sector_count(void) {
    return THUMBYONE_FAT_SIZE / THUMBYONE_DISK_SECTOR_SIZE;
}

uint32_t thumbyone_disk_sector_size(void) {
    return THUMBYONE_DISK_SECTOR_SIZE;
}


/* ATRANS snapshot helpers — the SDK's flash routines reset QMI
 * during erase/program, including the identity mappings our
 * chained slot needs for this FAT region. Save on the way in,
 * restore on the way out. */
static inline void save_atrans(uint32_t out[4]) {
    out[0] = qmi_hw->atrans[0];
    out[1] = qmi_hw->atrans[1];
    out[2] = qmi_hw->atrans[2];
    out[3] = qmi_hw->atrans[3];
}

static inline void restore_atrans(const uint32_t in[4]) {
    qmi_hw->atrans[0] = in[0];
    qmi_hw->atrans[1] = in[1];
    qmi_hw->atrans[2] = in[2];
    qmi_hw->atrans[3] = in[3];
#ifdef THUMBYONE_SLOT_MODE
    /* Chained slots run in fast QPI XIP mode (thumbyone_xip_fast_setup
     * was called at handoff). The SDK's flash op exits XIP, does the
     * operation, and re-enters XIP in whatever mode the chained
     * image's boot_stage2 selects — typically single-SPI fast read,
     * NOT QPI. Without this restore the slot's code XIP silently
     * breaks after the first flash write.
     *
     * The LOBBY, on the other hand, boots at flash 0 with the stock
     * boot_stage2 already in single-SPI fast-read mode, and the SDK
     * flash op re-enters XIP in exactly that same mode — no extra
     * work needed. Calling thumbyone_xip_fast_setup from the lobby
     * would forcibly reconfigure QMI to QPI mid-transaction (we
     * tracked a Thumbatro-launches-then-FAT-wipes bug to exactly
     * this: the first large host write through the lobby's MSC path
     * triggered an unneeded QMI reconfiguration that subsequent XIP
     * reads couldn't cleanly survive). Gate the call out of the
     * lobby build. */
    thumbyone_xip_fast_setup();
#endif
}


int thumbyone_disk_read(uint8_t *dst, uint32_t sector, uint32_t count) {
    if (sector + count > thumbyone_disk_sector_count()) return -1;
    const uint8_t *xip = (const uint8_t *)
        (XIP_BASE_ADDR + THUMBYONE_FAT_OFFSET + sector * THUMBYONE_DISK_SECTOR_SIZE);
    memcpy(dst, xip, count * THUMBYONE_DISK_SECTOR_SIZE);
    return 0;
}


/* Commit one 4 KB flash block. `block_idx` is the index within
 * the FAT region (block 0 is flash offset THUMBYONE_FAT_OFFSET,
 * block 1 is +4096, etc.). `buf` must be exactly 4 KB. */
static int commit_block(uint32_t block_idx, const uint8_t *buf) {
    uint32_t flash_off = THUMBYONE_FAT_OFFSET +
                         block_idx * THUMBYONE_DISK_ERASE_SIZE;

    /* Erase — IRQs off for ~50 ms. */
    {
        uint32_t ints = save_and_disable_interrupts();
        uint32_t saved[4];
        save_atrans(saved);
        flash_range_erase(flash_off, THUMBYONE_DISK_ERASE_SIZE);
        restore_atrans(saved);
        restore_interrupts(ints);
    }

    /* Program — split into 256-byte page programs so we can let
     * IRQs breathe between pages. Each program is ~1 ms with IRQs
     * off. This matches ThumbyNES's proven pattern — without the
     * split, a single 4 KB commit keeps IRQs off for 16 ms which
     * is enough to starve USB (not an issue here since MSC isn't
     * on this path, but keep the discipline for when it is). */
    const uint32_t PROG_CHUNK = 256u;
    for (uint32_t off = 0; off < THUMBYONE_DISK_ERASE_SIZE; off += PROG_CHUNK) {
        uint32_t ints = save_and_disable_interrupts();
        uint32_t saved[4];
        save_atrans(saved);
        flash_range_program(flash_off + off, buf + off, PROG_CHUNK);
        restore_atrans(saved);
        restore_interrupts(ints);
    }

    /* Verify — read back through XIP and confirm the program
     * landed. Anything but an exact match means the flash chip
     * didn't accept the write, or ATRANS is pointing somewhere
     * wrong. Either way the caller needs to know. */
    const uint8_t *xip = (const uint8_t *)
        (XIP_BASE_ADDR + THUMBYONE_FAT_OFFSET + block_idx * THUMBYONE_DISK_ERASE_SIZE);
    if (memcmp(xip, buf, THUMBYONE_DISK_ERASE_SIZE) != 0) {
        return -1;
    }
    return 0;
}


int thumbyone_disk_write(const uint8_t *src, uint32_t sector, uint32_t count) {
    if (sector + count > thumbyone_disk_sector_count()) return -1;

    /* Walk the write range one 4 KB erase block at a time. For
     * each block, assemble the merged contents (existing XIP
     * outside the write range + incoming bytes inside) in a
     * stack buffer, then commit. */
    uint8_t buf[THUMBYONE_DISK_ERASE_SIZE];

    uint32_t remaining = count;
    uint32_t cur_sector = sector;
    const uint8_t *cur_src = src;

    while (remaining > 0) {
        uint32_t block_idx      = cur_sector / SECTORS_PER_ERASE;
        uint32_t sector_in_blk  = cur_sector % SECTORS_PER_ERASE;
        uint32_t sectors_in_blk = SECTORS_PER_ERASE - sector_in_blk;
        if (sectors_in_blk > remaining) sectors_in_blk = remaining;

        /* Seed the block buffer from XIP (the pre-existing 4 KB). */
        const uint8_t *xip = (const uint8_t *)
            (XIP_BASE_ADDR + THUMBYONE_FAT_OFFSET +
             block_idx * THUMBYONE_DISK_ERASE_SIZE);
        memcpy(buf, xip, THUMBYONE_DISK_ERASE_SIZE);

        /* Overlay the sectors being written. */
        memcpy(buf + sector_in_blk * THUMBYONE_DISK_SECTOR_SIZE,
               cur_src,
               sectors_in_blk * THUMBYONE_DISK_SECTOR_SIZE);

        if (commit_block(block_idx, buf) != 0) return -1;

        cur_sector += sectors_in_blk;
        cur_src    += sectors_in_blk * THUMBYONE_DISK_SECTOR_SIZE;
        remaining  -= sectors_in_blk;
    }
    return 0;
}


int thumbyone_disk_sync(void) {
    /* No cache at this layer → nothing to flush. A cached layer
     * (lobby MSC) would override or wrap this. */
    return 0;
}
