/*
 * ThumbyOne handoff — implementation.
 *
 * Minimal dependencies: pico_stdlib for sleep_ms, pico_bootrom
 * for rom_load_partition_table / rom_get_partition_table_info /
 * rom_chain_image / rom_reboot / reset_usb_boot, hardware_watchdog
 * for watchdog_reboot and the scratch registers, boot/picobin for
 * the partition-location bitfield macros.
 */
#include "thumbyone_handoff.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/qmi.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"


/* ---- ATRANS slots 1..3 identity setup ----------------------------- */
/*
 * rom_chain_image sets up QMI ATRANS slot 0 for the partition
 * it's launching, and leaves slots 1..3 at SIZE=0. Any read in
 * slots 1..3's windows (XIP 0x10400000..0x11000000) then causes
 * a bus fault.
 *
 * Every ThumbyOne slot needs the shared FAT (at physical
 * 0x660000+, in slot 1) and potentially the P8 active-cart
 * scratch (0x620000, also in slot 1). ROMs in the FAT can live
 * anywhere in the 9.6 MB FAT region (up to physical 0x1000000,
 * in slots 1/2/3). And DOOM's get_end_of_flash scan walks the
 * full XIP window looking for its image tail.
 *
 * Simplest unified fix: set slots 1..3 to identity — logical
 * 0x10400000..0x11000000 maps to physical 0x400000..0x1000000.
 * Slot 0 is left alone (bootrom set it for our partition; we
 * rely on that mapping for our own code).
 *
 * Constructor priority 101 — runs before main, before any user
 * constructors (slot code) or flash operations. */
__attribute__((constructor(101)))
static void thumbyone_atrans_init(void) {
    qmi_hw->atrans[1] = (0x400u << 16) | 0x400u;  /* 0x400000..0x800000 */
    qmi_hw->atrans[2] = (0x400u << 16) | 0x800u;  /* 0x800000..0xC00000 */
    qmi_hw->atrans[3] = (0x400u << 16) | 0xC00u;  /* 0xC00000..0x1000000 */
    __asm__ volatile("dsb" ::: "memory");
}


/* ---- Scratch format helpers ---------------------------------------- */

static uint32_t scratch0_for_slot(thumbyone_slot_t s) {
    return THUMBYONE_HANDOFF_MAGIC_BASE
         | ((uint32_t)s & THUMBYONE_HANDOFF_SLOT_MASK);
}

static uint32_t scratch1_for(uint32_t s0) {
    return s0 ^ THUMBYONE_HANDOFF_CHECK;
}

static bool parse_scratch(uint32_t s0, uint32_t s1,
                          thumbyone_slot_t *out_slot) {
    if ((s0 & THUMBYONE_HANDOFF_MAGIC_MASK) != THUMBYONE_HANDOFF_MAGIC_BASE)
        return false;
    if (s1 != scratch1_for(s0))
        return false;
    uint32_t slot = s0 & THUMBYONE_HANDOFF_SLOT_MASK;
    if (slot >= THUMBYONE_SLOT_COUNT)
        return false;
    if (out_slot) *out_slot = (thumbyone_slot_t)slot;
    return true;
}


/* ---- Public API ---------------------------------------------------- */

bool thumbyone_handoff_peek(thumbyone_slot_t *out_slot) {
    return parse_scratch(watchdog_hw->scratch[0],
                         watchdog_hw->scratch[1],
                         out_slot);
}

void thumbyone_handoff_clear(void) {
    watchdog_hw->scratch[0] = 0;
    watchdog_hw->scratch[1] = 0;
}

void thumbyone_handoff_request_slot(thumbyone_slot_t slot) {
    /* Caller must pass a launchable slot; lobby is handled by
     * request_lobby. Be defensive: if someone passes the lobby,
     * just do the lobby path. */
    if (slot == THUMBYONE_SLOT_LOBBY
        || thumbyone_slot_partition_id(slot) < 0) {
        thumbyone_handoff_request_lobby();
        return;
    }

    uint32_t s0 = scratch0_for_slot(slot);
    watchdog_hw->scratch[0] = s0;
    watchdog_hw->scratch[1] = scratch1_for(s0);

    stdio_uart_deinit();

    /* Full reboot through bootrom. On the next boot the lobby
     * will call consume_if_present and chain. */
    rom_reboot(
        REBOOT2_FLAG_REBOOT_TYPE_NORMAL
          | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
        10, 0, 0
    );

    /* Unreachable — last-resort fallback. */
    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

void thumbyone_handoff_request_lobby(void) {
    thumbyone_handoff_clear();
    watchdog_reboot(0, 0, 0);
    while (1) tight_loop_contents();  /* unreachable */
}


/* ---- Chain helper — the pre-init fast path ------------------------- */

static void chain_to_partition(uint8_t *workarea, uint32_t workarea_size,
                                int partition_id) {
    /* Load PT into bootrom state. */
    int rc = rom_load_partition_table(workarea, workarea_size, false);
    if (rc != 0) goto fail;

    /* Look up the partition's flash location. */
    uint32_t info[8];
    rc = rom_get_partition_table_info(info, sizeof(info) / 4,
            PT_INFO_PARTITION_LOCATION_AND_FLAGS
          | PT_INFO_SINGLE_PARTITION
          | ((uint32_t)partition_id << 24));
    if (rc != 3) goto fail;

    uint32_t locperm = info[1];
    uint32_t first_sector = (locperm & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS)
                            >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    uint32_t last_sector  = (locperm & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)
                            >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    uint32_t start = XIP_BASE + first_sector * 0x1000;
    uint32_t size  = (last_sector - first_sector + 1) * 0x1000;

    /* Chain — does not return on success. */
    rom_chain_image(workarea, workarea_size, start, size);

fail:
    /* Fall back to BOOTSEL so the user can re-flash instead of
     * being stuck. The only way to reach here is a misconfigured
     * or missing partition, which means the flash contents don't
     * match the firmware — BOOTSEL is the right escape hatch. */
    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

void thumbyone_handoff_consume_if_present(uint8_t *workarea,
                                           uint32_t workarea_size) {
    thumbyone_slot_t slot;
    if (!thumbyone_handoff_peek(&slot)) return;
    if (slot == THUMBYONE_SLOT_LOBBY) { thumbyone_handoff_clear(); return; }

    int pid = thumbyone_slot_partition_id(slot);
    if (pid < 0) { thumbyone_handoff_clear(); return; }

    /* Clear the magic BEFORE chaining — on a failed chain the
     * BOOTSEL fallback will leave the device in BOOTSEL, but we
     * don't want a subsequent manual reset to loop back into
     * chain attempts. */
    thumbyone_handoff_clear();
    chain_to_partition(workarea, workarea_size, pid);
    /* does not return */
}
