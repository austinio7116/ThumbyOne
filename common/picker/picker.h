/*
 * ThumbyOne MPY-slot C picker.
 *
 * Runs as the FIRST THING in the MPY slot's main(), before
 * MicroPython is initialised. Draws a game chooser on the LCD
 * using the same GC9107 driver + bitmap font the lobby uses,
 * scans the shared FAT at `/games/<name>/main.py`, and on
 * selection writes `/.active_game` to flash so the Python-side
 * frozen launcher (thumbyone_launcher.py) knows which game to
 * exec.
 *
 * The picker mounts the shared FAT for read+write, so it can
 * also update `/.active_game` on every selection. It unmounts
 * before returning — MicroPython's extmod VFS then does its own
 * fresh mount via rp2.Flash / VfsFat in _boot_fat.py.
 *
 * UX:
 *   UP / DOWN   — scroll list
 *   A           — launch highlighted game
 *   MENU (long) — return to ThumbyOne lobby via
 *                 thumbyone_handoff_request_lobby() (does not
 *                 return; slot reboots, lobby comes back up)
 *   empty /games/  → "drop games" splash, stays there; user can
 *                    MENU-long to escape to the lobby and drop
 *                    files via the lobby's USB drive
 */
#ifndef THUMBYONE_PICKER_H
#define THUMBYONE_PICKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Run the picker. On successful selection:
 *
 *   - Writes the absolute path of the chosen game directory to
 *     /.active_game (e.g. "/games/mygame") on the shared FAT
 *   - Unmounts the FAT (so MicroPython's own mount can take
 *     over cleanly)
 *   - Returns 0
 *
 * On MENU-long-hold, calls thumbyone_handoff_request_lobby()
 * and does NOT return.
 *
 * On filesystem error, shows an error splash and returns -1.
 */
int thumbyone_picker_run(void);

#ifdef __cplusplus
}
#endif

#endif /* THUMBYONE_PICKER_H */
