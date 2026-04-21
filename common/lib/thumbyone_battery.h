/*
 * ThumbyOne — shared battery helper.
 *
 * One smoothed, hysteresised source of truth for battery percent,
 * charging state, and full-pack voltage. Every slot (NES, P8,
 * DOOM, MPY picker) plus the lobby delegates to this module so
 * "35% 3.42V" means the same thing everywhere.
 *
 * Reads GPIO 29 / ADC channel 3 — a voltage divider that returns
 * exactly half the pack voltage. A single read costs ~160 us (16
 * samples); the returned values are EMA-smoothed and percent /
 * charging flags are held steady with small hysteresis so the UI
 * doesn't twitch.
 *
 * Calibration (matches ThumbyNES v1.0 values):
 *   - Half-voltage 1.45 V  ->   0 %  (2.9 V pack cutoff)
 *   - Half-voltage 1.85 V  -> 100 %  (3.7 V pack full)
 *   - Charging flag: half-voltage >= 1.85 V (i.e. pack reads >= 3.7 V,
 *     which only happens on the USB rail).
 *
 * Thread-safety: not re-entrant. Call from one context.
 *
 * Non-re-entrancy note: adc_init() + adc_gpio_init() are idempotent
 * but not free — guarded by an internal "already initialised" flag.
 */
#ifndef THUMBYONE_BATTERY_H
#define THUMBYONE_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure ADC + pin. Idempotent. Safe to call multiple times. */
void thumbyone_battery_init(void);

/* Diagnostic read — exposes the internal numbers the percent
 * calculation is derived from. Any out pointer may be NULL.
 *
 *   raw_counts     : average 12-bit ADC counts after trimmed-mean
 *                    smoothing (0..4095)
 *   half_fresh_v   : half-divider voltage from this very read
 *                    (raw_counts × 3.3 / 4095), before EMA
 *   half_ema_v     : half-divider voltage after EMA smoothing —
 *                    what percent / charging-flag are computed from
 *
 * Used by the debug overlay to confirm whether the "45 % / 3.29 V"
 * report reflects a real under-voltage or an ADC-scaling bug. A
 * single fresh ADC burst feeds both this and the next
 * thumbyone_battery_read; do not call both back-to-back if the
 * second call's value needs to reflect the EXACT same samples. */
void thumbyone_battery_read_debug(int *raw_counts,
                                    float *half_fresh_v,
                                    float *half_ema_v);

/* Read smoothed battery state. Any of the out pointers may be NULL.
 *
 *   pct     : 0..100 (integer percent, hysteresised ±2 %)
 *   chg     : true when charging (hysteresised ±0.025 V around the
 *             1.85 V half-voltage threshold)
 *   volts   : full-pack voltage in volts (EMA-smoothed, ~3.0..4.2 V
 *             in normal operation)
 *
 * Performs a fresh ADC read every call. Call at menu-open and
 * cache, or call periodically — both work; the hysteresis keeps
 * the pct / chg outputs steady regardless. */
void thumbyone_battery_read(int *pct, bool *chg, float *volts);

#ifdef __cplusplus
}
#endif

#endif /* THUMBYONE_BATTERY_H */
