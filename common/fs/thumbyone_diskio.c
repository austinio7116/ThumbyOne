/*
 * FatFs R0.15 diskio glue — single-drive (pdrv == 0) wrapper
 * around thumbyone_disk. Drop into any consumer (lobby, NES, P8)
 * that wants to mount the shared FAT with upstream FatFs.
 *
 * MPY does NOT use this file — its extmod diskio lives in
 * mp-thumby/extmod/vfs_fat_diskio.c and routes through the Python
 * bdev (rp2.Flash). Result is the same on the wire: single 9.6 MB
 * FAT16 volume at physical flash 0x660000.
 */
#include "ff.h"
#include "diskio.h"
#include "thumbyone_disk.h"

DSTATUS disk_status(BYTE pdrv) {
    return (pdrv == 0) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    /* thumbyone_disk has no init step — it's just XIP reads and
     * SDK flash_range_* calls. Return ready for pdrv 0. */
    return (pdrv == 0) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;
    return (thumbyone_disk_read(buff, (uint32_t)sector, count) == 0)
         ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;
    return (thumbyone_disk_write(buff, (uint32_t)sector, count) == 0)
         ? RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC:
        return (thumbyone_disk_sync() == 0) ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (LBA_t)thumbyone_disk_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)thumbyone_disk_sector_size();
        return RES_OK;
    case GET_BLOCK_SIZE:
        /* Erase block in sectors: 4 KB / 512 B = 8. Used by f_mkfs
         * to pick cluster boundaries. */
        *(DWORD *)buff = (DWORD)(THUMBYONE_DISK_ERASE_SIZE /
                                 THUMBYONE_DISK_SECTOR_SIZE);
        return RES_OK;
    }
    return RES_PARERR;
}

/* Real-time clock stub. Consumers that have an RTC (ThumbyNES
 * reads one; lobby doesn't bother) can override this weak via a
 * stronger definition. Default returns a fixed 2026-01-01. */
__attribute__((weak)) DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}
