/*
 * ThumbyOne shared-FAT flash block device.
 *
 * Exposes a sector-addressed read/write/sync API over the 9.6 MB
 * FAT region at physical flash offset 0x660000 (see slot_layout.h
 * — THUMBYONE_FAT_OFFSET / THUMBYONE_FAT_SIZE). Sector size is
 * fixed at 512 bytes; flash erase unit is 4 KB so writes are
 * issued as read-modify-erase-program at the 4 KB granularity
 * that the flash wants.
 *
 * Consumed by:
 *   - Lobby (common/fs/thumbyone_diskio.c) → FatFs diskio for
 *     mounting, reading, and writing the shared FAT.
 *   - NES slot (after E.7) — replaces device/nes_flash_disk.c's
 *     direct flash ops.
 *   - P8 slot (after E.7) — replaces device/p8_flash_disk.c.
 *
 * Not used by:
 *   - DOOM (embeds WAD, no shared-FAT access).
 *   - MPY's Python-facing `rp2.Flash` (goes through
 *     ports/rp2/rp2_flash.c, which could be repointed at this
 *     layer later — tracked under task C2c / E.5b).
 *
 * Thread model: single-threaded. Any function that writes to
 * flash disables interrupts internally for the erase + program
 * window (~50 ms worst case per 4 KB). Do not call from ISR
 * context or while USB MSC is mid-transfer.
 */
#ifndef THUMBYONE_DISK_H
#define THUMBYONE_DISK_H

#include <stdint.h>
#include <stddef.h>

#define THUMBYONE_DISK_SECTOR_SIZE   512u
#define THUMBYONE_DISK_ERASE_SIZE   4096u

/* Number of logical sectors exposed. Derived from slot_layout.h
 * so the shared FAT region's total capacity is the single source
 * of truth. */
uint32_t thumbyone_disk_sector_count(void);
uint32_t thumbyone_disk_sector_size(void);

/* Sector-level read. Pulls from XIP-mapped flash — the FAT region
 * is always addressable through ATRANS (thumbyone_slot_init sets
 * up the identity mapping for slots 1..3; the lobby has direct
 * identity access). Returns 0 on success, -1 on out-of-range. */
int thumbyone_disk_read (uint8_t *dst, uint32_t sector, uint32_t count);

/* Sector-level write. Performs full erase-program cycles on any
 * 4 KB flash sectors touched by the range: reads the existing
 * sector (minus the overwritten portion) from XIP into a stack
 * buffer, erases the 4 KB block, programs the modified buffer.
 *
 * Returns 0 on success, -1 on out-of-range or flash error. */
int thumbyone_disk_write(const uint8_t *src, uint32_t sector, uint32_t count);

/* No-op sync — writes go through synchronously (no cache layer
 * at this level; a cache lives on top in the lobby's MSC path). */
int thumbyone_disk_sync(void);

#endif /* THUMBYONE_DISK_H */
