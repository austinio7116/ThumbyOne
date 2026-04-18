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
#include "hardware/pwm.h"
#include <stdio.h>
#include <string.h>

#include "thumbyone_handoff.h"
#include "slot_layout.h"
#include "ff.h"
#include "thumbyone_fs.h"

#include "lcd_gc9107.h"
#include "font.h"
#include "lobby_usb.h"
#include "lobby_icons.h"
#include "pico/time.h"

#define PIN_BL        7
#define PIN_BTN_LEFT  0
#define PIN_BTN_UP    1
#define PIN_BTN_RIGHT 2
#define PIN_BTN_DOWN  3
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
static bool btn_up_pressed(void)    { return !gpio_get(PIN_BTN_UP); }
static bool btn_down_pressed(void)  { return !gpio_get(PIN_BTN_DOWN); }
static bool btn_left_pressed(void)  { return !gpio_get(PIN_BTN_LEFT); }
static bool btn_right_pressed(void) { return !gpio_get(PIN_BTN_RIGHT); }

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


#define COL_USB_OFF 0x4208   /* dim grey — not connected */
#define COL_USB_ON  0x07E0   /* green — mounted */
#define COL_USB_BUSY 0xFFE0  /* yellow — transferring */

/* --- physical RGB LED --------------------------------------------- *
 * The Thumby Color's status LED is a common-anode RGB tri-LED driven
 * from GPIOs 10/11/12. Pin map + PWM wrap match the engine's
 * engine_io_rp3.c so the lobby's LED behaviour feels the same as a
 * game's engine_io.indicator(color) call.
 *
 * Common-anode: a PWM level of 0 is fully on, 2047 is fully off. */
#define PIN_LED_G   10   /* PWM5 A */
#define PIN_LED_R   11   /* PWM5 B */
#define PIN_LED_B   12   /* PWM6 A */
#define LED_PWM_WRAP 2048

/* Set the LED to a normalised RGB triple (each 0..255). */
static void led_set_rgb(int r, int g, int b) {
    /* Invert for common-anode. Scale 0..255 → 0..2047. */
    pwm_set_gpio_level(PIN_LED_R, (uint16_t)((255 - r) * LED_PWM_WRAP / 255));
    pwm_set_gpio_level(PIN_LED_G, (uint16_t)((255 - g) * LED_PWM_WRAP / 255));
    pwm_set_gpio_level(PIN_LED_B, (uint16_t)((255 - b) * LED_PWM_WRAP / 255));
}

static void led_set_off(void)    { led_set_rgb(0, 0, 0); }
/* RGB LED channels aren't perceptually balanced at equal PWM — the
 * red die is brighter per unit drive than green or blue on this
 * common-anode module, so (60, 60, 60) reads as pink. Push green
 * up and red down to land on something that actually looks white.
 * Total brightness stays "dim" (the LED is painfully bright at
 * full drive). */
static void led_set_white(void)  { led_set_rgb(25, 80, 55); }
static void led_set_green(void)  { led_set_rgb(0, 80, 0); }
static void led_set_yellow(void) { led_set_rgb(90, 60, 0); }

static void led_setup(void) {
    /* Slice 5 drives R + G, slice 6 drives B. */
    gpio_set_function(PIN_LED_R, GPIO_FUNC_PWM);
    gpio_set_function(PIN_LED_G, GPIO_FUNC_PWM);
    gpio_set_function(PIN_LED_B, GPIO_FUNC_PWM);
    uint slice_rg = pwm_gpio_to_slice_num(PIN_LED_R);
    uint slice_b  = pwm_gpio_to_slice_num(PIN_LED_B);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, LED_PWM_WRAP);
    pwm_init(slice_rg, &cfg, true);
    pwm_init(slice_b,  &cfg, true);
    /* Start with the default white — the lobby's USB state row
     * will override this once the main loop starts, but we want
     * the LED visibly on from first frame rather than dark for
     * the few ms before the first state update. */
    led_set_white();
}

/* Top-right USB indicator. A single-character-width "USB" label
 * followed by a 5x5 filled square acting as an LED. State changes
 * re-draw just the strip rather than the whole home screen.
 *
 * Earlier this lived as a two-row text strip at y=108, but that
 * strip clipped the lower row of 48x48 tiles (tile bottoms sit at
 * y=116). Pulling the indicator up into the header row lets the
 * full tiles be visible. */
typedef enum {
    USB_ROW_NONE = 0,    /* unplugged */
    USB_ROW_READY,       /* host has mounted the drive */
    USB_ROW_ACTIVE,      /* transfer in the last ~400 ms */
} usb_row_state_t;

static usb_row_state_t g_usb_row_state = USB_ROW_NONE;

/* Header strip extent (y=0..10). draw_usb_row only repaints the
 * right-hand portion so the "ThumbyOne" title on the left doesn't
 * flicker on every USB state change. */
#define USB_LED_X   119   /* 5x5 dot at the far right */
#define USB_LED_Y   3
#define USB_LED_S   5
#define USB_LABEL_X 103   /* "USB" text just left of the LED */

static void draw_usb_row(usb_row_state_t st) {
    /* Repaint only the right-hand header strip. */
    for (int y = 0; y < 11; ++y)
        for (int x = 100; x < 128; ++x)
            g_fb[y * 128 + x] = COL_BG;

    uint16_t led_col;
    uint16_t lbl_col;
    switch (st) {
    case USB_ROW_ACTIVE:
        led_col = COL_USB_BUSY;
        lbl_col = COL_USB_BUSY;
        led_set_yellow();
        break;
    case USB_ROW_READY:
        led_col = COL_USB_ON;
        lbl_col = COL_USB_ON;
        led_set_green();
        break;
    case USB_ROW_NONE:
    default:
        led_col = COL_USB_OFF;
        lbl_col = COL_USB_OFF;
        /* White matches the rest-of-system indicator default — the
         * LED is never "off" during normal use, just not signalling
         * USB. */
        led_set_white();
        break;
    }

    nes_font_draw(g_fb, "USB", USB_LABEL_X, 2, lbl_col);
    /* LED body. */
    for (int j = 0; j < USB_LED_S; ++j)
        for (int i = 0; i < USB_LED_S; ++i)
            g_fb[(USB_LED_Y + j) * 128 + (USB_LED_X + i)] = led_col;

    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

/* --- system selector grid ---------------------------------------- *
 *
 * 2x2 grid of 48x48 system icons centred horizontally. Grid area
 * runs y=12..116 (the last tile's bottom row). The top 10 px hold
 * the "ThumbyOne" title + USB indicator strip; the USB indicator
 * LED mirrors the on-screen dot to the device's physical RGB LED.
 * Tiles are at:
 *
 *    (12, 12)   (68, 12)   — NES, P8
 *    (12, 68)   (68, 68)   — DOOM, MPY
 *
 * Selected tile gets a 2 px yellow border. D-pad moves, A launches.
 */

/* Slot order in the grid must match the lobby_icons[] indexing
 * emitted by pack_icons.py (NES, P8, DOOM, MPY). */
static const thumbyone_slot_t g_grid_slot_order[4] = {
    THUMBYONE_SLOT_NES,
    THUMBYONE_SLOT_P8,
    THUMBYONE_SLOT_DOOM,
    THUMBYONE_SLOT_MPY,
};

/* Per-slot "is this slot actually compiled into this build" flags,
 * fed from the top-level CMake. Disabled tiles are drawn greyed
 * out and skipped over during D-pad navigation. Defaults to 1 so
 * an in-tree experiment that doesn't pass the definitions still
 * behaves like the full build. */
#ifndef THUMBYONE_LOBBY_HAS_NES
#define THUMBYONE_LOBBY_HAS_NES 1
#endif
#ifndef THUMBYONE_LOBBY_HAS_P8
#define THUMBYONE_LOBBY_HAS_P8 1
#endif
#ifndef THUMBYONE_LOBBY_HAS_DOOM
#define THUMBYONE_LOBBY_HAS_DOOM 1
#endif
#ifndef THUMBYONE_LOBBY_HAS_MPY
#define THUMBYONE_LOBBY_HAS_MPY 1
#endif

static const bool g_grid_slot_present[4] = {
    THUMBYONE_LOBBY_HAS_NES,
    THUMBYONE_LOBBY_HAS_P8,
    THUMBYONE_LOBBY_HAS_DOOM,
    THUMBYONE_LOBBY_HAS_MPY,
};

/* Pick the first enabled slot for the initial cursor position. If
 * none are enabled the build would be empty anyway; fall back to 0
 * so the lobby still renders something.  */
static int first_enabled_slot(void) {
    for (int i = 0; i < 4; ++i) if (g_grid_slot_present[i]) return i;
    return 0;
}

#define GRID_TILE_SIZE  48
#define GRID_ORIGIN_X   12
#define GRID_ORIGIN_Y   12
#define GRID_GUTTER      8

/* Start the cursor on the first enabled slot so the initial render
 * doesn't highlight a greyed-out tile. If all slots are disabled,
 * stays at 0 — the build is essentially broken in that case but we
 * don't want to crash at boot. */
static int g_grid_cursor = 0;   /* 0..3 — overwritten in main()  */

static void grid_tile_origin(int idx, int *ox, int *oy) {
    int col = idx & 1;
    int row = idx >> 1;
    *ox = GRID_ORIGIN_X + col * (GRID_TILE_SIZE + GRID_GUTTER);
    *oy = GRID_ORIGIN_Y + row * (GRID_TILE_SIZE + GRID_GUTTER);
}

/* Darken the 48x48 region at (x, y) in place by `shift` bits per
 * channel:
 *    shift=1 → 1/2 brightness (non-selected tiles dim back)
 *    shift=2 → 1/4 brightness (disabled slots — "present but
 *              unavailable" read, almost fully greyed)
 * The per-channel right-shift keeps hue stable — the icon still
 * reads as itself, just quieter. */
static void dim_tile(int x, int y, int shift) {
    for (int j = 0; j < GRID_TILE_SIZE; ++j) {
        int yy = y + j;
        if ((unsigned)yy >= 128) continue;
        for (int i = 0; i < GRID_TILE_SIZE; ++i) {
            int xx = x + i;
            if ((unsigned)xx >= 128) continue;
            uint16_t p = g_fb[yy * 128 + xx];
            uint32_t r = (p >> 11) & 0x1F;
            uint32_t g = (p >>  5) & 0x3F;
            uint32_t b = (p      ) & 0x1F;
            r >>= shift; g >>= shift; b >>= shift;
            g_fb[yy * 128 + xx] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

/* System labels — shown at the bottom of the screen under the
 * selected tile. Order matches the grid layout + lobby_icons[] /
 * g_grid_slot_order[]. */
static const char *const g_grid_labels[4] = {
    "NES / SMS / GG / GB",
    "PICO-8",
    "DOOM",
    "MICROPYTHON",
};

static void render_home(void) {
    for (int i = 0; i < 128 * 128; ++i) g_fb[i] = COL_BG;

    /* Slim title strip — keeps vertical room for the grid + label. */
    int w = nes_font_width("ThumbyOne");
    nes_font_draw(g_fb, "ThumbyOne", 2, 2, COL_TITLE);
    (void)w;

    /* Grid tiles. Each tile is drawn at full brightness first, then
     * dimmed in place as appropriate:
     *   - slot not present in this build → 1/4 brightness
     *   - slot present but not under the cursor → 1/2 brightness
     *   - cursor tile → full brightness (no dim applied)
     * Selection is conveyed by brightness alone — no border box. */
    for (int i = 0; i < 4 && i < (int)lobby_icons_count; ++i) {
        int ox, oy;
        grid_tile_origin(i, &ox, &oy);
        lobby_icon_draw(g_fb, &lobby_icons[i], ox, oy);
        if (!g_grid_slot_present[i]) {
            dim_tile(ox, oy, 2);        /* hard dim — disabled */
        } else if (i != g_grid_cursor) {
            dim_tile(ox, oy, 1);        /* gentle dim — not selected */
        }
    }

    /* System label under the grid. Centred, bright white to echo the
     * "activated" feel of the undimmed selected tile. */
    {
        const char *label = g_grid_labels[g_grid_cursor];
        uint16_t col = g_grid_slot_present[g_grid_cursor]
                         ? COL_TEXT : COL_USB_OFF;   /* dim grey for disabled */
        int lw = nes_font_width(label);
        nes_font_draw(g_fb, label, (128 - lw) / 2, 119, col);
    }

    /* Paint the USB indicator into the top-right strip using the
     * current state. This does NOT re-sample or re-drive the LED —
     * those are owned by the main loop's USB state machine, which
     * only updates on a real state change. */
    {
        usb_row_state_t st = g_usb_row_state;
        for (int y = 0; y < 11; ++y)
            for (int x = 100; x < 128; ++x)
                g_fb[y * 128 + x] = COL_BG;
        uint16_t indicator =
            st == USB_ROW_ACTIVE ? COL_USB_BUSY :
            st == USB_ROW_READY  ? COL_USB_ON   :
                                   COL_USB_OFF;
        nes_font_draw(g_fb, "USB", USB_LABEL_X, 2, indicator);
        for (int j = 0; j < USB_LED_S; ++j)
            for (int i = 0; i < USB_LED_S; ++i)
                g_fb[(USB_LED_Y + j) * 128 + (USB_LED_X + i)] = indicator;
    }

    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

static void redraw_home_if_cursor_moved(int prev_cursor) {
    if (prev_cursor == g_grid_cursor) return;
    render_home();
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
    gpio_init(PIN_BTN_UP);    gpio_set_dir(PIN_BTN_UP, GPIO_IN);    gpio_pull_up(PIN_BTN_UP);
    gpio_init(PIN_BTN_DOWN);  gpio_set_dir(PIN_BTN_DOWN, GPIO_IN);  gpio_pull_up(PIN_BTN_DOWN);
    gpio_init(PIN_BTN_LEFT);  gpio_set_dir(PIN_BTN_LEFT, GPIO_IN);  gpio_pull_up(PIN_BTN_LEFT);
    gpio_init(PIN_BTN_RIGHT); gpio_set_dir(PIN_BTN_RIGHT, GPIO_IN); gpio_pull_up(PIN_BTN_RIGHT);

    nes_lcd_init();
    nes_lcd_backlight(1);
    led_setup();

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

    g_grid_cursor = first_enabled_slot();
    render_home();

    /* Slot-launch intent: set by a button press, consumed after the
     * button is released AND USB has been quiet for a moment. This
     * avoids yanking a slot out mid-transfer when the user fumbles
     * the D-pad while Windows is still copying a file. */
    int pending_slot = -1;   /* -1 = none */
    uint64_t pending_since_us = 0;

    /* Small helper to pump USB + refresh the UI + drive the write-
     * back cache drain. Called from the tight button-wait loops too
     * so we never stall USB.
     *
     * The drain fires when MSC has been quiet for >300 ms AND the
     * cache holds dirty data. That interval is long enough that the
     * host's inter-command gap won't cause a spurious drain (then
     * stall the next command), but short enough that an unplug or
     * sudden power loss won't strand much data. Each drain commits
     * the entire 4 KB block in one shot (~70 ms with IRQs off per
     * erase + page programs); there's at most one dirty block in
     * flight, so one drain fully empties the cache. */
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
        if (lobby_usb_cache_dirty()) {                                   \
            uint64_t last = lobby_usb_last_op_us();                      \
            uint64_t now_us = (uint64_t)time_us_64();                    \
            if (last == 0 || (now_us - last) > 300000) {                 \
                lobby_usb_drain();                                       \
            }                                                            \
        }                                                                \
    } while (0)

    /* Edge-detection state for the D-pad. We only act on a
     * transition from not-pressed → pressed so holding a direction
     * doesn't sweep the cursor uncontrollably across tiles. */
    bool prev_up = false, prev_down = false;
    bool prev_left = false, prev_right = false;

    while (1) {
        USB_PUMP();

        int prev_cursor = g_grid_cursor;

        /* D-pad navigation. UP/DOWN flip the row (cursor ^= 2);
         * LEFT/RIGHT flip the column (cursor ^= 1). With four tiles
         * any axis press always moves to a valid cell — but if the
         * landed cell is a disabled slot, toggle the OTHER axis too
         * so the cursor lands on a present tile. That effectively
         * routes through to the diagonally-opposite cell, which is
         * the "next" enabled tile in the build-flag matrices we
         * actually ship (usually at least three of four are ON). */
        bool now_up    = btn_up_pressed();
        bool now_down  = btn_down_pressed();
        bool now_left  = btn_left_pressed();
        bool now_right = btn_right_pressed();

        int  axis_mask = 0;
        if (now_up    && !prev_up)    axis_mask ^= 2;
        if (now_down  && !prev_down)  axis_mask ^= 2;
        if (now_left  && !prev_left)  axis_mask ^= 1;
        if (now_right && !prev_right) axis_mask ^= 1;
        if (axis_mask) {
            g_grid_cursor ^= axis_mask;
            if (!g_grid_slot_present[g_grid_cursor]) {
                g_grid_cursor ^= (axis_mask ^ 3);   /* flip the other axis */
            }
            if (!g_grid_slot_present[g_grid_cursor]) {
                g_grid_cursor = first_enabled_slot();
            }
        }

        prev_up    = now_up;
        prev_down  = now_down;
        prev_left  = now_left;
        prev_right = now_right;

        redraw_home_if_cursor_moved(prev_cursor);

        /* Debounce: record intent on press, but defer the actual
         * handoff until the button is released AND no MSC activity
         * has happened in the last 500 ms. A launches the cursor
         * tile; MENU reboots the lobby. */
        if (pending_slot < 0) {
            if (btn_a_pressed() && g_grid_slot_present[g_grid_cursor]) {
                pending_slot = (int)g_grid_slot_order[g_grid_cursor];
            } else if (btn_menu_pressed()) {
                pending_slot = -2;   /* reboot lobby */
            }
            if (pending_slot != -1) {
                pending_since_us = (uint64_t)time_us_64();
            }
        }

        if (pending_slot != -1) {
            /* Wait for release. Pump USB while we wait. */
            bool still_held =
                (pending_slot == -2 && btn_menu_pressed()) ||
                (pending_slot != -2 && btn_a_pressed());
            if (!still_held) {
                /* Wait for USB to go quiet (500 ms since last op) so
                 * any in-flight host write has completed before we
                 * hand the FAT to a slot. ALSO flush the write-back
                 * cache: the drain in USB_PUMP will normally have
                 * done this already, but a race where the button
                 * press arrives inside the 300 ms quiet window
                 * before the drain fires would otherwise strand
                 * dirty data. The explicit flush here is the safety
                 * net. */
                uint64_t last = lobby_usb_last_op_us();
                uint64_t now  = (uint64_t)time_us_64();
                bool quiet = (last == 0) || ((now - last) > 500000);
                /* Also require at least 50 ms since the press itself
                 * to debounce finger-bounce. */
                bool stable = (now - pending_since_us) > 50000;
                if (quiet && stable) {
                    if (lobby_usb_cache_dirty()) lobby_usb_drain();
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
