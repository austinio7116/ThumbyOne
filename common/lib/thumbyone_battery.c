/*
 * ThumbyOne — shared battery helper implementation.
 */
#include "thumbyone_battery.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"

/* --- wiring -------------------------------------------------------- */
#define BATT_GPIO        29
#define BATT_ADC_CH       3
#define ADC_REF_VOLTS     3.3f
#define ADC_MAX_COUNT     4095

/* --- calibration --------------------------------------------------- */
/* Half-voltage = divider-output voltage. Pack voltage = 2 * half. */
#define HALF_MIN_V         1.45f   /*   0 %  (~2.9 V pack) */
#define HALF_MAX_V         1.85f   /* 100 %  (~3.7 V pack) */
#define HALF_SANITY_FLOOR  0.50f   /* below this = "bad read", ignore */

/* Charging hysteresis band (± around HALF_MAX_V). */
#define CHG_ENTER_V       (HALF_MAX_V + 0.025f)
#define CHG_LEAVE_V       (HALF_MAX_V - 0.025f)

/* Percent hysteresis — don't change the reported integer percent
 * unless the smoothed value has moved by at least this much. */
#define PCT_HYSTERESIS     2

/* Sampling. */
#define SAMPLE_COUNT       16    /* raw samples per read */
#define TRIM_EACH_SIDE      2    /* drop 2 highest + 2 lowest before mean */

/* EMA smoothing weight. y_new = (EMA_NUM * fresh + (EMA_DEN - EMA_NUM) * y_prev) / EMA_DEN */
#define EMA_NUM            1
#define EMA_DEN            4

/* --- state --------------------------------------------------------- */
static bool  g_ready           = false;
static bool  g_have_prev       = false;
static float g_half_ema        = 0.0f;
static int   g_reported_pct    = -1;
static bool  g_reported_chg    = false;

/* --- helpers ------------------------------------------------------- */

/* Insertion sort — 16 elements, negligible cost (~100 cycles). */
static void sort16_asc(uint16_t a[SAMPLE_COUNT]) {
    for (int i = 1; i < SAMPLE_COUNT; ++i) {
        uint16_t v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; --j; }
        a[j + 1] = v;
    }
}

static float read_half_voltage_raw(void) {
    adc_select_input(BATT_ADC_CH);
    /* RP2040/2350 ADC: first sample after input-select returns the
     * stale previous-channel conversion. Discard. */
    (void)adc_read();

    uint16_t samples[SAMPLE_COUNT];
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        samples[i] = adc_read();
    }
    sort16_asc(samples);

    /* Trimmed mean — drop TRIM_EACH_SIDE from each end, average the rest. */
    uint32_t sum = 0;
    const int lo = TRIM_EACH_SIDE;
    const int hi = SAMPLE_COUNT - TRIM_EACH_SIDE;
    for (int i = lo; i < hi; ++i) sum += samples[i];
    float avg_counts = (float)sum / (float)(hi - lo);

    return avg_counts * ADC_REF_VOLTS / (float)ADC_MAX_COUNT;
}

static int half_to_pct(float h) {
    if (h <= HALF_MIN_V) return 0;
    if (h >= HALF_MAX_V) return 100;
    float frac = (h - HALF_MIN_V) / (HALF_MAX_V - HALF_MIN_V);
    int p = (int)(frac * 100.0f + 0.5f);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

/* --- public API ---------------------------------------------------- */

void thumbyone_battery_init(void) {
    if (g_ready) return;
    adc_init();
    adc_gpio_init(BATT_GPIO);
    g_ready = true;
}

void thumbyone_battery_read(int *pct, bool *chg, float *volts) {
    thumbyone_battery_init();

    float h_fresh = read_half_voltage_raw();

    /* Sanity floor — if the reading is below the floor, the divider
     * isn't driven (pack disconnected?) or the ADC is misbehaving.
     * Keep whatever we had before. */
    if (h_fresh < HALF_SANITY_FLOOR) {
        if (pct)   *pct   = (g_reported_pct >= 0) ? g_reported_pct : 0;
        if (chg)   *chg   = g_reported_chg;
        if (volts) *volts = g_have_prev ? (g_half_ema * 2.0f) : 0.0f;
        return;
    }

    /* EMA smoothing. First sample seeds the filter directly. */
    if (!g_have_prev) {
        g_half_ema  = h_fresh;
        g_have_prev = true;
    } else {
        g_half_ema  = ((float)EMA_NUM * h_fresh
                    + (float)(EMA_DEN - EMA_NUM) * g_half_ema)
                    / (float)EMA_DEN;
    }

    /* Raw smoothed percent. */
    int p_smooth = half_to_pct(g_half_ema);

    /* Percent hysteresis — move the reported value only if the
     * smoothed value has crept far enough from the last reported
     * value to justify a redraw. Edges of the scale (0 / 100) pin
     * directly so we don't lie at the rails. */
    if (g_reported_pct < 0) {
        g_reported_pct = p_smooth;
    } else if (p_smooth == 0 || p_smooth == 100) {
        g_reported_pct = p_smooth;
    } else {
        int delta = p_smooth - g_reported_pct;
        if (delta >= PCT_HYSTERESIS || -delta >= PCT_HYSTERESIS) {
            g_reported_pct = p_smooth;
        }
    }

    /* Charging hysteresis. */
    if (g_reported_chg) {
        if (g_half_ema < CHG_LEAVE_V) g_reported_chg = false;
    } else {
        if (g_half_ema > CHG_ENTER_V) g_reported_chg = true;
    }

    if (pct)   *pct   = g_reported_pct;
    if (chg)   *chg   = g_reported_chg;
    if (volts) *volts = g_half_ema * 2.0f;
}
