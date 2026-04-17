/*
 * ThumbyOne lobby — system selector.
 *
 * For now this is a minimal version that only knows about the
 * NES slot (step C1: prove that a real subproject firmware can
 * be chained to). A full-featured system selector — the one
 * PLAN.md describes — comes later in step B.
 *
 * BOOT FLOW
 *   main() first line: thumbyone_handoff_consume_if_present.
 *   If a scratch handoff is waiting, the library chains into the
 *   target slot from a pristine chip state and never returns.
 *   Otherwise main() proceeds with normal lobby init (LCD, UI).
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <string.h>

#include "thumbyone_handoff.h"
#include "slot_layout.h"
#include "ff.h"
#include "thumbyone_fs.h"

#include "lcd_gc9107.h"
#include "font.h"

#define PIN_BL        7
#define PIN_BTN_LB    6
#define PIN_BTN_A    21
#define PIN_BTN_RB   22
#define PIN_BTN_B    25
#define PIN_BTN_MENU 26

#define COL_BG     0x0000
#define COL_TEXT   0xFFFF
#define COL_TITLE  0x07FF
#define COL_ACTION 0xF81F

static uint16_t g_fb[128 * 128] __attribute__((aligned(4)));

/* 4 KiB aligned workarea — used by rom_load_partition_table
 * and rom_chain_image inside the handoff library. Reused as the
 * f_mkfs working buffer (needs at least FF_MAX_SS bytes). */
static __attribute__((aligned(4))) uint8_t g_workarea[4 * 1024];

/* Shared-FAT mount. Lives for the lifetime of the lobby process. */
static FATFS g_fs;

static bool btn_a_pressed(void)    { return !gpio_get(PIN_BTN_A); }
static bool btn_b_pressed(void)    { return !gpio_get(PIN_BTN_B); }
static bool btn_lb_pressed(void)   { return !gpio_get(PIN_BTN_LB); }
static bool btn_rb_pressed(void)   { return !gpio_get(PIN_BTN_RB); }
static bool btn_menu_pressed(void) { return !gpio_get(PIN_BTN_MENU); }

/* --- shared-FAT erase (LB+RB held at boot) -------------------------- */
/*
 * Clean recovery path. When NES/P8/MPY disagree about FAT layout on
 * the shared region at 0x660000..0x1000000, or when a filesystem gets
 * wedged such that a slot hangs reading it, the user holds LB+RB at
 * boot. We wipe the whole 9.6 MB region in chunks, drawing a progress
 * bar so it's clear the device isn't just hung, then reboot back to
 * the lobby home screen. Whichever slot boots first after that will
 * mkfs a fresh FS in its own format.
 *
 * Erasing is destructive — roms/carts/games are lost. The prompt
 * explicitly says so, and we require both buttons to STAY held for
 * the entire confirmation window so a single-button stuck-in-pocket
 * start doesn't wipe the device.
 */
static void draw_progress(const char *title, int done, int total) {
    for (int i = 0; i < 128 * 128; ++i) g_fb[i] = COL_BG;
    int w = nes_font_width_2x("THUMBY");
    nes_font_draw_2x(g_fb, "THUMBY", (128 - w) / 2, 8, COL_TITLE);
    w = nes_font_width_2x("ONE");
    nes_font_draw_2x(g_fb, "ONE", (128 - w) / 2, 22, COL_TITLE);
    nes_font_draw(g_fb, title, 2, 50, COL_ACTION);

    const int bx = 6, by = 70, bw = 116, bh = 10;
    for (int x = bx; x < bx + bw; ++x) {
        g_fb[by * 128 + x]           = COL_TEXT;
        g_fb[(by + bh) * 128 + x]    = COL_TEXT;
    }
    for (int y = by; y <= by + bh; ++y) {
        g_fb[y * 128 + bx]           = COL_TEXT;
        g_fb[y * 128 + bx + bw - 1]  = COL_TEXT;
    }
    int fill = (total > 0) ? ((bw - 2) * done) / total : 0;
    for (int y = by + 2; y < by + bh - 1; ++y)
        for (int x = bx + 1; x < bx + 1 + fill; ++x)
            g_fb[y * 128 + x] = COL_ACTION;

    char msg[32];
    int pct = (total > 0) ? (100 * done) / total : 0;
    snprintf(msg, sizeof(msg), "%d%%", pct);
    nes_font_draw(g_fb, msg, (128 - nes_font_width(msg)) / 2, 88, COL_TEXT);

    nes_font_draw(g_fb, "do not power off",  10, 110, COL_TEXT);
    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

static void draw_text_splash(const char *title_top, const char *title_bot,
                             const char *line1, const char *line2,
                             uint16_t title_col) {
    for (int i = 0; i < 128 * 128; ++i) g_fb[i] = COL_BG;
    if (title_top) {
        int w = nes_font_width_2x(title_top);
        nes_font_draw_2x(g_fb, title_top, (128 - w) / 2, 8, title_col);
    }
    if (title_bot) {
        int w = nes_font_width_2x(title_bot);
        nes_font_draw_2x(g_fb, title_bot, (128 - w) / 2, 22, title_col);
    }
    if (line1) nes_font_draw(g_fb, line1, (128 - nes_font_width(line1)) / 2, 60, COL_TEXT);
    if (line2) nes_font_draw(g_fb, line2, (128 - nes_font_width(line2)) / 2, 72, COL_TEXT);
    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

static void erase_shared_fat(void) {
    const uint32_t off   = THUMBYONE_FAT_OFFSET;
    const uint32_t size  = THUMBYONE_FAT_SIZE;
    /* Erase in 64 KB chunks so we can update the progress bar and
     * never spend >~1 s with interrupts off per call. The SDK's
     * flash_range_erase is itself chunked internally, but it
     * disables interrupts around the entire range and we don't
     * want a single interrupts-off window of ~2 minutes. */
    const uint32_t CHUNK = 64u * 1024u;
    uint32_t done = 0;
    draw_progress("erasing shared FAT", 0, (int)size);
    while (done < size) {
        uint32_t n = (size - done > CHUNK) ? CHUNK : (size - done);
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(off + done, n);
        restore_interrupts(ints);
        done += n;
        draw_progress("erasing shared FAT", (int)done, (int)size);
    }

    /* Format the now-virgin region with the canonical shape so the
     * next slot launch finds a ready FAT. Reuses the lobby's boot
     * auto-format helper. */
    draw_text_splash("THUMBY", "ONE", "formatting", "shared FAT...", COL_TITLE);
    FRESULT r = thumbyone_fs_format(&g_fs, g_workarea, sizeof(g_workarea));
    if (r != FR_OK) {
        /* Formatting failed after a successful erase. Hard to
         * recover without user intervention — flash an error splash
         * and reboot; the auto-format path on the next boot will
         * try again. */
        draw_text_splash("WIPE", "FAIL", "format error", "rebooting...", COL_ACTION);
        sleep_ms(1500);
        watchdog_reboot(0, 0, 0);
        while (1) tight_loop_contents();
    }

    /* Success splash, then reboot so every peripheral state is
     * fresh for the lobby home screen. */
    draw_text_splash("DONE", NULL, "rebooting...", NULL, COL_ACTION);
    sleep_ms(800);
    watchdog_reboot(0, 0, 0);
    while (1) tight_loop_contents();
}

/* Confirm the chord: both buttons must be held together for ~1 s
 * after the initial detection. Draws a countdown so the user sees
 * what's about to happen. Aborts (returns false) if either is
 * released at any point. */
static bool confirm_erase_chord(void) {
    const int HOLD_MS   = 1200;
    const int STEP_MS   = 50;
    const int STEPS     = HOLD_MS / STEP_MS;
    for (int i = 0; i < STEPS; ++i) {
        if (!btn_lb_pressed() || !btn_rb_pressed()) return false;
        for (int k = 0; k < 128 * 128; ++k) g_fb[k] = COL_BG;
        int w = nes_font_width_2x("WIPE");
        nes_font_draw_2x(g_fb, "WIPE",  (128 - w) / 2, 8, COL_ACTION);
        w = nes_font_width_2x("FAT?");
        nes_font_draw_2x(g_fb, "FAT?",  (128 - w) / 2, 22, COL_ACTION);
        nes_font_draw(g_fb, "erases roms,",    14, 48, COL_TEXT);
        nes_font_draw(g_fb, "carts, games.",   10, 58, COL_TEXT);
        nes_font_draw(g_fb, "keep holding",    14, 78, COL_TEXT);
        nes_font_draw(g_fb, "LB + RB...",      22, 88, COL_TEXT);
        /* Countdown bar */
        int bw = ((128 - 20) * i) / STEPS;
        for (int y = 104; y < 110; ++y)
            for (int x = 10; x < 10 + bw; ++x)
                g_fb[y * 128 + x] = COL_ACTION;
        nes_lcd_present(g_fb);
        nes_lcd_wait_idle();
        sleep_ms(STEP_MS);
    }
    return btn_lb_pressed() && btn_rb_pressed();
}


static void render_home(void) {
    for (int i = 0; i < 128 * 128; ++i) g_fb[i] = COL_BG;

    int w = nes_font_width_2x("THUMBY");
    nes_font_draw_2x(g_fb, "THUMBY", (128 - w) / 2, 8, COL_TITLE);
    w = nes_font_width_2x("ONE");
    nes_font_draw_2x(g_fb, "ONE", (128 - w) / 2, 22, COL_TITLE);

    for (int x = 4; x < 124; ++x) g_fb[38 * 128 + x] = COL_TEXT;

    nes_font_draw(g_fb, "system selector",  2, 46, COL_TEXT);
    nes_font_draw(g_fb, "A:   launch NES",  2, 58, COL_TEXT);
    nes_font_draw(g_fb, "B:   launch P8",   2, 68, COL_TEXT);
    nes_font_draw(g_fb, "LB:  launch DOOM", 2, 78, COL_TEXT);
    nes_font_draw(g_fb, "RB:  launch MPY",  2, 88, COL_TEXT);
    nes_font_draw(g_fb, "MENU: reboot",     2, 98, COL_TEXT);

    nes_font_draw(g_fb, "hold MENU at boot:", 4, 110, COL_ACTION);
    nes_font_draw(g_fb, "force lobby recovery",4, 120, COL_ACTION);

    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

int main(void) {
    /* Boot-time escape hatch: init the MENU button FIRST so we can
     * check it before the handoff. Holding MENU at power-on bypasses
     * consume_if_present and forces the lobby to come up, even if
     * a stale watchdog scratch from a previous broken slot would
     * otherwise chain us straight into that slot.
     *
     * Why this matters: BOOTSEL-reflash on RP2350 does NOT reliably
     * clear the watchdog scratch. A slot that requested a chain,
     * then crashed, can leave scratch[0..1] set. On next boot (even
     * after the user reflashes a "fix"), the lobby sees the stale
     * request and chains into the slot — if that slot hangs or
     * drops to a non-LCD REPL, the device looks bricked.
     *
     * MENU-held-at-boot is the documented recovery. It always
     * lands you in the lobby. From there LB+RB wipes the FAT and
     * every slot agrees on a clean canonical format. */
    gpio_init(PIN_BTN_MENU); gpio_set_dir(PIN_BTN_MENU, GPIO_IN); gpio_pull_up(PIN_BTN_MENU);

    /* Give the GPIO pull-up a couple of microseconds to settle
     * before reading — cold boot the line hasn't fully risen yet. */
    for (volatile int i = 0; i < 1000; ++i) { __asm__ volatile("nop"); }

    if (!btn_menu_pressed()) {
        /* Handoff check BEFORE any peripheral init — required for
         * rom_chain_image to work reliably on RP2350. If the scratch
         * magic is set, the library chains into the target slot's
         * partition and never returns. */
        thumbyone_handoff_consume_if_present(g_workarea, sizeof(g_workarea));
    } else {
        /* User wants to escape to lobby. Clear any stale handoff
         * so the NEXT boot isn't still trying to chain somewhere. */
        thumbyone_handoff_clear();
    }

    /* Normal lobby flow — no handoff pending. */
    gpio_init(PIN_BTN_A);    gpio_set_dir(PIN_BTN_A, GPIO_IN);    gpio_pull_up(PIN_BTN_A);
    gpio_init(PIN_BTN_B);    gpio_set_dir(PIN_BTN_B, GPIO_IN);    gpio_pull_up(PIN_BTN_B);
    gpio_init(PIN_BTN_LB);   gpio_set_dir(PIN_BTN_LB, GPIO_IN);   gpio_pull_up(PIN_BTN_LB);
    gpio_init(PIN_BTN_RB);   gpio_set_dir(PIN_BTN_RB, GPIO_IN);   gpio_pull_up(PIN_BTN_RB);

    nes_lcd_init();
    nes_lcd_backlight(1);

    /* Recovery chord: LB+RB held at boot → wipe shared FAT. Runs
     * BEFORE home-screen render so there's no ambiguity about what
     * triggered the prompt. confirm_erase_chord requires continuous
     * hold; aborts silently and falls through to the home screen
     * if the user releases either button. */
    if (btn_lb_pressed() && btn_rb_pressed()) {
        if (confirm_erase_chord()) {
            erase_shared_fat();   /* reboots — never returns */
        }
    }

    /* Mount the shared FAT, formatting it on first boot if
     * needed. Lobby owns the canonical shape — slots only ever
     * mount. If the auto-format path runs (virgin flash, or just
     * after the LB+RB wipe chord + reboot) this takes ~1 s and
     * the user sees the "formatting" splash. */
    {
        FRESULT r = thumbyone_fs_mount(&g_fs);
        if (r == FR_NO_FILESYSTEM) {
            draw_text_splash("THUMBY", "ONE", "formatting", "shared FAT...", COL_TITLE);
            r = thumbyone_fs_format(&g_fs, g_workarea, sizeof(g_workarea));
        }
        if (r != FR_OK) {
            /* Fatal: can't mount and can't format. Show something
             * user-actionable rather than a blank screen — they
             * can still use DOOM which doesn't need the shared FS. */
            draw_text_splash("FS", "ERR", "mount failed", "try LB+RB wipe", COL_ACTION);
            sleep_ms(3000);
        }
    }

    render_home();

    while (1) {
        if (btn_a_pressed()) {
            while (btn_a_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_slot(THUMBYONE_SLOT_NES);
            /* does not return */
        }
        if (btn_b_pressed()) {
            while (btn_b_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_slot(THUMBYONE_SLOT_P8);
            /* does not return */
        }
        if (btn_lb_pressed()) {
            while (btn_lb_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_slot(THUMBYONE_SLOT_DOOM);
            /* does not return */
        }
        if (btn_rb_pressed()) {
            while (btn_rb_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_slot(THUMBYONE_SLOT_MPY);
            /* does not return */
        }
        if (btn_menu_pressed()) {
            while (btn_menu_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_lobby();
            /* does not return */
        }
        sleep_ms(20);
    }
}
