/*
 * ThumbyOne — shared system-wide settings (volume + brightness).
 *
 * One-byte-each files on the shared FAT root:
 *   /.volume      : 0..20 unified volume scale
 *   /.brightness  : 0..255 backlight PWM level
 *
 * Set once (typically in the lobby), applied everywhere: every
 * FatFs-enabled slot reads these on boot and after its own menu
 * closes, so "turn the volume down" in the lobby actually silences
 * NES / P8 / MPY on the very next launch, and brightness tracks the
 * same way.
 *
 * Defaults if the files are absent (first boot, or after a FAT
 * wipe): VOL_DEFAULT=10 (50 %), BRIGHT_DEFAULT=255 (full-on).
 *
 * Writes are tiny: single f_open + f_write + f_close per byte. No
 * magic, no versioning — if the file is empty / too short / out
 * of range we clamp to the default on read. The files live at the
 * FAT root so they're visible + editable from the host over USB
 * MSC (drop in a custom /.brightness byte if you want).
 *
 * DOOM does NOT use this module — it has no FatFs. DOOM keeps its
 * own volume in flash slot 7 and has no brightness concept. That's
 * the only slot where the "set in lobby" model doesn't reach.
 */
#ifndef THUMBYONE_SETTINGS_H
#define THUMBYONE_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THUMBYONE_VOLUME_MIN        0
#define THUMBYONE_VOLUME_MAX       20
#define THUMBYONE_VOLUME_DEFAULT   10

#define THUMBYONE_BRIGHTNESS_MIN     0
#define THUMBYONE_BRIGHTNESS_MAX   255
#define THUMBYONE_BRIGHTNESS_DEFAULT 255

/* Read /.volume from the mounted shared FAT. Returns the byte,
 * clamped to [MIN, MAX]. If the file is missing / empty / unreadable,
 * returns THUMBYONE_VOLUME_DEFAULT. Caller must have already
 * f_mount'd the shared FAT. */
uint8_t thumbyone_settings_load_volume(void);

/* Read /.brightness. Same rules. */
uint8_t thumbyone_settings_load_brightness(void);

/* Write one byte to /.volume or /.brightness. Clamps to the valid
 * range before writing. Returns true on success. Caller must have
 * the shared FAT mounted writable. */
bool thumbyone_settings_save_volume(uint8_t volume);
bool thumbyone_settings_save_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif /* THUMBYONE_SETTINGS_H */
