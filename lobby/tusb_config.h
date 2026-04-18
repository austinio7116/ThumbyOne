/*
 * TinyUSB configuration for the ThumbyOne lobby.
 *
 * MSC-only: the lobby is the single place where the user transfers
 * files over USB. CDC (Python REPL) isn't useful here because no
 * interpreter is running. Keeping the descriptor set minimal also
 * means Windows enumerates a single removable drive with a clean
 * per-slot identity, so drive-letter binding behaves predictably.
 */
#ifndef THUMBYONE_LOBBY_TUSB_CONFIG_H
#define THUMBYONE_LOBBY_TUSB_CONFIG_H

#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_OS             OPT_OS_PICO
#define CFG_TUSB_DEBUG          0

/* CRITICAL: without CFG_TUSB_RHPORT0_MODE, tusb_init() succeeds but
 * never calls tud_init() — device silently doesn't enumerate. See
 * the matching comment in ThumbyNES/device/tusb_config.h. */
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED         1
#define CFG_TUD_MAX_SPEED       OPT_MODE_FULL_SPEED

#define CFG_TUD_ENDPOINT0_SIZE  64

/* Class enables — MSC only. */
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* Sector buffer — must be at least one FAT sector (512 B). We keep
 * it at one sector because thumbyone_disk_write does its own
 * read-modify-erase on 4 KB boundaries; larger MSC EP buffer would
 * not speed that up. */
#define CFG_TUD_MSC_EP_BUFSIZE  512

#endif
