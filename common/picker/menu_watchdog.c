/*
 * ThumbyOne — MENU-long-hold return-to-lobby watchdog.
 *
 * While an MPY game is running, a pico-sdk repeating timer polls
 * GPIO 26 (MENU) at 50 Hz. When MENU has been held for 5 s we
 * reboot straight to the lobby — no overlay, no splash.
 *
 * Rationale: the overlay used to draw a 128×128 "returning to
 * picker..." frame, which meant carrying a permanent 32 KB
 * framebuffer in BSS (`g_ovl_fb`) plus all the LCD-acquire /
 * release plumbing to steal the panel from the running engine.
 * That 32 KB noticeably cut into the MicroPython GC heap — games
 * with tight memory budgets (e.g. Thumbalaga) hit MemoryError on
 * import under ThumbyOne but loaded fine under stock firmware.
 * The 5 s hold is itself deliberate enough to not need visual
 * confirmation; the reboot is immediate and obvious.
 */
#include "menu_watchdog.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#include "thumbyone_handoff.h"

#define MENU_PIN          26
#define POLL_PERIOD_MS    20
#define HOLD_MS         5000       /* 5 s — deliberate reboot gesture */
#define HOLD_TICKS      (HOLD_MS / POLL_PERIOD_MS)

static repeating_timer_t g_timer;
static volatile int      g_hold_ticks = 0;
static bool              g_installed  = false;

static inline bool btn(uint pin) { return !gpio_get(pin); }

static bool menu_watchdog_cb(repeating_timer_t *t) {
    (void)t;
    if (btn(MENU_PIN)) {
        g_hold_ticks++;
        if (g_hold_ticks >= HOLD_TICKS) {
            /* 5 s hold satisfied — hand off. Does not return. */
            thumbyone_handoff_request_lobby();
        }
    } else {
        g_hold_ticks = 0;
    }
    return true;
}

void thumbyone_menu_watchdog_install(void) {
    if (g_installed) return;
    g_installed = true;
    add_repeating_timer_ms(POLL_PERIOD_MS, menu_watchdog_cb, NULL, &g_timer);
}
