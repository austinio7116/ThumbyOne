/*
 * ThumbyOne — MENU-long-hold overlay + return-to-lobby watchdog.
 *
 * While an MPY game is running, a pico-sdk repeating timer polls
 * GPIO 26 (MENU) at 50 Hz. When MENU has been held for ~500 ms, the
 * timer callback STEALS the LCD via the common nes_lcd_gc9107
 * driver (clobbering whatever the engine is pushing) and draws a
 * small overlay with battery, firmware, and two choices:
 *
 *      > Back to lobby   (A)
 *        Resume game     (B or release MENU)
 *
 * All of this runs inside the IRQ — which is unusual but works on
 * Cortex-M33: the game's foreground loop is suspended while the
 * ISR is running, and the LCD driver's DMA completion IRQ can nest.
 *
 * On "Back to lobby": thumbyone_handoff_request_lobby() fires and
 * the chip reboots.
 *
 * On "Resume game": the LCD driver is torn down (releasing its DMA
 * channel and deinit-ing SPI0). The engine's next display.send()
 * re-initialises SPI/DMA from its own side and starts pushing
 * frames again — there's a brief glitch as the first post-overlay
 * frame fights the torn-down state, but the engine recovers on the
 * next tick. For a hold-MENU-to-exit flow that the user can always
 * cancel, that one frame of glitch is a reasonable trade.
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

typedef enum {
    OVL_ITEM_LOBBY  = 0,
    OVL_ITEM_RESUME,
    OVL_ITEM_COUNT,
} ovl_item_t;

static void draw_overlay(int cursor) {
    fb_fill(COL_BG);

    /* Orange title bar + underline. */
    nes_font_draw(g_ovl_fb, "MENU", 2, 2, COL_TITLE);
    fb_hline(0, 10, 128, COL_TITLE);

    /* Battery info. */
    int y = 14;
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
        y += 10;
    }

    /* Firmware. */
    {
        nes_font_draw(g_ovl_fb, "fw", 4, y, COL_DIM);
        const char *fw = "ONE " THUMBYONE_FW_VERSION;
        int vw = nes_font_width(fw);
        nes_font_draw(g_ovl_fb, fw, 128 - vw - 4, y, COL_FG);
        y += 10;
    }

    /* Divider before actions. */
    fb_hline(8, y, 112, COL_DIM);
    y += 4;

    /* Action rows. */
    const char *labels[OVL_ITEM_COUNT] = {
        [OVL_ITEM_LOBBY]  = "back to lobby",
        [OVL_ITEM_RESUME] = "resume game",
    };
    for (int i = 0; i < OVL_ITEM_COUNT; ++i) {
        bool here = (i == cursor);
        if (here) fb_rect(0, y - 1, 128, 10, COL_HL_BG);
        uint16_t fg = here ? COL_HIGHLT : COL_FG;
        if (here) nes_font_draw(g_ovl_fb, ">", 2, y, fg);
        nes_font_draw(g_ovl_fb, labels[i], 10, y, fg);
        y += 10;
    }

    /* Footer hint. */
    fb_hline(0, 120, 128, COL_TITLE);
    const char *hint =
        (cursor == OVL_ITEM_LOBBY)  ? "A: go to lobby" :
        (cursor == OVL_ITEM_RESUME) ? "A: resume"      :
                                      "A: select";
    int hw = nes_font_width(hint);
    nes_font_draw(g_ovl_fb, hint, (128 - hw) / 2, 122, COL_DIM);

    nes_lcd_present(g_ovl_fb);
    nes_lcd_wait_idle();
}

/* --- overlay input loop ------------------------------------------ */

static inline bool btn(uint pin) { return !gpio_get(pin); }

/* Run the overlay synchronously. Returns when the user cancels
 * (B / MENU release) — or never returns, if they picked "Back to
 * lobby" (the handoff reboots the chip). */
static void overlay_run(void) {
    g_overlay_active = true;

    /* Steal the LCD — engine's current DMA/SPI state gets clobbered
     * by nes_lcd_init, but we'll teardown at the end and the engine's
     * next present() will re-establish its own state. */
    nes_lcd_init();
    nes_lcd_backlight(1);

    int cursor = OVL_ITEM_LOBBY;
    /* The menu opened because MENU was HELD, so initialise prev_menu
     * to 'pressed' and wait for the actual release before reading
     * any other prev_. This avoids auto-closing on the MENU release
     * that the user hasn't issued yet. */
    bool prev_a = false;
    bool prev_b = false;
    bool prev_up = false;
    bool prev_down = false;

    draw_overlay(cursor);

    /* Wait for the opening MENU hold to end so we don't mistake
     * that release for a "cancel overlay" gesture. */
    while (btn(MENU_PIN)) busy_wait_ms(10);

    uint32_t tick = 0;
    while (1) {
        bool a = btn(A_PIN);
        bool b = btn(B_PIN);
        bool u = btn(UP_PIN);
        bool d = btn(DOWN_PIN);
        bool m = btn(MENU_PIN);

        bool dirty = false;
        if (u && !prev_up && cursor > 0)                 { --cursor; dirty = true; }
        if (d && !prev_down && cursor < OVL_ITEM_COUNT-1){ ++cursor; dirty = true; }

        if (a && !prev_a) {
            if (cursor == OVL_ITEM_LOBBY) {
                nes_lcd_wait_idle();
                thumbyone_handoff_request_lobby();
                /* does not return */
            } else {
                break;   /* resume */
            }
        }
        /* Only B or a fresh MENU press cancels (MENU toggles the
         * overlay closed). Ignore MENU release — the opening hold
         * is over by now. */
        if (b && !prev_b) break;
        if (m && !prev_up) {
            /* reserved: fresh MENU-press while overlay is up —
             * same as B. */
        }

        prev_a = a; prev_b = b; prev_up = u; prev_down = d;
        (void)m;

        /* Tiny "loop alive" heartbeat in the top-right of the title
         * bar — cycles through 4 pixel positions so we can visually
         * tell if the loop is running even when no input has been
         * detected. If it's animating but nothing else happens,
         * we've proved the freeze is in input detection rather than
         * the loop itself. */
        tick++;
        const uint16_t blip_on  = 0x07E0;
        const uint16_t blip_off = 0x0000;
        for (int k = 0; k < 4; ++k) {
            bool on = (k == (int)(tick & 3));
            g_ovl_fb[2 * 128 + (120 + k)] = on ? blip_on : blip_off;
        }

        if (dirty) {
            draw_overlay(cursor);
        } else {
            /* Push just enough fb to show the tick. */
            nes_lcd_present(g_ovl_fb);
            nes_lcd_wait_idle();
        }
        busy_wait_ms(50);
    }

    /* Resume path: hand the LCD back to the engine. Engine's next
     * present() will set its own SPI format + window and push
     * frames using its own DMA channel. We must NOT reset the
     * SPI0 or DMA peripheral blocks here — that would wipe the
     * engine's claimed audio + display DMA channels and cause
     * the engine to hang on its next tick. */
    nes_lcd_release();

    /* Drain any still-held buttons so the game doesn't see the press
     * that closed the overlay. */
    while (btn(A_PIN) || btn(B_PIN) || btn(MENU_PIN)) busy_wait_ms(10);

    g_overlay_active = false;
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
