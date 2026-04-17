/*
 * ThumbyOne handoff — cross-reboot state for lobby <-> slot
 * transitions.
 *
 * USAGE
 *   From the lobby:
 *     - User picks a system on the selector.
 *     - Call thumbyone_handoff_request_slot(THUMBYONE_SLOT_NES).
 *     - Library writes magic to watchdog_hw->scratch[0..1] and
 *       calls rom_reboot(NORMAL). Does not return.
 *
 *   Lobby main() first line on every boot:
 *     - Call thumbyone_handoff_consume_if_present(workarea, 4096).
 *     - If the scratch magic is set, library clears it and
 *       chains into the target slot's partition via
 *       rom_chain_image — with no peripheral init between boot
 *       and chain, the condition rom_chain_image needs to work.
 *       On success, does not return.
 *     - Otherwise returns so the lobby can run its normal UI.
 *
 *   From a slot (going back to lobby):
 *     - Call thumbyone_handoff_request_lobby(). Library calls
 *       watchdog_reboot(0,0,0). On the next boot no magic is
 *       set, so the lobby runs its normal UI.
 *
 * WIRE FORMAT (in watchdog_hw->scratch[0..1])
 *   scratch[0] = (THUMBYONE_HANDOFF_MAGIC_BASE | slot_id_low4)
 *                — magic in the upper 28 bits, slot id in the
 *                low 4 bits.
 *   scratch[1] = XOR of scratch[0] with THUMBYONE_HANDOFF_CHECK
 *                — rough integrity check so random leftover
 *                values in scratch don't accidentally trigger a
 *                slot launch.
 *
 * Why two registers: user scratch[0..3] are free but any of
 * them could pattern-match a magic by accident on a power cycle
 * from an entirely different firmware. A derived-from-scratch[0]
 * check word in scratch[1] catches that.
 */
#ifndef THUMBYONE_HANDOFF_H
#define THUMBYONE_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>
#include "slot_layout.h"

/* Magic in the upper bits of scratch[0]. Bottom 4 bits reserved
 * for the slot id (0..15, we use 0..4). */
#define THUMBYONE_HANDOFF_MAGIC_BASE   0xDB1F00B0u
#define THUMBYONE_HANDOFF_MAGIC_MASK   0xFFFFFFF0u
#define THUMBYONE_HANDOFF_SLOT_MASK    0x0000000Fu

/* Check word derivation: scratch[1] = scratch[0] XOR this. */
#define THUMBYONE_HANDOFF_CHECK        0xC0FFEE42u

/* Read the current scratch-based handoff. Returns
 *   *out_slot = target slot if a valid handoff is present,
 *   true if a valid handoff is present, else false. */
bool thumbyone_handoff_peek(thumbyone_slot_t *out_slot);

/* Clear the scratch handoff — called by the lobby after
 * consuming it. Safe to call when no handoff is present. */
void thumbyone_handoff_clear(void);

/* Write a handoff asking the next boot to go to `slot` and
 * reboot the chip via rom_reboot(NORMAL | NO_RETURN_ON_SUCCESS).
 * Does not return on success. `slot` must be a launchable slot
 * (non-lobby). */
void thumbyone_handoff_request_slot(thumbyone_slot_t slot);

/* Write no handoff (lobby is the default-boot image) and reboot.
 * Does not return on success. Called by a slot when the user
 * wants to go back to the lobby. */
void thumbyone_handoff_request_lobby(void);

/* If a valid handoff to a non-lobby slot is present in scratch,
 * clear it and chain into the target slot's partition via
 * rom_chain_image. Does not return on success.
 *
 * If no handoff is present (or the target is the lobby), returns
 * normally so the caller can continue with lobby init.
 *
 * On rom_chain_image failure, falls back to BOOTSEL via
 * reset_usb_boot so the device doesn't soft-brick.
 *
 * `workarea` must be at least 4 KiB, 4-byte aligned. The lobby
 * should call this as the very first thing in main(), before
 * any peripheral init. */
void thumbyone_handoff_consume_if_present(uint8_t *workarea,
                                           uint32_t workarea_size);

#endif /* THUMBYONE_HANDOFF_H */
