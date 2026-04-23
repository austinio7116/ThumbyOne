/*
 * ThumbyOne — shared backlight driver (PIO implementation).
 *
 * IMPORTANT history / rationale: an earlier version of this
 * driver used hardware PWM slice 3 channel B on GP7 (8-bit wrap).
 * That works standalone in the lobby, but inside the NES and P8
 * slots the audio path drives GP23 via hardware PWM slice 3
 * channel B too — **GP7 and GP23 alias onto the same slice+channel
 * on RP2040 / RP2350** (slice = (gpio>>1)&7, channel = gpio&1).
 * The audio callback rewrites CH3_CC.B on every audio sample with
 * values mapped to 0..512, which also drives the backlight, so the
 * user's saved brightness lasted until the first audio tick and
 * then the screen effectively flapped at audio rate.
 *
 * Fix: drive GP7 via PIO PWM (vendored from the MicroPython engine
 * — pwm.pio — public domain/BSD from the Raspberry Pi examples).
 * PIO0 SM0 runs a 7-instruction PWM loop that's completely
 * independent of the PWM slice hardware. 8-bit resolution (0..255),
 * floor enforced inside _set so slider 0 still leaves the screen
 * readable.
 */
#include "thumbyone_backlight.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "thumbyone_backlight_pio.h"

/* Use PIO0 SM0. Other users of PIO0 (engine audio, etc.) tend to
 * grab later SMs; SM0 is available on all slots we ship today. If
 * a new slot grows a PIO conflict, switch to dma_claim_unused_sm /
 * pio_claim_free_sm and track that in the state. */
#define BL_PIO             pio0
#define BL_SM              0
#define BL_PERIOD          255u

static bool g_inited = false;
static uint g_offset = 0;
static uint8_t g_last_value = THUMBYONE_BACKLIGHT_DEFAULT;

/* Load PWM value X into PIO X scratch: pull from TX FIFO. */
static void pio_pwm_set_level(uint32_t level) {
    /* pio_sm_put_blocking is safe from any context. We publish the
     * requested level; the SM picks it up on its next pull. */
    pio_sm_put_blocking(BL_PIO, BL_SM, level);
}

static void pio_pwm_set_period(uint32_t period) {
    pio_sm_set_enabled(BL_PIO, BL_SM, false);
    pio_sm_put_blocking(BL_PIO, BL_SM, period);
    /* Prime the ISR with the period. */
    pio_sm_exec(BL_PIO, BL_SM, pio_encode_pull(false, false));
    pio_sm_exec(BL_PIO, BL_SM, pio_encode_out(pio_isr, 32));
    pio_sm_set_enabled(BL_PIO, BL_SM, true);
}

void thumbyone_backlight_init(void) {
    if (g_inited) return;

    /* Claim + load the PIO program. pio_add_program crashes if the
     * program's already loaded at a different offset; we guard with
     * the idempotent g_inited flag so successive init calls are a
     * no-op. */
    g_offset = pio_add_program(BL_PIO, &pwm_program);
    pwm_program_init(BL_PIO, BL_SM, g_offset, THUMBYONE_BACKLIGHT_GPIO);
    pio_pwm_set_period(BL_PERIOD);
    pio_pwm_set_level(0);   /* off */

    g_inited = true;
}

void thumbyone_backlight_set(uint8_t value) {
    if (!g_inited) thumbyone_backlight_init();

    /* Floor — same rationale as before: slider-zero shouldn't
     * result in an invisible screen. */
    if (value < THUMBYONE_BACKLIGHT_FLOOR)
        value = THUMBYONE_BACKLIGHT_FLOOR;

    /* PIO program: counter Y is preloaded to ISR=PERIOD each cycle
     * and decremented to 0. On the decrement where Y matches X, it
     * side-sets the pin HIGH; the pin stays HIGH until the next
     * `pull noblock side 0` restart. So cycles-HIGH = X + 1.
     *
     * With PERIOD = 255: X = 0 → 1/256 duty (nearly off), X = 255
     * → 256/256 duty (fully on). Direct mapping — pass the
     * brightness byte straight through. */
    pio_pwm_set_level((uint32_t)value);
    g_last_value = value;
}

uint8_t thumbyone_backlight_get(void) {
    return g_last_value;
}

void thumbyone_backlight_track(uint8_t value) {
    if (value < THUMBYONE_BACKLIGHT_FLOOR)
        value = THUMBYONE_BACKLIGHT_FLOOR;
    g_last_value = value;
}

void thumbyone_backlight_release(void) {
    if (g_inited) {
        pio_sm_set_enabled(BL_PIO, BL_SM, false);
        /* Not removing the program from PIO memory — the slot will
         * likely reboot out of this image shortly anyway. */
        g_inited = false;
    }

    /* Leave GP7 as plain GPIO high so the next driver taking over
     * (rom_chain_image destination, or the MPY engine's own PIO
     * PWM) sees a visible screen through the handoff. */
    gpio_set_function(THUMBYONE_BACKLIGHT_GPIO, GPIO_FUNC_SIO);
    gpio_init(THUMBYONE_BACKLIGHT_GPIO);
    gpio_set_dir(THUMBYONE_BACKLIGHT_GPIO, GPIO_OUT);
    gpio_put(THUMBYONE_BACKLIGHT_GPIO, 1);
}
