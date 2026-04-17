/*
 * ThumbyOne shared-FAT mount/format API — lobby-facing.
 *
 * The lobby owns the canonical shape of the shared FAT. Slots
 * (NES, P8, MPY) mount and read/write, but never format. This
 * header exposes the lifecycle: mount, format (with folder
 * skeleton), unmount.
 *
 * All operations target the single shared FAT at THUMBYONE_FAT_OFFSET
 * (see slot_layout.h). FF_VOLUMES = 1 in ffconf.h, so everything
 * uses drive 0 implicitly.
 */
#ifndef THUMBYONE_FS_H
#define THUMBYONE_FS_H

#include <stddef.h>
#include "ff.h"

/* Canonical mkfs parameters — MUST match the MKFS_PARM used by
 * mp-thumby's extmod/vfs_fat.c so all four slots produce
 * byte-identical on-disk layout.
 *
 * Shape: FAT16, 1 KB clusters, single-FAT, MBR-partitioned.
 * Volume label: "THUMBYONE".
 */
extern const MKFS_PARM thumbyone_mkfs_params;

/* Attempt to mount the shared FAT. On FR_OK the volume is ready
 * for use. `out_fs` must point to a caller-owned FATFS struct
 * that outlives the mount. */
FRESULT thumbyone_fs_mount(FATFS *out_fs);

/* Unconditionally format the shared FAT with the canonical shape,
 * label it "THUMBYONE", and create the folder skeleton
 * (/roms /carts /games). Leaves the volume mounted on `out_fs`.
 *
 * Called by the lobby's auto-format path (when mount fails) and
 * by the LB+RB wipe recovery chord. Destructive — the caller is
 * responsible for user confirmation. */
FRESULT thumbyone_fs_format(FATFS *out_fs, uint8_t *work, size_t worklen);

/* Mount, or format-then-mount if the volume has no valid FAT.
 * Convenience for the common case. */
FRESULT thumbyone_fs_mount_or_format(FATFS *out_fs, uint8_t *work, size_t worklen);

#endif /* THUMBYONE_FS_H */
