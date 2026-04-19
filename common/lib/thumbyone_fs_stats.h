/*
 * ThumbyOne — shared shared-FAT disk-usage helper.
 *
 * One FatFs query + one formatting convention used by every place
 * that shows disk usage in the UI (lobby MENU overlay, NES picker
 * menu, P8 picker menu, MPY picker menu). Keeps the "X.X M / Y.Y M"
 * text and the progress-bar direction in lockstep — previously the
 * bar filled with FREE in the lobby but with USED in NES, and P8
 * displayed raw 512-byte sectors mislabelled "k".
 *
 * Convention (agreed 2026-04-19):
 *   - Bar fills with USED  (more data on disk = fuller bar; standard
 *     progress-toward-a-limit direction).
 *   - Text shows "USED / TOTAL"  (same direction as the bar).
 *
 * Caller must have already mounted the shared FAT (`f_mount` on ""
 * before this runs). Uses the default drive.
 */
#ifndef THUMBYONE_FS_STATS_H
#define THUMBYONE_FS_STATS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Query the shared FAT. On success returns true and fills all
 * three out pointers; on failure returns false and writes 0 to
 * each. Any pointer may be NULL. */
bool thumbyone_fs_get_usage(uint64_t *used_bytes,
                            uint64_t *free_bytes,
                            uint64_t *total_bytes);

/* Format a byte count as a compact "X.XM" / "YM" / "ZZZK" string
 * for the menu's narrow value column. No trailing unit suffix
 * beyond the single letter. `out` must hold at least 8 bytes. */
void thumbyone_fs_fmt_compact(uint64_t bytes, char *out, size_t n);

/* Format "USED / TOTAL" into a single buffer, e.g. "2.3M/9.6M".
 * Uses `thumbyone_fs_fmt_compact` for each side. `out` must hold
 * at least 16 bytes. Returns the number of characters written
 * (excluding the NUL). */
int thumbyone_fs_fmt_used_total(uint64_t used_bytes,
                                uint64_t total_bytes,
                                char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* THUMBYONE_FS_STATS_H */
