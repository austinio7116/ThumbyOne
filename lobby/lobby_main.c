/*
 * ThumbyOne lobby — system selector.
 *
 * For now this is a minimal version that only knows about the
 * NES slot (step C1: prove that a real subproject firmware can
 * be chained to). A full-featured system selector — the one
 * PLAN.md describes — comes later in step B.
 *
 * BOOT FLOW
 *   main() first line: thumbyone_handoff_consume_if_present.
 *   If a scratch handoff is waiting, the library chains into the
 *   target slot from a pristine chip state and never returns.
 *   Otherwise main() proceeds with normal lobby init (LCD, UI).
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

#include "thumbyone_handoff.h"
#include "slot_layout.h"

#include "lcd_gc9107.h"
#include "font.h"

#define PIN_BL       7
#define PIN_BTN_A    21
#define PIN_BTN_B    25
#define PIN_BTN_MENU 26

#define COL_BG     0x0000
#define COL_TEXT   0xFFFF
#define COL_TITLE  0x07FF
#define COL_ACTION 0xF81F

static uint16_t g_fb[128 * 128] __attribute__((aligned(4)));

/* 4 KiB aligned workarea — used by rom_load_partition_table
 * and rom_chain_image inside the handoff library. */
static __attribute__((aligned(4))) uint8_t g_workarea[4 * 1024];

static bool btn_a_pressed(void)    { return !gpio_get(PIN_BTN_A); }
static bool btn_b_pressed(void)    { return !gpio_get(PIN_BTN_B); }
static bool btn_menu_pressed(void) { return !gpio_get(PIN_BTN_MENU); }

static void render_home(void) {
    for (int i = 0; i < 128 * 128; ++i) g_fb[i] = COL_BG;

    int w = nes_font_width_2x("THUMBY");
    nes_font_draw_2x(g_fb, "THUMBY", (128 - w) / 2, 8, COL_TITLE);
    w = nes_font_width_2x("ONE");
    nes_font_draw_2x(g_fb, "ONE", (128 - w) / 2, 22, COL_TITLE);

    for (int x = 4; x < 124; ++x) g_fb[38 * 128 + x] = COL_TEXT;

    nes_font_draw(g_fb, "system selector",  2, 46, COL_TEXT);
    nes_font_draw(g_fb, "",                 2, 56, COL_TEXT);
    nes_font_draw(g_fb, "A:    launch NES", 2, 66, COL_TEXT);
    nes_font_draw(g_fb, "B:    launch P8",  2, 76, COL_TEXT);
    nes_font_draw(g_fb, "MENU: reboot",     2, 86, COL_TEXT);

    nes_font_draw(g_fb, "(C2a placeholder)", 4, 120, COL_ACTION);

    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

int main(void) {
    /* Handoff check BEFORE any peripheral init — required for
     * rom_chain_image to work reliably on RP2350. If the scratch
     * magic is set, the library chains into the target slot's
     * partition and never returns. */
    thumbyone_handoff_consume_if_present(g_workarea, sizeof(g_workarea));

    /* Normal lobby flow — no handoff pending. */
    gpio_init(PIN_BTN_A);    gpio_set_dir(PIN_BTN_A, GPIO_IN);    gpio_pull_up(PIN_BTN_A);
    gpio_init(PIN_BTN_B);    gpio_set_dir(PIN_BTN_B, GPIO_IN);    gpio_pull_up(PIN_BTN_B);
    gpio_init(PIN_BTN_MENU); gpio_set_dir(PIN_BTN_MENU, GPIO_IN); gpio_pull_up(PIN_BTN_MENU);

    nes_lcd_init();
    nes_lcd_backlight(1);
    render_home();

    while (1) {
        if (btn_a_pressed()) {
            while (btn_a_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_slot(THUMBYONE_SLOT_NES);
            /* does not return */
        }
        if (btn_b_pressed()) {
            while (btn_b_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            thumbyone_handoff_request_slot(THUMBYONE_SLOT_P8);
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
