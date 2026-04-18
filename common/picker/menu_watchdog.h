/*
 * ThumbyOne — cross-slot MENU-long-hold "return to lobby" watchdog.
 *
 * Slots that don't have their own in-game MENU overlay (the MPY
 * slot, specifically, because games are arbitrary Python that owns
 * the LCD + button polling) need a way for the user to bail out
 * back to the top-level lobby even when a game is misbehaving.
 * The watchdog installs a 100 Hz pico-sdk repeating timer that
 * polls GPIO 26 (MENU); if MENU stays held for 2 seconds it fires
 * thumbyone_handoff_request_lobby() — same code path the picker
 * uses for its own long-hold escape, but driven from an IRQ so it
 * keeps working even when core 0 is spinning inside a hung game.
 *
 * Safe to call more than once (idempotent).
 */
#ifndef THUMBYONE_MENU_WATCHDOG_H
#define THUMBYONE_MENU_WATCHDOG_H

void thumbyone_menu_watchdog_install(void);

#endif
