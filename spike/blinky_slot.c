/*
 * ThumbyOne flash-layout spike v2 — RP2350 partition-table edition.
 *
 * Architecture follows pico-examples/bootloaders/encrypted (the
 * canonical RP2350 multi-image pattern from Raspberry Pi):
 *
 *   1. A partition table (pt.json) is embedded in slot 0's binary,
 *      declaring partition "app" at 128K start, 256K size.
 *   2. Slot 0 (the "lobby") boots normally at 0x10000000. It:
 *        a. rom_load_partition_table(workarea, 4096, false)
 *        b. rom_get_partition_table_info(...)  — find partition 0's
 *           actual start/end sectors
 *        c. stdio_uart_deinit() (matches example preamble)
 *        d. rom_chain_image(workarea, 4096, start, size)
 *      On success rom_chain_image does not return; on failure we
 *      fall back to BOOTSEL via reset_usb_boot.
 *   3. Slot 1 lives inside the partition at flash offset 0x20000
 *      (XIP 0x10020000). It's a minimal blinky so we can confirm
 *      arrival without the SPI/DMA complications the earlier
 *      attempts brought in.
 *   4. Slot 1's A button does watchdog_reboot(0,0,0) — the same
 *      call ThumbyP8 uses and that we've verified works from
 *      slot 0 — to cold-reboot back into the lobby.
 *
 * The workarea is 4 KiB aligned in main SRAM, as pico-examples
 * does. The preamble is DELIBERATELY minimal (just stdio_uart_deinit)
 * — pico-examples doesn't do manual IRQ/DMA/flash teardown, so
 * neither do we.
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

/* Handoff: user-scratch register 0 in the watchdog block. Scratch
 * 0..3 are user-reserved and survive watchdog-induced resets
 * (bootrom uses scratch 4..7 for its own magic). If the lobby
 * finds this magic in scratch[0] on cold-boot, it takes the chain
 * path immediately — no peripheral init, just rom_chain_image to
 * the app partition. This is the classic "bootloader reads a
 * flag" pattern, required on RP2350 because bootrom always picks
 * the default-boot image (lobby at 0x10000000) over a partition
 * when both are valid — there's no way to force-boot a partition
 * through rom_reboot. */
#define HANDOFF_MAGIC_LAUNCH_APP   0xB007A990u

#ifndef SPIKE_SLOT_ID
#error "SPIKE_SLOT_ID must be 0 or 1"
#endif

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

#define COL_BG       0x0000
#define COL_TEXT     0xFFFF
#define COL_SLOT0    0x07FF  /* cyan */
#define COL_ACTION   0xF81F  /* magenta */
#define COL_ERROR    0xF800  /* red */

static uint16_t g_fb[128 * 128] __attribute__((aligned(4)));

/* Workarea for rom_load_partition_table / rom_chain_image — matches
 * the 4 KiB aligned layout in pico-examples/bootloaders/encrypted. */
static __attribute__((aligned(4))) uint8_t g_workarea[4 * 1024];

#define LOG_MAX_LINES 10
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
    nes_font_draw_2x(g_fb, "SLOT 0", (128 - w) / 2, 8, COL_SLOT0);
    w = nes_font_width_2x("(LOBBY)");
    nes_font_draw_2x(g_fb, "(LOBBY)", (128 - w) / 2, 22, COL_SLOT0);

    for (int x = 4; x < 124; ++x) g_fb[38 * 128 + x] = COL_TEXT;

    for (int i = 0; i < g_log_next; ++i) {
        nes_font_draw(g_fb, g_log[i], 2, 44 + i * 8, COL_TEXT);
    }
    nes_font_draw(g_fb, "A=chain  MENU=reset", 4, 120, COL_ACTION);

    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

static void log_and_render(const char *s) {
    log_push(s);
    render_screen();
}

/* Fire the handoff: set the magic in scratch[0] and reboot. The
 * lobby's early-boot code (before any peripheral init) will see
 * the magic on the subsequent cold boot and chain to the app. */
static void request_launch_app(void) {
    log_and_render("set handoff magic");
    log_and_render("rebooting...");
    sleep_ms(500);
    nes_lcd_wait_idle();

    /* Write the magic to all 4 user scratch registers so we can
     * tell on the next boot which ones survived the reboot. */
    watchdog_hw->scratch[0] = HANDOFF_MAGIC_LAUNCH_APP;
    watchdog_hw->scratch[1] = HANDOFF_MAGIC_LAUNCH_APP;
    watchdog_hw->scratch[2] = HANDOFF_MAGIC_LAUNCH_APP;
    watchdog_hw->scratch[3] = HANDOFF_MAGIC_LAUNCH_APP;
    stdio_uart_deinit();

    rom_reboot(
        REBOOT2_FLAG_REBOOT_TYPE_NORMAL
          | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
        10, 0, 0
    );

    /* Unreachable — but fall back to BOOTSEL if the reboot
     * somehow doesn't fire. */
    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

/* Called from main() IMMEDIATELY on cold boot, before any
 * peripheral init, if scratch[0] holds the launch-app magic.
 * Does the rom_chain_image dance from a pristine chip state —
 * the condition under which the bootrom chain call actually
 * works reliably. Never returns. */
static void early_launch_app(uint8_t *workarea_4k) {
    /* 1. Load the partition table into bootrom state. */
    int rc = rom_load_partition_table(workarea_4k, 4096, false);
    if (rc != 0) goto fail;

    /* 2. Look up the app partition's flash location. */
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

    /* 3. Chain to the partition. Should not return on success. */
    rc = rom_chain_image(workarea_4k, 4096, start, size);

fail:
    /* Chain or PT lookup failed — flash BL in an error pattern
     * and fall back to BOOTSEL so we don't soft-brick. */
    gpio_init(7);
    gpio_set_dir(7, GPIO_OUT);
    int count = rc < 0 ? -rc : (rc == 0 ? 99 : rc);
    if (count <= 0 || count > 32) count = 32;
    for (int pass = 0; pass < 3; ++pass) {
        gpio_put(7, 1); sleep_ms(1500);
        gpio_put(7, 0); sleep_ms(600);
        for (int i = 0; i < count; ++i) {
            gpio_put(7, 1); sleep_ms(250);
            gpio_put(7, 0); sleep_ms(350);
        }
        sleep_ms(1500);
    }
    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

int main(void) {
    /* HANDOFF CHECK + scratch-register diagnostic.
     *
     * We write the magic to all 4 user scratch registers in
     * request_launch_app; on boot, check which survived the
     * reboot. Report survivors as a BL pulse code so we can tell
     * which scratches rom_reboot preserves. */
    uint32_t m = HANDOFF_MAGIC_LAUNCH_APP;
    int surv = 0;
    if (watchdog_hw->scratch[0] == m) surv |= 1;
    if (watchdog_hw->scratch[1] == m) surv |= 2;
    if (watchdog_hw->scratch[2] == m) surv |= 4;
    if (watchdog_hw->scratch[3] == m) surv |= 8;

    if (surv != 0) {
        /* At least one survived. Clear all, then flash a
         * "survivors bitmap" as BL pulses (1-4 pulses). Then
         * proceed to chain. */
        for (int i = 0; i < 4; ++i) watchdog_hw->scratch[i] = 0;

        gpio_init(7); gpio_set_dir(7, GPIO_OUT);
        /* preamble: 2 s solid on */
        gpio_put(7, 1); sleep_ms(2000); gpio_put(7, 0); sleep_ms(500);
        /* survivors: N short pulses where N = 1..4 depending on
         * which scratch survived */
        int count = 0;
        if (surv & 1) count = 1;
        else if (surv & 2) count = 2;
        else if (surv & 4) count = 3;
        else if (surv & 8) count = 4;
        for (int i = 0; i < count; ++i) {
            gpio_put(7, 1); sleep_ms(200);
            gpio_put(7, 0); sleep_ms(200);
        }
        sleep_ms(800);

        early_launch_app(g_workarea);
        /* early_launch_app does not return. */
    }

    /* Normal lobby flow */
    gpio_init(PIN_BTN_A); gpio_set_dir(PIN_BTN_A, GPIO_IN); gpio_pull_up(PIN_BTN_A);
    gpio_init(PIN_BTN_MENU); gpio_set_dir(PIN_BTN_MENU, GPIO_IN); gpio_pull_up(PIN_BTN_MENU);

    nes_lcd_init();
    nes_lcd_backlight(1);

    log_push("slot 0 lobby booted");
    log_push("A = launch app");
    log_push("MENU = self reboot");
    render_screen();

    while (1) {
        if (btn_a_pressed()) {
            while (btn_a_pressed()) sleep_ms(10);
            sleep_ms(50);
            request_launch_app();
        }
        if (btn_menu_pressed()) {
            log_and_render("MENU: watchdog_reboot");
            sleep_ms(500);
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
    /* Minimal app — just BL blink + A-button handler. No LCD,
     * no SPI, no DMA. Arrival signal: a distinctive "knock" of
     * 3 long pulses, followed by a fast blink forever. */
    gpio_init(PIN_BL);    gpio_set_dir(PIN_BL, GPIO_OUT);    gpio_put(PIN_BL, 0);
    gpio_init(PIN_BTN_A); gpio_set_dir(PIN_BTN_A, GPIO_IN);  gpio_pull_up(PIN_BTN_A);

    /* Arrival knock: 3 long pulses */
    for (int i = 0; i < 3; ++i) {
        gpio_put(PIN_BL, 1); sleep_ms(700);
        gpio_put(PIN_BL, 0); sleep_ms(400);
    }

    /* Fast-blink loop with A polling */
    while (1) {
        if (btn_a_pressed()) {
            while (btn_a_pressed()) sleep_ms(10);
            sleep_ms(50);
            /* Cold reboot via BootROM normal path. Since the lobby
             * is the "absolute" binary at 0x10000000, BootROM
             * picks it up on normal boot. */
            watchdog_reboot(0, 0, 0);
            while (1) tight_loop_contents();
        }
        gpio_put(PIN_BL, 1); sleep_ms(80);
        gpio_put(PIN_BL, 0); sleep_ms(80);
    }
}

#endif /* SPIKE_SLOT_ID == 1 */
