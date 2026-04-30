/*
 * ThumbyOne RTC helper — see thumbyone_rtc.h for the contract.
 *
 * Mirrors the engine's src/time/engine_rtc.c approach (i2c0 @ 100 kHz,
 * GPIO 8/9 with pull-ups enabled, hardware-agnostic bm8563 driver
 * given C i2c read/write callbacks). Kept separate so the lobby
 * (and other slots that need RTC access without dragging the engine
 * in) can link this directly.
 */

#include "thumbyone_rtc.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "lib/bm8563/bm8563.h"

#define RTC_I2C            i2c0
#define RTC_I2C_HZ         100000      /* 100 kHz — matches engine's choice; */
                                       /* 400 kHz was unreliable on this board. */
#define RTC_I2C_SDA_GPIO   8
#define RTC_I2C_SCL_GPIO   9

static bm8563_t s_bm;
static bool s_initialised = false;

static int32_t i2c_read_cb(void *handle, uint8_t address, uint8_t reg,
                           uint8_t *buffer, uint16_t size) {
    (void)handle;
    /* Write the register pointer first (with restart). */
    int ret = i2c_write_timeout_us(RTC_I2C, address, &reg, 1, true, 500000);
    if (ret == PICO_ERROR_GENERIC || ret == PICO_ERROR_TIMEOUT) {
        return BM8563_ERROR_NOTTY;
    }
    /* Read N bytes (no restart, then stop). */
    ret = i2c_read_timeout_us(RTC_I2C, address, buffer, size, false, 500000);
    if (ret == PICO_ERROR_GENERIC || ret == PICO_ERROR_TIMEOUT) {
        return BM8563_ERROR_NOTTY;
    }
    return BM8563_OK;
}

static int32_t i2c_write_cb(void *handle, uint8_t address, uint8_t reg,
                            const uint8_t *buffer, uint16_t size) {
    (void)handle;
    /* I2C write = single transaction with [reg, ...payload]. Stack-
     * allocated copy keeps it bounded; bm8563 calls write with at
     * most 7 bytes payload (full datetime), so the VLA is safe. */
    uint8_t data[size + 1];
    data[0] = reg;
    memcpy(data + 1, buffer, size);
    int ret = i2c_write_timeout_us(RTC_I2C, address, data, size + 1, false, 500000);
    if (ret == PICO_ERROR_GENERIC || ret == PICO_ERROR_TIMEOUT) {
        return BM8563_ERROR_NOTTY;
    }
    return BM8563_OK;
}

bool thumbyone_rtc_init(void) {
    if (s_initialised) return true;

    i2c_init(RTC_I2C, RTC_I2C_HZ);
    gpio_set_function(RTC_I2C_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(RTC_I2C_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_set_pulls(RTC_I2C_SDA_GPIO, true, false);
    gpio_set_pulls(RTC_I2C_SCL_GPIO, true, false);

    s_bm.read = &i2c_read_cb;
    s_bm.write = &i2c_write_cb;
    s_bm.handle = NULL;
    bm8563_init(&s_bm);

    /* Probe the chip with a single read — confirms it's there and
     * acks. We don't act on the result here; callers can detect by
     * checking thumbyone_rtc_get's return code. */
    struct tm probe;
    bm8563_read(&s_bm, &probe);

    s_initialised = true;
    return true;
}

int thumbyone_rtc_get(struct tm *out) {
    if (!out) return -1;
    if (!s_initialised && !thumbyone_rtc_init()) return -1;
    bm8563_err_t err = bm8563_read(&s_bm, out);
    /* BM8563_ERR_LOW_VOLTAGE means the read was partial / stale but
     * the chip still spoke; we return success so the caller can show
     * something and prompt for a re-set via is_compromised(). */
    if (err == BM8563_OK || err == BM8563_ERR_LOW_VOLTAGE) return 0;
    return -1;
}

int thumbyone_rtc_set(const struct tm *in) {
    if (!in) return -1;
    if (!s_initialised && !thumbyone_rtc_init()) return -1;
    /* bm8563_write takes non-const struct tm * (legacy API); cast off
     * the const since the lib doesn't actually mutate the input. */
    struct tm copy = *in;
    bm8563_err_t err = bm8563_write(&s_bm, &copy);
    return (err == BM8563_OK) ? 0 : -1;
}

bool thumbyone_rtc_is_compromised(void) {
    if (!s_initialised && !thumbyone_rtc_init()) return true;
    struct tm probe;
    bm8563_err_t err = bm8563_read(&s_bm, &probe);
    return (err == BM8563_ERR_LOW_VOLTAGE);
}
