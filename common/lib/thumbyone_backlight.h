/*
 * ThumbyOne — shared hardware-PWM backlight driver (GP7).
 *
 * Every slot + lobby drives the LCD backlight through this module
 * so brightness changes in one place are consistent everywhere,
 * and so a future Brightness slider can be wired in without each
 * slot open-coding its own PWM setup.
 *
 * Hardware: GPIO 7 / PWM slice 3 channel B. 8-bit wrap (255) so
 * the "value" argument is a plain 0..255 brightness.
 *
 * Floor: inside thumbyone_backlight_set() the requested value is
 * clamped UP to THUMBYONE_BACKLIGHT_FLOOR. A user-set "0" on a
 * Brightness slider therefore still leaves the screen visible —
 * true-zero would make the device look broken and be very hard
 * to recover from (you'd need a slider you can't see to raise it
 * again). The floor is roughly 10 % PWM duty.
 *
 * DOOM does NOT use this module — it drives GP7 as a plain GPIO
 * "on" (full brightness) and has no brightness slider. Simpler +
 * saves the PWM resource.
 */
#ifndef THUMBYONE_BACKLIGHT_H
#define THUMBYONE_BACKLIGHT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THUMBYONE_BACKLIGHT_GPIO     7
#define THUMBYONE_BACKLIGHT_WRAP     255
#define THUMBYONE_BACKLIGHT_FLOOR    10   /* ~4 % — minimum visible in dim rooms */
#define THUMBYONE_BACKLIGHT_DEFAULT  255  /* full-on at boot */

/* Configure GP7 as PWM output. Idempotent. Also starts the PWM
 * slice running. Call once on boot / on taking ownership from
 * another driver. After this, brightness is 0 (off). */
void thumbyone_backlight_init(void);

/* Set the backlight level. `value` is clamped to
 * [THUMBYONE_BACKLIGHT_FLOOR, 255] so slider values of 0 still
 * leave the screen readable. Pass 0 to disable the floor — the
 * FLOOR constant is enforced for NORMAL operation; "hard off"
 * (for MPY slot teardown before handing the backlight to the
 * engine's PIO driver) goes via thumbyone_backlight_release(). */
void thumbyone_backlight_set(uint8_t value);

/* Release the PWM slice and return GP7 to a plain GPIO output
 * driven HIGH (full brightness). Use when handing the backlight
 * over to another driver that re-acquires the pin (MPY slot
 * engine uses its own PIO PWM for pixel-smooth brightness
 * changes; calling this before mp_init gives back the pin
 * cleanly). */
void thumbyone_backlight_release(void);

#ifdef __cplusplus
}
#endif

#endif /* THUMBYONE_BACKLIGHT_H */
