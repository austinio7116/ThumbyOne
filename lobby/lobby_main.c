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
#include "lobby_usb.h"
#include "pico/time.h"

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


#define COL_USB    0x07E0   /* green */
#define COL_USB_ON 0xFFE0   /* yellow — flashes during transfers */

/* Tracks what the USB row last displayed so we only redraw when
 * the state actually changes — redrawing 128x128 at 60 Hz just to
 * animate an indicator is pointless. */
typedef enum {
    USB_ROW_NONE = 0,    /* unplugged */
    USB_ROW_READY,       /* host has mounted the drive */
    USB_ROW_ACTIVE,      /* transfer in the last ~400 ms */
} usb_row_state_t;

static usb_row_state_t g_usb_row_state = USB_ROW_NONE;

static void draw_usb_row(usb_row_state_t st) {
    /* Clear the two-row strip (y=108..126). */
    for (int y = 108; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            g_fb[y * 128 + x] = COL_BG;
    switch (st) {
    case USB_ROW_NONE:
        nes_font_draw(g_fb, "hold MENU at boot:", 4, 110, COL_ACTION);
        nes_font_draw(g_fb, "force lobby recovery",4, 120, COL_ACTION);
        break;
    case USB_ROW_READY:
        nes_font_draw(g_fb, "USB connected", 18, 110, COL_USB);
        nes_font_draw(g_fb, "drop files on drive", 4, 120, COL_USB);
        break;
    case USB_ROW_ACTIVE:
        nes_font_draw(g_fb, "USB transfer...", 14, 110, COL_USB_ON);
        nes_font_draw(g_fb, "do not unplug",   20, 120, COL_USB_ON);
        break;
    }
    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
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

    /* The bottom strip gets managed by draw_usb_row so initial state
     * shows the "hold MENU at boot" hint until a host mounts. */
    g_usb_row_state = USB_ROW_NONE;
    draw_usb_row(g_usb_row_state);
}

/* Compute which state the USB row should be in right now. */
static usb_row_state_t usb_current_row_state(void) {
    if (!lobby_usb_mounted()) return USB_ROW_NONE;
    uint64_t last = lobby_usb_last_op_us();
    if (last == 0) return USB_ROW_READY;
    uint64_t delta = (uint64_t)time_us_64() - last;
    return (delta < 400000) ? USB_ROW_ACTIVE : USB_ROW_READY;
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

    /* Bring up USB MSC — single place in the firmware that exposes
     * the shared FAT to a host. Slots never enumerate USB, so this
     * is the one-and-only drive Windows/macOS/Linux ever sees from
     * this device. */
    lobby_usb_init();

    render_home();

    /* Slot-launch intent: set by a button press, consumed after the
     * button is released AND USB has been quiet for a moment. This
     * avoids yanking a slot out mid-transfer when the user fumbles
     * the D-pad while Windows is still copying a file. */
    int pending_slot = -1;   /* -1 = none */
    uint64_t pending_since_us = 0;

    /* Small helper to pump USB + refresh the bottom UI row. Called
     * from the tight button-wait loops too so we never stall USB. */
    absolute_time_t next_row_check = make_timeout_time_ms(0);
    #define USB_PUMP() do {                                              \
        lobby_usb_task();                                                \
        if (absolute_time_diff_us(get_absolute_time(), next_row_check)   \
                <= 0) {                                                  \
            usb_row_state_t st = usb_current_row_state();                \
            if (st != g_usb_row_state) {                                 \
                g_usb_row_state = st;                                    \
                draw_usb_row(st);                                        \
            }                                                            \
            next_row_check = make_timeout_time_ms(100);                  \
        }                                                                \
    } while (0)

    while (1) {
        USB_PUMP();

        /* Debounce: record intent on press, but defer the actual
         * handoff until the button is released AND no MSC activity
         * has happened in the last 500 ms. */
        if (pending_slot < 0) {
            if (btn_a_pressed())       pending_slot = THUMBYONE_SLOT_NES;
            else if (btn_b_pressed())  pending_slot = THUMBYONE_SLOT_P8;
            else if (btn_lb_pressed()) pending_slot = THUMBYONE_SLOT_DOOM;
            else if (btn_rb_pressed()) pending_slot = THUMBYONE_SLOT_MPY;
            else if (btn_menu_pressed()) pending_slot = -2;   /* reboot lobby */
            if (pending_slot != -1) {
                pending_since_us = (uint64_t)time_us_64();
            }
        }

        if (pending_slot != -1) {
            /* Wait for release. Pump USB while we wait. */
            bool still_held =
                (pending_slot == THUMBYONE_SLOT_NES  && btn_a_pressed())  ||
                (pending_slot == THUMBYONE_SLOT_P8   && btn_b_pressed())  ||
                (pending_slot == THUMBYONE_SLOT_DOOM && btn_lb_pressed()) ||
                (pending_slot == THUMBYONE_SLOT_MPY  && btn_rb_pressed()) ||
                (pending_slot == -2                  && btn_menu_pressed());
            if (!still_held) {
                /* Wait for USB to go quiet (500 ms since last op) so
                 * any in-flight host write has completed before we
                 * hand the FAT to a slot. */
                uint64_t last = lobby_usb_last_op_us();
                uint64_t now  = (uint64_t)time_us_64();
                bool quiet = (last == 0) || ((now - last) > 500000);
                /* Also require at least 50 ms since the press itself
                 * to debounce finger-bounce. */
                bool stable = (now - pending_since_us) > 50000;
                if (quiet && stable) {
                    nes_lcd_wait_idle();
                    if (pending_slot == -2) thumbyone_handoff_request_lobby();
                    else thumbyone_handoff_request_slot(pending_slot);
                    /* does not return */
                }
            }
        }

        sleep_ms(5);   /* shorter than before — keeps USB responsive */
    }
    #undef USB_PUMP
}
