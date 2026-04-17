/*
 * ThumbyOne flash-layout spike — RP2350 multi-slot boot.
 *
 * Proves the architecture ThumbyOne needs: two independent
 * firmwares coexist in flash, and the "lobby" hands control to
 * a second slot via BootROM, which can hand control back via
 * a plain watchdog reset.
 *
 * FLOW
 *   cold boot             → lobby (full LCD + UI)
 *   lobby A press         → sets magic in watchdog scratch[0],
 *                           rom_reboot(NORMAL) for full reset
 *   post-reboot lobby     → detects scratch magic BEFORE any
 *                           peripheral init, calls rom_chain_image
 *                           from pristine chip state → app
 *   app A press           → watchdog_reboot(0,0,0) → cold boot
 *                           → lobby (no magic, normal flow)
 *
 * WHY THE HANDOFF DANCE
 *   On RP2350, rom_chain_image only works reliably from a
 *   chip state that hasn't touched LCD / SPI / DMA. A direct
 *   call from an initialized lobby silently hangs in the
 *   bootrom. The scratch-based handoff gets us back to
 *   "pristine post-bootrom" state before the chain call.
 *
 *   Scratch[0..3] are reserved for user data and survive
 *   rom_reboot on RP2350 (verified on device).
 *
 * WHY APP IS LINKED AT 0x10000000
 *   rom_chain_image sets up QMI ATRANS to remap the XIP window
 *   to the partition's physical flash offset. The image's
 *   absolute code references therefore must be at the default
 *   flash origin (0x10000000). The UF2 combiner rebases the
 *   app's block target addresses to 0x10020000 so the bytes
 *   physically land inside partition 0.
 *
 * Reference pattern: pico-examples/bootloaders/encrypted.
 * Partition table format: see pt.json.
 */

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include <stdio.h>
#include <string.h>

#ifndef SPIKE_SLOT_ID
#error "SPIKE_SLOT_ID must be 0 or 1"
#endif

/* Magic written to watchdog_hw->scratch[0] by the lobby to tell
 * its post-reboot self "launch the app". Clears before chain. */
#define HANDOFF_MAGIC_LAUNCH_APP   0xB007A990u

#define PIN_BL       7
#define PIN_BTN_A    21
#define PIN_BTN_MENU 26

static bool btn_a_pressed(void)    { return !gpio_get(PIN_BTN_A); }
static bool btn_menu_pressed(void) { return !gpio_get(PIN_BTN_MENU); }


/* ==================================================================
 *                          SLOT 0 — LOBBY
 * ================================================================== */
#if SPIKE_SLOT_ID == 0

#include "lcd_gc9107.h"
#include "font.h"

#define COL_BG     0x0000
#define COL_TEXT   0xFFFF
#define COL_TITLE  0x07FF  /* cyan */
#define COL_ACTION 0xF81F  /* magenta */

static uint16_t g_fb[128 * 128] __attribute__((aligned(4)));

/* 4 KiB aligned workarea — the size pico-examples uses for
 * rom_load_partition_table + rom_chain_image. */
static __attribute__((aligned(4))) uint8_t g_workarea[4 * 1024];

#define LOG_MAX_LINES 8
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

    for (int i = 0; i < g_log_next; ++i) {
        nes_font_draw(g_fb, g_log[i], 2, 44 + i * 10, COL_TEXT);
    }
    nes_font_draw(g_fb, "A=launch MENU=reset", 4, 120, COL_ACTION);

    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

static void log_and_render(const char *s) { log_push(s); render_screen(); }

/* Post-reboot entry point — called from main() BEFORE any
 * peripheral init, when the scratch magic is present. Chains
 * into the app at partition 0. Never returns on success; on
 * failure falls back to BOOTSEL so we don't soft-brick. */
static void early_launch_app(uint8_t *workarea_4k) {
    int rc = rom_load_partition_table(workarea_4k, 4096, false);
    if (rc != 0) goto fail;

    uint32_t info[8];
    rc = rom_get_partition_table_info(info, sizeof(info) / 4,
            PT_INFO_PARTITION_LOCATION_AND_FLAGS
          | PT_INFO_SINGLE_PARTITION
          | (0 << 24));
    if (rc != 3) goto fail;

    uint32_t locperm = info[1];
    uint32_t first_sector = (locperm & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS)
                            >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    uint32_t last_sector  = (locperm & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)
                            >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    uint32_t start = XIP_BASE + first_sector * 0x1000;
    uint32_t size  = (last_sector - first_sector + 1) * 0x1000;

    rom_chain_image(workarea_4k, 4096, start, size);
    /* rom_chain_image does not return on success. */

fail:
    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

/* Lobby A-button action: stash the magic in scratch[0] and
 * full-reset. The post-reboot early_launch_app path takes over. */
static void request_launch_app(void) {
    log_and_render("A pressed -> reboot");
    log_and_render("(launching app...)");
    sleep_ms(400);
    nes_lcd_wait_idle();

    watchdog_hw->scratch[0] = HANDOFF_MAGIC_LAUNCH_APP;
    stdio_uart_deinit();

    rom_reboot(
        REBOOT2_FLAG_REBOOT_TYPE_NORMAL
          | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
        10, 0, 0
    );

    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

int main(void) {
    /* Handoff check FIRST, before any peripheral init: if the
     * scratch magic is present, we're here because the lobby
     * just rebooted itself to launch the app. Clear the magic
     * and chain from a pristine chip state. */
    if (watchdog_hw->scratch[0] == HANDOFF_MAGIC_LAUNCH_APP) {
        watchdog_hw->scratch[0] = 0;
        early_launch_app(g_workarea);
        /* unreachable */
    }

    /* Normal lobby flow */
    gpio_init(PIN_BTN_A);    gpio_set_dir(PIN_BTN_A, GPIO_IN);    gpio_pull_up(PIN_BTN_A);
    gpio_init(PIN_BTN_MENU); gpio_set_dir(PIN_BTN_MENU, GPIO_IN); gpio_pull_up(PIN_BTN_MENU);

    nes_lcd_init();
    nes_lcd_backlight(1);

    log_push("lobby booted");
    log_push("A    launch app");
    log_push("MENU reboot lobby");
    render_screen();

    while (1) {
        if (btn_a_pressed()) {
            while (btn_a_pressed()) sleep_ms(10);
            sleep_ms(50);
            request_launch_app();
        }
        if (btn_menu_pressed()) {
            while (btn_menu_pressed()) sleep_ms(10);
            sleep_ms(50);
            nes_lcd_wait_idle();
            watchdog_reboot(0, 0, 0);
            while (1) tight_loop_contents();
        }
        sleep_ms(20);
    }
}

#endif /* SPIKE_SLOT_ID == 0 */


/* ==================================================================
 *                          SLOT 1 — APP
 * ================================================================== */
#if SPIKE_SLOT_ID == 1

int main(void) {
    /* Minimal app — backlight pulse on arrival, then fast blink
     * until the user presses A. Doesn't touch the LCD, so the
     * panel keeps showing whatever the lobby rendered last.
     * That's a feature for the spike: the lobby text + flashing
     * BL is an unmistakable "app is running" signal. */
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
            /* Plain reboot — bootrom picks up the lobby image at
             * 0x10000000 on normal boot (no magic in scratch). */
            watchdog_reboot(0, 0, 0);
            while (1) tight_loop_contents();
        }
        gpio_put(PIN_BL, 1); sleep_ms(80);
        gpio_put(PIN_BL, 0); sleep_ms(80);
    }
}

#endif /* SPIKE_SLOT_ID == 1 */
