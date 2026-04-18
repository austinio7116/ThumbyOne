/*
 * ThumbyOne — MENU-long-hold → return-to-lobby watchdog.
 *
 * Implemented as a 100 Hz pico-sdk repeating timer so it fires from
 * an IRQ irrespective of what the foreground (MicroPython + game)
 * is doing. Hold MENU for 2 s and the callback triggers
 * thumbyone_handoff_request_lobby() — the same reboot path the
 * picker uses for its own long-hold escape.
 *
 * We deliberately DON'T filter for "held continuously since install"
 * — users might be holding MENU at the moment the game starts (that
 * was how they got from picker to game after all) so the counter
 * only starts counting from the first ACTIVE poll. We also reset
 * the counter on any release so a transient glitch doesn't
 * accumulate.
 */
#include "menu_watchdog.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "thumbyone_handoff.h"

#define MENU_PIN         26
#define POLL_PERIOD_MS  100
#define HOLD_MS        2000
#define HOLD_TICKS     (HOLD_MS / POLL_PERIOD_MS)

static repeating_timer_t g_timer;
static volatile int      g_hold_ticks = 0;
static bool              g_installed  = false;

static bool menu_watchdog_cb(repeating_timer_t *t) {
    (void)t;
    /* Active-low pin (button is shorted to GND when pressed, pulled
     * up by the internal pull resistor when released). */
    bool pressed = !gpio_get(MENU_PIN);
    if (pressed) {
        g_hold_ticks++;
        if (g_hold_ticks >= HOLD_TICKS) {
            /* thumbyone_handoff_request_lobby() writes the watchdog
             * scratch and triggers a reset — safe to call from an
             * IRQ, does not return. */
            thumbyone_handoff_request_lobby();
        }
    } else {
        g_hold_ticks = 0;
    }
    return true;   /* keep ticking */
}

void thumbyone_menu_watchdog_install(void) {
    if (g_installed) return;
    g_installed = true;

    /* MENU pin is already configured as input+pull-up by the C
     * picker's buttons_init — we just piggyback on its GPIO setup.
     * If anything later reconfigures the pin to a non-SIO function
     * (the engine's io module does re-init it as SIO input with
     * pull-up too, so it's harmless), the read will still work as
     * long as the pin stays an input. */
    add_repeating_timer_ms(POLL_PERIOD_MS, menu_watchdog_cb, NULL, &g_timer);
}
