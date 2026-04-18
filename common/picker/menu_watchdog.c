/*
 * ThumbyOne — MENU-long-hold return-to-lobby watchdog.
 *
 * While an MPY game is running, a pico-sdk repeating timer polls
 * GPIO 26 (MENU) at 50 Hz. When MENU has been held for ~500 ms, the
 * timer callback STEALS the LCD via the common nes_lcd_gc9107
 * driver, flashes a "returning to picker..." overlay with battery
 * and firmware info for ~700 ms, then reboots to the lobby.
 *
 * The overlay used to offer a "resume game" path but handing the
 * LCD back to the already-running engine from IRQ context is
 * flaky — the engine's DMA + SPI state can't be cleanly reconciled.
 * The resume path has been removed until that can be solved; for
 * now MENU-hold is a one-shot back-to-lobby gesture.
 *
 * Everything runs inside the IRQ — which is unusual but works on
 * Cortex-M33 since overlay_run() never returns (it reboots).
 */
#include "menu_watchdog.h"

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

#include "lcd_gc9107.h"
#include "font.h"
#include "thumbyone_handoff.h"

/* --- pin map (matches common/picker/picker.c) --------------------- */
#define MENU_PIN  26
#define A_PIN     21
#define B_PIN     25
#define UP_PIN     1
#define DOWN_PIN   3
#define LEFT_PIN   0
#define RIGHT_PIN  2

#define BATT_GPIO   29
#define BATT_ADC    3

#define POLL_PERIOD_MS   20
#define HOLD_MS         500
#define HOLD_TICKS      (HOLD_MS / POLL_PERIOD_MS)

/* --- palette (matches lobby / picker menus) ----------------------- */
#define COL_BG        0x0000
#define COL_FG        0xFFFF
#define COL_DIM       0x8410
#define COL_DARK      0x4208
#define COL_TITLE     0xFD20   /* orange   */
#define COL_HIGHLT    0x07E0   /* green    */
#define COL_HL_BG     0x0220
#define COL_BAR_BG    0x39E7
#define COL_ACCENT    0xFFE0

/* --- build-time firmware tag ------------------------------------- */
#ifndef THUMBYONE_FW_VERSION
#define THUMBYONE_FW_VERSION "dev"
#endif

/* --- state ------------------------------------------------------- */

static repeating_timer_t g_timer;
static volatile int      g_hold_ticks = 0;
static bool              g_installed  = false;
static bool              g_overlay_active = false;

/* Framebuffer for the overlay. 32 KB of BSS — the MPY slot has
 * plenty; the engine's own framebuffers are already larger. */
static uint16_t g_ovl_fb[128 * 128] __attribute__((aligned(4)));

/* --- battery helpers (duplicated from picker.c so the overlay
 *     doesn't depend on picker.c's private statics) -------------- */
static bool g_adc_ready = false;
static void ovl_battery_init(void) {
    if (g_adc_ready) return;
    adc_init();
    adc_gpio_init(BATT_GPIO);
    g_adc_ready = true;
}
static float ovl_battery_half_voltage(void) {
    ovl_battery_init();
    adc_select_input(BATT_ADC);
    (void)adc_read();  /* RP2350 first-sample-after-switch quirk */
    uint32_t sum = 0;
    for (int i = 0; i < 8; ++i) sum += adc_read();
    return ((float)sum / 8.0f) * 3.3f / 4095.0f;
}
static int ovl_battery_percent(void) {
    float h = ovl_battery_half_voltage();
    const float lo = 1.45f, hi = 1.85f;   /* ~2.9 V..3.7 V at the pack */
    if (h <= lo) return 0;
    if (h >= hi) return 100;
    int p = (int)((h - lo) / (hi - lo) * 100.0f + 0.5f);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}
static float ovl_battery_voltage(void) { return 2.0f * ovl_battery_half_voltage(); }
static bool  ovl_battery_charging(void) { return ovl_battery_half_voltage() >= 1.85f; }

/* --- drawing helpers --------------------------------------------- */

static void fb_fill(uint16_t c) {
    for (int i = 0; i < 128 * 128; ++i) g_ovl_fb[i] = c;
}
static void fb_hline(int x, int y, int w, uint16_t c) {
    if ((unsigned)y >= 128) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > 128) w = 128 - x;
    for (int i = 0; i < w; ++i) g_ovl_fb[y * 128 + x + i] = c;
}
static void fb_rect(int x, int y, int w, int h, uint16_t c) {
    for (int j = 0; j < h; ++j)
        if ((unsigned)(y + j) < 128)
            fb_hline(x, y + j, w, c);
}

/* --- overlay render ---------------------------------------------- */

static inline bool btn(uint pin) { return !gpio_get(pin); }

/* Resuming the game after the overlay runs is currently broken —
 * the engine's display DMA + SPI state can't be handed back cleanly
 * from IRQ context. Until that's solved, the overlay is "back to
 * lobby" one-shot only: flash a confirmation and reboot. */

static void draw_handoff_overlay(void) {
    fb_fill(COL_BG);

    /* Orange title bar + underline. */
    nes_font_draw(g_ovl_fb, "MENU", 2, 2, COL_TITLE);
    fb_hline(0, 10, 128, COL_TITLE);

    int y = 16;

    /* Battery row. */
    {
        int pct = ovl_battery_percent();
        float v = ovl_battery_voltage();
        bool chg = ovl_battery_charging();
        char val[20];
        int vmv = (int)(v * 100.0f + 0.5f);
        if (chg) snprintf(val, sizeof(val), "CHRG %d.%02dV", vmv / 100, vmv % 100);
        else     snprintf(val, sizeof(val), "%d%% %d.%02dV", pct, vmv / 100, vmv % 100);
        uint16_t col = chg ? COL_HIGHLT : (pct < 15 ? 0xF800 : COL_FG);
        nes_font_draw(g_ovl_fb, "batt", 4, y, COL_DIM);
        int vw = nes_font_width(val);
        nes_font_draw(g_ovl_fb, val, 128 - vw - 4, y, col);
        fb_rect(8, y + 7, 112, 2, COL_BAR_BG);
        int fill = (112 * (pct < 0 ? 0 : (pct > 100 ? 100 : pct))) / 100;
        fb_rect(8, y + 7, fill, 2, COL_HIGHLT);
        y += 12;
    }

    /* Firmware row. */
    {
        nes_font_draw(g_ovl_fb, "fw", 4, y, COL_DIM);
        const char *fw = "ONE " THUMBYONE_FW_VERSION;
        int vw = nes_font_width(fw);
        nes_font_draw(g_ovl_fb, fw, 128 - vw - 4, y, COL_FG);
        y += 14;
    }

    /* Main message, centred. */
    fb_hline(8, y, 112, COL_DIM);
    y += 10;

    const char *msg1 = "returning to";
    const char *msg2 = "picker...";
    int w1 = nes_font_width(msg1);
    int w2 = nes_font_width(msg2);
    nes_font_draw(g_ovl_fb, msg1, (128 - w1) / 2, y,     COL_HIGHLT);
    nes_font_draw(g_ovl_fb, msg2, (128 - w2) / 2, y + 10, COL_HIGHLT);

    /* Footer bar. */
    fb_hline(0, 120, 128, COL_TITLE);

    nes_lcd_present(g_ovl_fb);
    nes_lcd_wait_idle();
}

/* Run the overlay then reboot to the lobby. Does not return. */
static void overlay_run(void) {
    g_overlay_active = true;

    /* Take over the LCD without disturbing engine state — SPI0 is
     * already alive, panel is already initialised, backlight is
     * already running on PIO-PWM. We just need a DMA channel of
     * our own to push pixels. */
    nes_lcd_acquire();

    draw_handoff_overlay();

    /* Give the user ~700ms to register the confirmation before
     * we reboot. Using busy_wait so we remain safe inside IRQ
     * context. */
    for (int i = 0; i < 70; ++i) busy_wait_ms(10);

    nes_lcd_wait_idle();
    thumbyone_handoff_request_lobby();
    /* does not return */
    while (1) tight_loop_contents();
}

/* --- timer callback ---------------------------------------------- */

static bool menu_watchdog_cb(repeating_timer_t *t) {
    /* Don't re-enter the overlay while it's already up (paranoid —
     * we clear the timer before calling overlay_run, but IRQs can
     * nest). */
    if (g_overlay_active) return true;

    bool pressed = btn(MENU_PIN);
    if (pressed) {
        g_hold_ticks++;
        if (g_hold_ticks >= HOLD_TICKS) {
            g_hold_ticks = 0;
            /* Stop the repeating timer before the overlay runs —
             * overlay_run blocks for an arbitrarily long time and
             * we don't want another fire interrupting it. */
            cancel_repeating_timer(t);
            overlay_run();
            /* Re-arm the watchdog for the next MENU hold. */
            add_repeating_timer_ms(POLL_PERIOD_MS, menu_watchdog_cb,
                                   NULL, t);
            return false;   /* old timer cancelled */
        }
    } else {
        g_hold_ticks = 0;
    }
    return true;
}

/* --- public install ---------------------------------------------- */

void thumbyone_menu_watchdog_install(void) {
    if (g_installed) return;
    g_installed = true;
    add_repeating_timer_ms(POLL_PERIOD_MS, menu_watchdog_cb, NULL, &g_timer);
}
