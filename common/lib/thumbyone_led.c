/*
 * ThumbyOne — shared front-RGB-indicator driver.
 *
 * See thumbyone_led.h for the policy. Pin map and PWM setup mirror
 * the lobby's earlier open-coded version exactly so moving between
 * lobby and a slot (where the picker also drives the LED) produces
 * no visible brightness step.
 */
#include "thumbyone_led.h"

#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "thumbyone_backlight.h"
#include "thumbyone_settings.h"

#define PIN_LED_G   10   /* PWM5 A */
#define PIN_LED_R   11   /* PWM5 B */
#define PIN_LED_B   12   /* PWM6 A */
#define LED_PWM_WRAP 2048

/* LED brightness curve: at slider=0 the LED runs at LED_MIN_DUTY
 * (just-visible), and at slider=255 at LED_MAX_DUTY (the nominal
 * "brightest" — capped below 100 % because raw full duty is painful
 * in a dark room). Intermediate slider values linearly interpolate.
 * Channel intensity (0..255 per R/G/B) then scales the peak.
 *
 * Earlier revisions used `channel × slider × MAX_PCT × WRAP /
 * denom` (i.e. slider scales from 0 → MAX) with a floor. That left
 * mid-slider values uncomfortably dim and wasted the low-slider
 * range on "floored but identical" territory. Linear MIN..MAX gives
 * every slider position a distinct, useful brightness.
 *
 * Keep these values in sync with lobby_main.c's open-coded copy
 * and engine_io_rp3.c. */
#define LED_MIN_DUTY    400    /* slider=0,   channel=255  → ~19.5 % */
#define LED_MAX_DUTY   1638    /* slider=255, channel=255  → ~80 %   */

/* Claim the pin as PWM, with a full per-pin pwm_init (NOT just
 * pwm_set_wrap). Matches the lobby's led_pwm_setup_pin pattern
 * exactly — the lobby has a comment (lobby_main.c:288) noting that
 * on the RP2350 a per-slice pwm_set_wrap sometimes leaves the
 * channel config in a state where one PWM channel (slice 5A =
 * green in the lobby's case) silently fails to output. Re-issuing
 * pwm_init with a fresh config per pin is the pattern that
 * reliably lands the hardware configuration.
 *
 * Called on every set_rgb rather than once at startup, because
 * sibling drivers in the same slot may repurpose the pins behind
 * our back — e.g. NES defrag reconfigures GPIO 11 as a bare GPIO
 * for a brief red-on/red-off status. The reclaim then hands the
 * pin back to PWM with our WRAP before the next LED paint. Cost:
 * a few MMIO writes plus a one-cycle CC reset (inaudibly brief at
 * the 73 kHz PWM rate). */
static void reclaim_pwm(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, LED_PWM_WRAP);
    pwm_init(slice, &cfg, true);
}

void thumbyone_led_init(void) {
    reclaim_pwm(PIN_LED_R);
    reclaim_pwm(PIN_LED_G);
    reclaim_pwm(PIN_LED_B);
    pwm_set_gpio_level(PIN_LED_R, LED_PWM_WRAP);
    pwm_set_gpio_level(PIN_LED_G, LED_PWM_WRAP);
    pwm_set_gpio_level(PIN_LED_B, LED_PWM_WRAP);
}

/* Scale one channel (0..255) to the PWM off-time (common-anode →
 * more off-time = less bright; off-time = WRAP - on-time).
 *
 *   peak_on = MIN_DUTY + (MAX_DUTY - MIN_DUTY) × slider / 255
 *   on_time = peak_on × channel / 255         (scale by channel)
 *
 * channel == 0 is a hard-off: the whole RGB request can ask a channel
 * to be totally dark (e.g. pure green wants R=0, B=0). The linear
 * interpolation above would give peak_on × 0 = 0 anyway, but we
 * short-circuit the branch to make that explicit. */
static inline uint16_t channel_offtime(uint8_t channel, uint8_t slider) {
    if (channel == 0) return LED_PWM_WRAP;   /* fully off */
    uint32_t peak_on = LED_MIN_DUTY
                    + ((uint32_t)(LED_MAX_DUTY - LED_MIN_DUTY) * slider) / 255u;
    uint32_t on_time = (peak_on * channel) / 255u;
    if (on_time > LED_PWM_WRAP) on_time = LED_PWM_WRAP;
    return (uint16_t)(LED_PWM_WRAP - on_time);
}

/* Last colour applied via set_rgb. Kept so thumbyone_led_refresh()
 * can replay at the current brightness after a slider commit
 * without the caller having to remember what it last set. s_have_last
 * stays false until the first set_rgb — refresh-before-set is a no-op
 * so slots that never touch the LED don't steal it away from whatever
 * the picker or boot sequence left on it. */
static uint8_t s_last_r, s_last_g, s_last_b;
static bool    s_have_last;

void thumbyone_led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    s_last_r = r; s_last_g = g; s_last_b = b;
    s_have_last = true;
    reclaim_pwm(PIN_LED_R);
    reclaim_pwm(PIN_LED_G);
    reclaim_pwm(PIN_LED_B);
    uint8_t slider = thumbyone_backlight_get();
    pwm_set_gpio_level(PIN_LED_R, channel_offtime(r, slider));
    pwm_set_gpio_level(PIN_LED_G, channel_offtime(g, slider));
    pwm_set_gpio_level(PIN_LED_B, channel_offtime(b, slider));
}

void thumbyone_led_off(void) {
    reclaim_pwm(PIN_LED_R);
    reclaim_pwm(PIN_LED_G);
    reclaim_pwm(PIN_LED_B);
    pwm_set_gpio_level(PIN_LED_R, LED_PWM_WRAP);
    pwm_set_gpio_level(PIN_LED_G, LED_PWM_WRAP);
    pwm_set_gpio_level(PIN_LED_B, LED_PWM_WRAP);
}

void thumbyone_led_refresh(void) {
    if (!s_have_last) return;
    thumbyone_led_set_rgb(s_last_r, s_last_g, s_last_b);
}

void thumbyone_slot_init_brightness_and_led(bool drive_backlight) {
    uint8_t bri = thumbyone_settings_load_brightness();
    if (drive_backlight) {
        /* Slot owns GP7 directly (NES, P8, DOOM). backlight_set
         * configures the PWM slice and drives the duty cycle. */
        thumbyone_backlight_set(bri);
    } else {
        /* Another driver owns GP7 (MPY slot — engine's PIO PWM
         * program on the same pin). Just keep backlight_get() in
         * sync so the LED scaling math reads the right slider
         * value without us double-driving the pin. */
        thumbyone_backlight_track(bri);
    }
    thumbyone_led_init();
    thumbyone_led_set_rgb(THUMBYONE_LED_IDLE_R,
                          THUMBYONE_LED_IDLE_G,
                          THUMBYONE_LED_IDLE_B);
}
