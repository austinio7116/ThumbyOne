/*
 * ThumbyOne lobby — USB MSC layer.
 *
 * The lobby is the ONLY place on ThumbyOne that exposes USB: by
 * centralising the MSC stack here we guarantee there's never a
 * moment where both the host and a slot are trying to write to the
 * shared FAT. Slots never enumerate USB at all under THUMBYONE_SLOT_MODE.
 *
 * Backed by thumbyone_disk.c — the lobby MSC callbacks route every
 * read10 / write10 to the sector-level API on the 9.6 MB shared FAT
 * region at flash offset 0x660000.
 *
 * Usage from lobby_main.c:
 *
 *   lobby_usb_init();             // once, after clock/GPIO setup
 *   while (1) {
 *       lobby_usb_task();         // every iteration — pumps tud_task()
 *       // UI polling, etc.
 *   }
 */
#ifndef THUMBYONE_LOBBY_USB_H
#define THUMBYONE_LOBBY_USB_H

#include <stdint.h>
#include <stdbool.h>

/* Bring up tinyUSB and start MSC. Call once from main(). */
void lobby_usb_init(void);

/* Pump tinyUSB — call every main-loop iteration. Does nothing
 * noticeable when no host is attached. */
void lobby_usb_task(void);

/* True once the host has issued SET_CONFIG (drive mounted). Drops
 * back to false on detach / suspend. Good signal for showing a
 * "connected" indicator on screen. */
bool lobby_usb_mounted(void);

/* Microsecond timestamp of the last MSC read/write callback call.
 * Returns 0 if no transfer has happened yet. The lobby uses this to
 * flash an activity indicator while the host is copying data. */
uint64_t lobby_usb_last_op_us(void);

/* Write-back cache state. The lobby main loop polls both every
 * iteration and, whenever the cache holds dirty data AND MSC has
 * been quiet for >300 ms, calls lobby_usb_drain() to commit one
 * block. Calling drain() when there's nothing dirty is a cheap
 * no-op. */
bool lobby_usb_cache_dirty(void);
void lobby_usb_drain(void);

#endif
