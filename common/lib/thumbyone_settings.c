/*
 * ThumbyOne — shared system-wide settings (implementation).
 */
#include "thumbyone_settings.h"

#include "ff.h"

#define VOLUME_PATH      "/.volume"
#define BRIGHTNESS_PATH  "/.brightness"

static uint8_t load_byte_clamped(const char *path,
                                  uint8_t min_v, uint8_t max_v,
                                  uint8_t default_v) {
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return default_v;
    uint8_t b = 0;
    UINT nread = 0;
    FRESULT r = f_read(&f, &b, 1, &nread);
    f_close(&f);
    if (r != FR_OK || nread != 1) return default_v;
    if (b < min_v) return default_v;
    if (b > max_v) return max_v;
    return b;
}

static bool save_byte(const char *path, uint8_t value) {
    FIL f;
    if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT nwritten = 0;
    FRESULT r = f_write(&f, &value, 1, &nwritten);
    f_close(&f);
    return (r == FR_OK && nwritten == 1);
}

uint8_t thumbyone_settings_load_volume(void) {
    return load_byte_clamped(VOLUME_PATH,
                             THUMBYONE_VOLUME_MIN,
                             THUMBYONE_VOLUME_MAX,
                             THUMBYONE_VOLUME_DEFAULT);
}

uint8_t thumbyone_settings_load_brightness(void) {
    return load_byte_clamped(BRIGHTNESS_PATH,
                             THUMBYONE_BRIGHTNESS_MIN,
                             THUMBYONE_BRIGHTNESS_MAX,
                             THUMBYONE_BRIGHTNESS_DEFAULT);
}

bool thumbyone_settings_save_volume(uint8_t volume) {
    if (volume > THUMBYONE_VOLUME_MAX) volume = THUMBYONE_VOLUME_MAX;
    return save_byte(VOLUME_PATH, volume);
}

bool thumbyone_settings_save_brightness(uint8_t brightness) {
    if (brightness > THUMBYONE_BRIGHTNESS_MAX) brightness = THUMBYONE_BRIGHTNESS_MAX;
    return save_byte(BRIGHTNESS_PATH, brightness);
}
