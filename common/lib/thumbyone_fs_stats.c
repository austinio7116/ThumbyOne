/*
 * ThumbyOne — shared shared-FAT disk-usage helper (implementation).
 */
#include "thumbyone_fs_stats.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"

/* --- query --------------------------------------------------------- */

bool thumbyone_fs_get_usage(uint64_t *used_bytes,
                            uint64_t *free_bytes,
                            uint64_t *total_bytes) {
    if (used_bytes)  *used_bytes  = 0;
    if (free_bytes)  *free_bytes  = 0;
    if (total_bytes) *total_bytes = 0;

    FATFS *fs = NULL;
    DWORD free_clust = 0;
    if (f_getfree("/", &free_clust, &fs) != FR_OK || !fs) return false;

    /* FatFs R0.15: total sectors = (n_fatent - 2) * csize,
     * free = free_clust * csize. Sector size = 512 for all the
     * cluster sizes we use (1 KB, 2 KB, 4 KB). */
    uint64_t total_sec = (uint64_t)(fs->n_fatent - 2) * fs->csize;
    uint64_t free_sec  = (uint64_t)free_clust * fs->csize;
    uint64_t used_sec  = (total_sec > free_sec) ? (total_sec - free_sec) : 0;

    const uint64_t SECTOR = 512;
    if (total_bytes) *total_bytes = total_sec * SECTOR;
    if (free_bytes)  *free_bytes  = free_sec  * SECTOR;
    if (used_bytes)  *used_bytes  = used_sec  * SECTOR;
    return true;
}

/* --- formatting ---------------------------------------------------- */

/* Format as a compact "X.XM" / "YM" / "ZZZK" string. One letter
 * unit suffix. Rolls over to M at 1024 K so small FATs still get
 * the K suffix (unused today — our shared FAT is always >1 MB —
 * but keeps the helper correct for smaller volumes). */
void thumbyone_fs_fmt_compact(uint64_t bytes, char *out, size_t n) {
    if (!out || n == 0) return;
    if (n < 2) { out[0] = 0; return; }

    uint64_t kb = bytes / 1024;

    if (kb < 1024) {
        /* Fits in KB. No decimal. */
        snprintf(out, n, "%luK", (unsigned long)kb);
        return;
    }

    /* MB with one decimal place. */
    unsigned long mb_whole = (unsigned long)(kb / 1024);
    unsigned long mb_tenths = (unsigned long)(((kb % 1024) * 10) / 1024);
    snprintf(out, n, "%lu.%luM", mb_whole, mb_tenths);
}

int thumbyone_fs_fmt_used_total(uint64_t used_bytes,
                                uint64_t total_bytes,
                                char *out, size_t n) {
    if (!out || n == 0) return 0;

    char u[10], t[10];
    thumbyone_fs_fmt_compact(used_bytes,  u, sizeof(u));
    thumbyone_fs_fmt_compact(total_bytes, t, sizeof(t));
    return snprintf(out, n, "%s/%s", u, t);
}
