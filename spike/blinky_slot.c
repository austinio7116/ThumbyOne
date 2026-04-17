/*
 * ThumbyOne flash-layout spike — uses common/thumbyone_handoff.
 *
 * Proves the architecture ThumbyOne needs: two independent
 * firmwares coexist in flash, and the "lobby" hands control to
 * a second slot via the shared handoff library.
 *
 * FLOW
 *   cold boot                   → lobby UI
 *   lobby A press               → thumbyone_handoff_request_slot(SLOT_NES)
 *                                 (scratch magic + rom_reboot)
 *   lobby post-reboot main()    → thumbyone_handoff_consume_if_present
 *                                 (chain into the NES partition)
 *   app (runs from NES slot)    → backlight flashes
 *   app A press                 → thumbyone_handoff_request_lobby()
 *                                 (watchdog_reboot(0,0,0))
 *   cold boot                   → lobby UI (no handoff magic)
 *
 * Slot 0 in this spike plays the role of the real ThumbyOne
 * lobby; slot 1 is a placeholder for whichever real firmware
 * eventually fills the NES partition.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

#include "thumbyone_handoff.h"   /* common library */

#ifndef SPIKE_SLOT_ID
#error "SPIKE_SLOT_ID must be 0 or 1"
#endif

#define PIN_BL       7
#define PIN_BTN_A    21
#define PIN_BTN_MENU 26

static bool btn_a_pressed(void)    { return !gpio_get(PIN_BTN_A); }
static bool btn_menu_pressed(void) { return !gpio_get(PIN_BTN_MENU); }


/* ==================================================================
 *                      SLOT 0 — LOBBY
 * ================================================================== */
#if SPIKE_SLOT_ID == 0

#include "lcd_gc9107.h"
#include "font.h"

#define COL_BG     0x0000
#define COL_TEXT   0xFFFF
#define COL_TITLE  0x07FF
#define COL_ACTION 0xF81F

static uint16_t g_fb[128 * 128] __attribute__((aligned(4)));

/* 4 KiB aligned workarea for rom_chain_image + rom_load_partition_table. */
static __attribute__((aligned(4))) uint8_t g_workarea[4 * 1024];

#define LOG_MAX_LINES 6
#define LOG_LINE_LEN  30
static char g_log[LOG_MAX_LINES][LOG_LINE_LEN];
static int  g_log_next = 0;

static void log_push(const char *s) {
    if (g_log_next >= LOG_MAX_LINES) {
        for (int i = 1; i < LOG_MAX_LINES; ++i)
            memcpy(g_log[i - 1], g_log[i], LOG_LINE_LEN);
        g_log_next = LOG_MAX_LINES - 1;
    }
    strncpy(g_log[g_log_next], s, LOG_LINE_LEN - 1);
    g_log[g_log_next][LOG_LINE_LEN - 1] = 0;
    g_log_next++;
}

static void render_screen(void) {
    for (int i = 0; i < 128 * 128; ++i) g_fb[i] = COL_BG;

    int w = nes_font_width_2x("SLOT 0");
    nes_font_draw_2x(g_fb, "SLOT 0", (128 - w) / 2, 8, COL_TITLE);
    w = nes_font_width_2x("(LOBBY)");
    nes_font_draw_2x(g_fb, "(LOBBY)", (128 - w) / 2, 22, COL_TITLE);

    for (int x = 4; x < 124; ++x) g_fb[38 * 128 + x] = COL_TEXT;

    for (int i = 0; i < g_log_next; ++i)
        nes_font_draw(g_fb, g_log[i], 2, 44 + i * 10, COL_TEXT);

    nes_font_draw(g_fb, "A=NES  MENU=reboot", 4, 120, COL_ACTION);
    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

int main(void) {
    /* Handoff check FIRST — before any peripheral init. If the
     * scratch magic is set, the library will chain into the
     * requested slot's partition and never return. */
    thumbyone_handoff_consume_if_present(g_workarea, sizeof(g_workarea));

    /* No handoff — normal lobby flow. */
    gpio_init(PIN_BTN_A);    gpio_set_dir(PIN_BTN_A, GPIO_IN);    gpio_pull_up(PIN_BTN_A);
    gpio_init(PIN_BTN_MENU); gpio_set_dir(PIN_BTN_MENU, GPIO_IN); gpio_pull_up(PIN_BTN_MENU);

    nes_lcd_init();
    nes_lcd_backlight(1);

    log_push("lobby booted");
    log_push("A    launch NES slot");
    log_push("MENU reboot lobby");
    render_screen();

    while (1) {
        if (btn_a_pressed()) {
            while (btn_a_pressed()) sleep_ms(10);
            sleep_ms(50);
            log_push("-> NES slot");
            render_screen();
            nes_lcd_wait_idle();
            thumbyone_handoff_request_slot(THUMBYONE_SLOT_NES);
            /* does not return */
        }
        if (btn_menu_pressed()) {
            while (btn_menu_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_lobby();
            /* does not return */
        }
        sleep_ms(20);
    }
}

#endif /* SPIKE_SLOT_ID == 0 */


/* ==================================================================
 *                      SLOT 1 — NES-partition placeholder
 * ================================================================== */
#if SPIKE_SLOT_ID == 1

int main(void) {
    /* Minimal — just blink the backlight, then wait for A to
     * return to the lobby. No LCD / SPI / DMA so the handoff
     * back stays simple. */
    gpio_init(PIN_BL);
    gpio_set_dir(PIN_BL, GPIO_OUT);
    gpio_put(PIN_BL, 0);

    gpio_init(PIN_BTN_A);
    gpio_set_dir(PIN_BTN_A, GPIO_IN);
    gpio_pull_up(PIN_BTN_A);

    /* Arrival knock — 3 slow pulses */
    for (int i = 0; i < 3; ++i) {
        gpio_put(PIN_BL, 1); sleep_ms(700);
        gpio_put(PIN_BL, 0); sleep_ms(400);
    }

    while (1) {
        if (btn_a_pressed()) {
            while (btn_a_pressed()) sleep_ms(10);
            sleep_ms(50);
            thumbyone_handoff_request_lobby();
            /* does not return */
        }
        gpio_put(PIN_BL, 1); sleep_ms(80);
        gpio_put(PIN_BL, 0); sleep_ms(80);
    }
}

#endif /* SPIKE_SLOT_ID == 1 */
