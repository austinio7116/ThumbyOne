/*
 * ThumbyOne RTC helper — thin wrapper over the BM8563 driver.
 *
 * The BM8563 is on i2c0 at GPIO 8 (SDA) / 9 (SCL). The chip has a
 * VBAT input that may or may not have a backup supply on Color
 * hardware — `thumbyone_rtc_is_compromised()` reflects the chip's
 * own low-voltage flag, which gets latched whenever its supply
 * dropped below the threshold (i.e., the time you're about to read
 * back is no longer trustworthy).
 *
 * Lobby owns the canonical setting flow: a SET TIME submenu
 * (lobby_main.c) reads back current values and lets the user push
 * adjustments via the d-pad, then commits via this API. Slots can
 * read the current time at boot and use it however they like
 * (ThumbyNES wires it into peanut-GB's gb_set_rtc / gb_tick_rtc for
 * Pokemon Crystal/Gold/Silver berry growth + day-night cycle).
 */

#ifndef THUMBYONE_RTC_H
#define THUMBYONE_RTC_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise i2c0 + the BM8563 driver. Idempotent — safe to call
 * multiple times. Returns true on success, false if the chip didn't
 * ack on the bus (no chip, bus stuck, wrong wiring, etc.). */
bool thumbyone_rtc_init(void);

/* Read current time into a struct tm. Returns 0 on success,
 * non-zero on I2C error. The compromised flag is checked separately
 * via thumbyone_rtc_is_compromised() and is NOT reflected in the
 * return value here — the caller should decide what to do with
 * stale-but-readable time. */
int thumbyone_rtc_get(struct tm *out);

/* Write a struct tm to the chip. Returns 0 on success, non-zero on
 * I2C error. Writing also clears the low-voltage / compromised flag
 * (the chip resets its VL bit on a write to the seconds register). */
int thumbyone_rtc_set(const struct tm *in);

/* Returns true if the BM8563's low-voltage flag is set, meaning
 * its time has been corrupted by a power loss / undervoltage event
 * since the last write. False after a successful set or if the chip
 * has been continuously powered. */
bool thumbyone_rtc_is_compromised(void);

#ifdef __cplusplus
}
#endif

#endif /* THUMBYONE_RTC_H */
