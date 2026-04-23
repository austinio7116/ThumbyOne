/*
 * ThumbyOne — shared front-RGB-indicator driver.
 *
 * The device's front RGB LED is wired common-anode across GPIO 10
 * (green), 11 (red), 12 (blue). Each slot in ThumbyOne had been
 * open-coding its own init + drive for these pins (lobby, NES
 * defrag, MPY engine). This module centralises the PWM setup and
 * the "scale by live brightness slider" policy so every slot drives
 * the LED identically and the picker menu can paint the LED live as
 * the user drags the brightness slider.
 *
 * Brightness policy (matches lobby/NES/MPY): output is scaled by
 * the shared backlight slider (thumbyone_backlight_get()) and
 * capped at LED_MAX_ON_PCT % of full duty, so the LED dims with
 * the screen instead of blinding the user in a dark room. Channels
 * the caller requested as exactly 0 still go fully off; channels
 * set >0 are floored to a visible minimum.
 */
#ifndef THUMBYONE_LED_H
#define THUMBYONE_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent. Configures GP10/11/12 as PWM outputs (WRAP=2048) and
 * writes "fully off" to each channel. Subsequent calls are no-ops.
 * Safe to call from any slot. */
void thumbyone_led_init(void);

/* Set the front RGB LED to a specific colour, scaled by the live
 * shared backlight slider (thumbyone_backlight_get()). Each channel
 * is 0..255 with 0=off; the peak is capped per the module-wide
 * policy. Reads the slider live every call so picker/menu drags
 * repaint in real time. */
void thumbyone_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Turn all three channels fully off. Leaves GPIOs as PWM at level=
 * off so a subsequent set_rgb call can just write new PWM levels
 * without re-initialising. */
void thumbyone_led_off(void);

/* Re-apply the last colour passed to thumbyone_led_set_rgb at the
 * current shared-brightness slider value. Callers that just
 * committed a brightness change use this to make the LED track the
 * new slider without needing to remember what colour they last
 * set. If no colour has been set yet (LED never driven by this
 * module in the current slot) the call is a no-op. */
void thumbyone_led_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* THUMBYONE_LED_H */
