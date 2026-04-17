/*
 * Lobby-owned mount/format for the shared FAT. See header for
 * policy.
 */
#include "thumbyone_fs.h"

#include <stddef.h>

/* Canonical FAT shape. DO NOT CHANGE WITHOUT UPDATING
 * mp-thumby/extmod/vfs_fat.c:mkfs_default_opt — they must match
 * byte-for-byte so the MPY slot's own mkfs (which may run as a
 * belt-and-braces fallback in _boot_fat.py) produces an identical
 * on-disk layout to what the lobby writes.
 *
 *   FM_FAT        FAT16 auto-select, MBR-partitioned (no FM_SFD).
 *                 Partitioned FAT is what Windows expects for
 *                 USB-MSC removable drives; SFD triggers "drive
 *                 not formatted" prompts on several Windows
 *                 versions.
 *   n_fat = 1     Single FAT copy; the redundancy isn't useful
 *                 on a device that's its own FS master.
 *   au_size = 1024  1 KB clusters → 9600 clusters on a 9.6 MB
 *                 volume. Comfortably above FAT12 (4084) and
 *                 below FAT16 (65524). Small clusters keep
 *                 per-file slack low for Python-game save data.
 */
const MKFS_PARM thumbyone_mkfs_params = {
    .fmt     = FM_FAT,
    .n_fat   = 1,
    .align   = 0,
    .n_root  = 0,
    .au_size = 1024,
};

FRESULT thumbyone_fs_mount(FATFS *out_fs) {
    /* opt=1 forces immediate mount (probes BPB now rather than on
     * first file op) so the caller can react to FR_NO_FILESYSTEM. */
    return f_mount(out_fs, "", 1);
}

FRESULT thumbyone_fs_format(FATFS *out_fs, uint8_t *work, size_t worklen) {
    /* Release any existing mount first. f_unmount("") is a macro
     * for f_mount(0, "", 0) in R0.15 — clears the slot 0 binding. */
    f_unmount("");

    FRESULT r = f_mkfs("", &thumbyone_mkfs_params, work, worklen);
    if (r != FR_OK) return r;

    r = f_mount(out_fs, "", 1);
    if (r != FR_OK) return r;

    /* Volume label. Identity that Windows shows in Explorer. */
    f_setlabel("THUMBYONE");

    /* Folder skeleton. Ignore FR_EXIST — a concurrent slot could
     * in theory have created these already (though they can't,
     * in our architecture, because only the lobby formats). Any
     * other error is fatal enough to surface. */
    (void)f_mkdir("/roms");
    (void)f_mkdir("/carts");
    (void)f_mkdir("/games");

    return FR_OK;
}

FRESULT thumbyone_fs_mount_or_format(FATFS *out_fs, uint8_t *work, size_t worklen) {
    FRESULT r = thumbyone_fs_mount(out_fs);
    if (r == FR_OK) return FR_OK;
    if (r != FR_NO_FILESYSTEM) return r;
    return thumbyone_fs_format(out_fs, work, worklen);
}
