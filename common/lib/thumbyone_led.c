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

#define PIN_LED_G   10   /* PWM5 A */
#define PIN_LED_R   11   /* PWM5 B */
#define PIN_LED_B   12   /* PWM6 A */
#define LED_PWM_WRAP 2048

/* Peak LED duty cap. Raw hardware LED at 100 % duty is uncomfortably
 * bright; 80 % is the nominal "brightest" level — bumped from 63 %
 * on 2026-04-23 because the old cap felt too dim as the top of the
 * slider range. Keep in sync with lobby_main.c's open-coded copy and
 * engine_io_rp3.c's LED_MAX_ON_FRACTION. */
#define LED_MAX_ON_PCT  80

/* Minimum on-time applied per-channel when the caller set that
 * channel non-zero. Floors low-slider values so the LED stays
 * visible even when the screen is dimmed for a dark room. 180 =
 * ~9 % duty at WRAP 2048 (was 100 ≈ 5 %). */
#define LED_ON_TIME_FLOOR  180

/* Claim the pin as PWM and set our WRAP. This is called on every
 * set_rgb rather than once at startup, because sibling drivers in
 * the same slot may change the PWM wrap behind our back — e.g. NES
 * defrag reconfigures GPIO 11 with WRAP=255 for an 8-bit single-
 * channel setup; if the picker then writes a level computed against
 * WRAP=2048 the slice would modulo it and the LED output would be
 * silently wrong. Cheap (a few MMIO writes) and always correct. */
static void reclaim_pwm(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv_int_frac(slice, 1, 0);
    pwm_set_wrap(slice, LED_PWM_WRAP);
    pwm_set_enabled(slice, true);
}

void thumbyone_led_init(void) {
    reclaim_pwm(PIN_LED_R);
    reclaim_pwm(PIN_LED_G);
    reclaim_pwm(PIN_LED_B);
    pwm_set_gpio_level(PIN_LED_R, LED_PWM_WRAP);
    pwm_set_gpio_level(PIN_LED_G, LED_PWM_WRAP);
    pwm_set_gpio_level(PIN_LED_B, LED_PWM_WRAP);
}

/* Scale a single channel to the PWM off-time. uint64 because the
 * intermediate (channel × slider × PCT × WRAP) can exceed 2^32. */
static inline uint16_t channel_offtime(uint8_t channel, uint8_t slider) {
    uint64_t on = (uint64_t)channel * slider * LED_MAX_ON_PCT * LED_PWM_WRAP;
    uint64_t denom = 255ull * 255ull * 100ull;
    uint32_t on_time = (uint32_t)(on / denom);
    if (channel > 0 && on_time < LED_ON_TIME_FLOOR) on_time = LED_ON_TIME_FLOOR;
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
