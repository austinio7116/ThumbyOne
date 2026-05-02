/*
 * ThumbyOne — shared system-wide settings (volume + brightness).
 *
 * Backing store: one 4 KB flash sector at
 * THUMBYONE_SETTINGS_MIRROR_OFFSET (see slot_layout.h). Reads go
 * direct via XIP — microseconds, zero FatFs overhead. Writes erase
 * the sector and program page 0 with the new values, ~50 ms with
 * interrupts disabled.
 *
 * Why a flash sector rather than a FatFs file?
 *   - DOOM has no FatFs; this way DOOM reads from the same place
 *     as everyone else (one source of truth).
 *   - Settings are two bytes; a 512-byte-sector FatFs file with a
 *     directory entry + cluster + FAT update is absurd overhead.
 *   - FatFs write-back caches in NES / P8 / MPY went away as a
 *     concern — we bypass FatFs entirely.
 *
 * Layout inside the sector:
 *   byte 0..3 : magic "TSM1"
 *   byte 4    : volume     (0..20)
 *   byte 5    : brightness (0..255)
 *   rest      : 0xFF (erased flash)
 *
 * Readers tolerate an erased / corrupt sector (on first boot of a
 * fresh flash, or after LB+RB wipe) by returning the default values.
 */
#include "thumbyone_settings.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"

#include "slot_layout.h"
#include "thumbyone_handoff.h"  /* thumbyone_xip_fast_setup */

#define TSM_MAGIC_0  'T'
#define TSM_MAGIC_1  'S'
#define TSM_MAGIC_2  'M'
#define TSM_MAGIC_3  '1'

static const volatile uint8_t *sector_xip(void) {
    return (const volatile uint8_t *)
        (THUMBYONE_FLASH_BASE + THUMBYONE_SETTINGS_MIRROR_OFFSET);
}

static bool sector_valid(const volatile uint8_t *p) {
    return p[0] == TSM_MAGIC_0 && p[1] == TSM_MAGIC_1 &&
           p[2] == TSM_MAGIC_2 && p[3] == TSM_MAGIC_3;
}

uint8_t thumbyone_settings_load_volume(void) {
    const volatile uint8_t *p = sector_xip();
    if (!sector_valid(p)) return THUMBYONE_VOLUME_DEFAULT;
    uint8_t v = p[4];
    if (v > THUMBYONE_VOLUME_MAX) return THUMBYONE_VOLUME_DEFAULT;
    return v;
}

uint8_t thumbyone_settings_load_brightness(void) {
    const volatile uint8_t *p = sector_xip();
    if (!sector_valid(p)) return THUMBYONE_BRIGHTNESS_DEFAULT;
    uint8_t b = p[5];
    /* uint8_t can't exceed BRIGHTNESS_MAX=255 so no upper clamp. */
    return b;
}

/* Flash ops save/restore ATRANS because SDK flash_range_* resets
 * QMI; without restoring we'd break XIP reads back in the caller's
 * slot partition. Same pattern as every other flash writer in
 * this codebase. */
static inline void save_atrans(uint32_t out[4]) {
    out[0] = qmi_hw->atrans[0];
    out[1] = qmi_hw->atrans[1];
    out[2] = qmi_hw->atrans[2];
    out[3] = qmi_hw->atrans[3];
}

static inline void restore_atrans_and_xip(const uint32_t in[4]) {
    qmi_hw->atrans[0] = in[0];
    qmi_hw->atrans[1] = in[1];
    qmi_hw->atrans[2] = in[2];
    qmi_hw->atrans[3] = in[3];
    /* SDK's flash_range_erase / flash_range_program internally call
     * rom_flash_enter_cmd_xip, which leaves QMI in slow safe-mode
     * single-bit read. Without restoring fast QPI XIP here, every
     * flash read after this point — including the slot's own code
     * that's executing in place — runs 4-8× slower until the next
     * fast-XIP setup. Frame rate visibly drops in any emulator
     * runner whose volume/brightness slider triggered this. Same
     * call as nes_flash_disk's restore path. */
    thumbyone_xip_fast_setup();
}

static bool write_pair(uint8_t volume, uint8_t brightness) {
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    page[0] = TSM_MAGIC_0;
    page[1] = TSM_MAGIC_1;
    page[2] = TSM_MAGIC_2;
    page[3] = TSM_MAGIC_3;
    page[4] = volume;
    page[5] = brightness;

    const uint32_t off = THUMBYONE_SETTINGS_MIRROR_OFFSET;
    uint32_t ints = save_and_disable_interrupts();
    uint32_t saved[4];
    save_atrans(saved);
    flash_range_erase(off, FLASH_SECTOR_SIZE);
    flash_range_program(off, page, FLASH_PAGE_SIZE);
    restore_atrans_and_xip(saved);
    restore_interrupts(ints);

    const volatile uint8_t *p = sector_xip();
    return sector_valid(p) && p[4] == volume && p[5] == brightness;
}

bool thumbyone_settings_save_volume(uint8_t volume) {
    if (volume > THUMBYONE_VOLUME_MAX) volume = THUMBYONE_VOLUME_MAX;
    return write_pair(volume, thumbyone_settings_load_brightness());
}

bool thumbyone_settings_save_brightness(uint8_t brightness) {
    if (brightness > THUMBYONE_BRIGHTNESS_MAX) brightness = THUMBYONE_BRIGHTNESS_MAX;
    return write_pair(thumbyone_settings_load_volume(), brightness);
}


/* --- Legacy-thumby render settings on shared FAT ------------------ *
 * These live in tiny ASCII files (one byte: '0'..'4' or '0'/'1')
 * because the MPY slot is the sole consumer and reads via plain
 * Python file I/O. Avoids the C-binding-from-MicroPython dance the
 * flash-mirror path would require.
 *
 * DOOM doesn't link FatFs, so the FAT-using functions are stubbed
 * out via __has_include guard. DOOM never reads or writes legacy-
 * thumby settings anyway — they're MPY-slot-only. */
#if __has_include("ff.h")
#include "ff.h"
#define THUMBYONE_SETTINGS_HAS_FAT 1
#else
#define THUMBYONE_SETTINGS_HAS_FAT 0
#endif

#if THUMBYONE_SETTINGS_HAS_FAT
static uint8_t load_ascii_byte(const char *path, uint8_t lo, uint8_t hi, uint8_t dflt) {
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return dflt;
    uint8_t buf[2] = {0};
    UINT br = 0;
    f_read(&f, buf, 1, &br);
    f_close(&f);
    if (br < 1) return dflt;
    if (buf[0] < '0' || buf[0] > '9') return dflt;
    uint8_t v = (uint8_t)(buf[0] - '0');
    if (v < lo || v > hi) return dflt;
    return v;
}

static bool save_ascii_byte(const char *path, uint8_t v, uint8_t lo, uint8_t hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (v > 9) return false;   /* single-digit invariant */
    FIL f;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    uint8_t buf[1] = { (uint8_t)('0' + v) };
    UINT bw = 0;
    bool ok = (f_write(&f, buf, 1, &bw) == FR_OK) && (bw == 1);
    f_close(&f);
    return ok;
}

uint8_t thumbyone_settings_load_legacy_scale(void) {
    return load_ascii_byte("/.legacy_scale",
                           THUMBYONE_LEGACY_SCALE_MIN,
                           THUMBYONE_LEGACY_SCALE_MAX,
                           THUMBYONE_LEGACY_SCALE_DEFAULT);
}

bool thumbyone_settings_save_legacy_scale(uint8_t idx) {
    return save_ascii_byte("/.legacy_scale", idx,
                           THUMBYONE_LEGACY_SCALE_MIN,
                           THUMBYONE_LEGACY_SCALE_MAX);
}

uint8_t thumbyone_settings_load_legacy_fps(void) {
    return load_ascii_byte("/.legacy_fps", 0, 1,
                           THUMBYONE_LEGACY_FPS_DEFAULT);
}

bool thumbyone_settings_save_legacy_fps(uint8_t enabled) {
    return save_ascii_byte("/.legacy_fps", enabled ? 1 : 0, 0, 1);
}
#else  /* DOOM build — no FatFs */
uint8_t thumbyone_settings_load_legacy_scale(void) {
    return THUMBYONE_LEGACY_SCALE_DEFAULT;
}
bool thumbyone_settings_save_legacy_scale(uint8_t idx) {
    (void)idx; return false;
}
uint8_t thumbyone_settings_load_legacy_fps(void) {
    return THUMBYONE_LEGACY_FPS_DEFAULT;
}
bool thumbyone_settings_save_legacy_fps(uint8_t enabled) {
    (void)enabled; return false;
}
#endif
