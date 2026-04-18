/*
 * ThumbyOne MPY-slot C picker — hero view.
 *
 * Self-contained: initialises LCD, buttons, and a FAT mount on
 * the shared volume. Scans /games/<name>/main.py to build a list,
 * renders a one-per-screen hero view (big icon + title + blurb),
 * and on A-press writes the chosen directory path to /.active_game
 * before unmounting. MicroPython's _boot_fat.py then mounts the
 * same FAT, and the frozen thumbyone_launcher.py reads the file
 * and execs the game.
 *
 * Per-game assets used by the hero view:
 *   /games/<name>/icon.bmp                — 16 bpp, any size up to 64x64
 *   /games/<name>/arcade_description.txt  — first line is the title,
 *                                            remaining lines are the
 *                                            description blurb.
 * Both are optional; the picker falls back to the directory name
 * for the title and a placeholder rectangle for the icon when an
 * asset is missing or malformed.
 *
 * Keeping the picker in C (not Python) means the LCD lights up
 * the instant the slot chains from the lobby, with no MicroPython
 * startup latency.
 */
#include "picker.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include "lcd_gc9107.h"
#include "font.h"
#include "picker_bmp.h"
#include "ff.h"
#include "thumbyone_fs.h"
#include "thumbyone_handoff.h"

/* --- button pins ----------------------------------------------------
 * Matches engine_io_rp3.h so the C picker and the engine agree on
 * the board's wiring. GP4 is NOT a button — it's the LCD RST pin
 * (see lcd_gc9107.c) — claiming it here would fight the LCD
 * driver and leave the panel blank. */
#define PIN_LEFT        0
#define PIN_UP          1
#define PIN_RIGHT       2
#define PIN_DOWN        3
#define PIN_LB          6
#define PIN_A          21
#define PIN_RB         22
#define PIN_B          25
#define PIN_MENU       26

/* --- palette ------------------------------------------------------- */
#define COL_BG      0x0000
#define COL_PANEL   0x10A2   /* dark blue-grey for hero card   */
#define COL_TITLE   0x07FF   /* cyan for the "MPY" banner       */
#define COL_FG      0xFFFF   /* pure white for the game title   */
#define COL_TEXT    0xDEFB   /* slightly dim off-white for body */
#define COL_DIM     0x8410   /* grey for footers / hints        */
#define COL_ACCENT  0xFFE0   /* yellow for the position counter */
#define COL_WARN    0xFC00   /* orange — MENU hold hint         */
#define COL_ERR     0xF800   /* red — FS error screen           */

/* --- buffers ------------------------------------------------------- */
static uint16_t g_fb[128 * 128] __attribute__((aligned(4)));

/* One decoded icon lives here. Re-filled whenever the selection
 * changes. 64x64 is the canonical icon size we designed the layout
 * around; smaller icons (e.g. 38x38) get centred inside the slot. */
#define ICON_MAX    64
static uint16_t g_icon_px[ICON_MAX * ICON_MAX] __attribute__((aligned(4)));
static int      g_icon_w;     /* 0 when no icon is loaded */
static int      g_icon_h;

/* Description-blurb storage. We read up to DESC_CAP bytes from
 * arcade_description.txt and split it into lines in place (NUL-
 * terminating each one). The first non-empty line is the title,
 * the rest are the body; blank separators are collapsed. */
#define DESC_CAP       512
#define DESC_MAX_LINES 8
static char   g_desc_buf[DESC_CAP + 1];
static char  *g_title;
static char  *g_body[DESC_MAX_LINES];
static int    g_body_count;

/* --- game list ----------------------------------------------------- */
#define MAX_GAMES        32
#define MAX_NAME_LEN     32
#define PICKER_PATH_CAP  64

typedef struct {
    char name[MAX_NAME_LEN + 1];
    char path[PICKER_PATH_CAP];   /* "/games/<name>" */
} picker_game_t;

static picker_game_t g_games[MAX_GAMES];
static int g_game_count = 0;

/* --- button helpers ----------------------------------------------- */
static bool btn(uint pin) { return !gpio_get(pin); }

static void buttons_init(void) {
    const uint pins[] = {
        PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT,
        PIN_LB, PIN_A, PIN_RB, PIN_B, PIN_MENU,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
}

static bool just_pressed(uint pin, bool *prev) {
    bool now = btn(pin);
    bool edge = now && !*prev;
    *prev = now;
    return edge;
}

/* --- screen helpers ----------------------------------------------- */
static void fb_fill(uint16_t c) {
    for (int i = 0; i < 128 * 128; ++i) g_fb[i] = c;
}

static void fb_rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 128) w = 128 - x;
    if (y + h > 128) h = 128 - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) {
        uint16_t *row = g_fb + (y + yy) * 128 + x;
        for (int xx = 0; xx < w; ++xx) row[xx] = c;
    }
}

/* Blit an uint16_t source image at (dst_x, dst_y) with clipping. */
static void fb_blit(const uint16_t *src, int sw, int sh, int dst_x, int dst_y) {
    for (int y = 0; y < sh; ++y) {
        int dy = dst_y + y;
        if (dy < 0 || dy >= 128) continue;
        for (int x = 0; x < sw; ++x) {
            int dx = dst_x + x;
            if (dx < 0 || dx >= 128) continue;
            g_fb[dy * 128 + dx] = src[y * sw + x];
        }
    }
}

static void present_blocking(void) {
    nes_lcd_present(g_fb);
    nes_lcd_wait_idle();
}

/* --- /games/ scan -------------------------------------------------- */

static int has_main_py(const char *game_name) {
    size_t name_len = strlen(game_name);
    if (name_len > MAX_NAME_LEN) return 0;
    char path[7 + MAX_NAME_LEN + 8 + 1];
    memcpy(path, "/games/", 7);
    memcpy(path + 7, game_name, name_len);
    memcpy(path + 7 + name_len, "/main.py", 9);
    FILINFO fno;
    return (f_stat(path, &fno) == FR_OK) ? 1 : 0;
}

static void sort_games(void) {
    for (int i = 0; i < g_game_count - 1; ++i) {
        for (int j = 0; j < g_game_count - 1 - i; ++j) {
            if (strcasecmp(g_games[j].name, g_games[j + 1].name) > 0) {
                picker_game_t tmp = g_games[j];
                g_games[j] = g_games[j + 1];
                g_games[j + 1] = tmp;
            }
        }
    }
}

static int scan_games(void) {
    g_game_count = 0;
    DIR dir;
    FRESULT r = f_opendir(&dir, "/games");
    if (r == FR_NO_PATH || r == FR_NO_FILE) return 0;
    if (r != FR_OK) return -1;

    FILINFO fno;
    while (g_game_count < MAX_GAMES) {
        r = f_readdir(&dir, &fno);
        if (r != FR_OK || fno.fname[0] == 0) break;
        if (!(fno.fattrib & AM_DIR)) continue;
        if (fno.fname[0] == '.') continue;
        size_t name_len = strlen(fno.fname);
        if (name_len == 0 || name_len > MAX_NAME_LEN) continue;
        if (!has_main_py(fno.fname)) continue;

        picker_game_t *g = &g_games[g_game_count++];
        memcpy(g->name, fno.fname, name_len);
        g->name[name_len] = '\0';
        memcpy(g->path, "/games/", 7);
        memcpy(g->path + 7, g->name, name_len);
        g->path[7 + name_len] = '\0';
    }
    f_closedir(&dir);
    sort_games();
    return 0;
}

/* --- per-game asset load ------------------------------------------- */

/* Build "/games/<name>/<file>" into out. Returns 0 on success,
 * -1 if the buffer is too small. */
static int build_asset_path(char *out, size_t cap,
                            const char *game_path, const char *leaf) {
    size_t gp_len   = strlen(game_path);
    size_t leaf_len = strlen(leaf);
    /* game_path + '/' + leaf + NUL */
    if (gp_len + 1 + leaf_len + 1 > cap) return -1;
    memcpy(out, game_path, gp_len);
    out[gp_len] = '/';
    memcpy(out + gp_len + 1, leaf, leaf_len + 1);
    return 0;
}

/* Load /games/<name>/icon.bmp into g_icon_px. On failure, sets
 * g_icon_w/h to 0 — render_hero draws a placeholder in that case. */
static void load_icon(const char *game_path) {
    g_icon_w = 0;
    g_icon_h = 0;
    char path[PICKER_PATH_CAP + 16];
    if (build_asset_path(path, sizeof(path), game_path, "icon.bmp") < 0) return;
    int w = 0, h = 0;
    if (thumbyone_picker_bmp_load(path, g_icon_px, ICON_MAX * ICON_MAX, &w, &h) == 0) {
        g_icon_w = w;
        g_icon_h = h;
    }
}

/* Strip trailing whitespace (CR/LF/spaces) in place and return the
 * trimmed length. */
static size_t rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        s[--n] = '\0';
    }
    return n;
}

/* Load /games/<name>/arcade_description.txt into g_desc_buf and
 * split it into g_title + g_body[]. Falls back to the directory
 * leaf when the file is missing / empty. Body lines are collapsed
 * so blank separators don't eat our limited row budget. */
static void load_description(const picker_game_t *g) {
    g_title = (char *)g->name;
    g_body_count = 0;

    char path[PICKER_PATH_CAP + 32];
    if (build_asset_path(path, sizeof(path), g->path, "arcade_description.txt") < 0) {
        return;
    }

    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return;
    UINT br = 0;
    if (f_read(&f, g_desc_buf, DESC_CAP, &br) != FR_OK) {
        f_close(&f);
        return;
    }
    f_close(&f);
    g_desc_buf[br] = '\0';

    /* Walk the buffer, splitting on '\n'. Each line gets NUL-
     * terminated in place. First non-empty line is the title;
     * remaining non-empty lines are body. */
    char *title = NULL;
    char *p = g_desc_buf;
    while (*p && g_body_count < DESC_MAX_LINES) {
        char *line = p;
        while (*p && *p != '\n') ++p;
        if (*p == '\n') { *p = '\0'; ++p; }
        rtrim(line);
        if (*line == '\0') continue;
        if (!title) {
            title = line;
        } else {
            g_body[g_body_count++] = line;
        }
    }
    if (title) g_title = title;
}

static void load_selection_assets(int sel) {
    if (sel < 0 || sel >= g_game_count) {
        g_icon_w = g_icon_h = 0;
        g_title = NULL;
        g_body_count = 0;
        return;
    }
    load_icon(g_games[sel].path);
    load_description(&g_games[sel]);
}

/* --- rendering ----------------------------------------------------- */

/* Minimal word-wrap: split `s` into non-overflowing chunks by
 * pushing whole words into `out` up to `max_px`. Each chunk is
 * emitted to a callback so we don't need a temp array of lines.
 * Words longer than max_px are hard-wrapped at the pixel boundary. */
typedef void (*line_draw_fn)(const char *line, int y, void *user);

static void wrap_draw(const char *s, int max_px, int line_h,
                      int y_start, int max_lines,
                      line_draw_fn draw, void *user) {
    if (!s || !*s) return;
    char tmp[64];
    int lines_drawn = 0;
    const char *cur = s;

    while (*cur && lines_drawn < max_lines) {
        /* Find the longest prefix that fits, preferring a space
         * break. We never exceed `sizeof(tmp)-1` characters either. */
        int best_break = 0;
        int i = 0;
        int last_space = -1;
        while (cur[i] && i < (int)sizeof(tmp) - 1) {
            if (cur[i] == ' ') last_space = i;
            tmp[i] = cur[i];
            tmp[i + 1] = '\0';
            if (nes_font_width(tmp) > max_px) {
                /* Overflowed — back off to the last word boundary. */
                if (last_space > 0) {
                    best_break = last_space;
                } else {
                    best_break = i;   /* no space: hard-wrap */
                }
                break;
            }
            ++i;
        }
        if (cur[i] == '\0') best_break = i;   /* end of string fit */

        /* Emit a NUL-terminated slice. */
        char saved = tmp[best_break];
        tmp[best_break] = '\0';
        rtrim(tmp);
        draw(tmp, y_start + lines_drawn * line_h, user);
        tmp[best_break] = saved;
        ++lines_drawn;

        cur += best_break;
        while (*cur == ' ') ++cur;      /* skip spaces at break  */
    }
}

static void draw_body_line(const char *line, int y, void *user) {
    (void)user;
    nes_font_draw(g_fb, line, 4, y, COL_TEXT);
}

/* Placeholder for missing icon.bmp — a soft gradient square with the
 * first letter of the name. Keeps the hero layout balanced. */
static void render_icon_placeholder(int x, int y, int side, char init) {
    fb_rect(x, y, side, side, COL_PANEL);
    /* Subtle outline. */
    for (int i = 0; i < side; ++i) {
        g_fb[y * 128 + (x + i)] = COL_DIM;
        g_fb[(y + side - 1) * 128 + (x + i)] = COL_DIM;
        g_fb[(y + i) * 128 + x] = COL_DIM;
        g_fb[(y + i) * 128 + (x + side - 1)] = COL_DIM;
    }
    char s[2] = { init ? init : '?', 0 };
    int sw = nes_font_width_2x(s);
    nes_font_draw_2x(g_fb, s, x + (side - sw) / 2, y + (side - 10) / 2, COL_FG);
}

static void render_empty(void) {
    fb_fill(COL_BG);
    int w = nes_font_width_2x("MPY");
    nes_font_draw_2x(g_fb, "MPY", (128 - w) / 2, 10, COL_TITLE);
    for (int x = 4; x < 124; ++x) g_fb[30 * 128 + x] = COL_DIM;

    nes_font_draw(g_fb, "no games found",    14, 48, COL_FG);
    nes_font_draw(g_fb, "drop main.py into", 10, 66, COL_DIM);
    nes_font_draw(g_fb, "/games/<name>/",    18, 76, COL_DIM);
    nes_font_draw(g_fb, "via the lobby usb", 10, 86, COL_DIM);
    nes_font_draw(g_fb, "drive",             48, 96, COL_DIM);

    nes_font_draw(g_fb, "MENU: back", 34, 116, COL_WARN);
    present_blocking();
}

static void render_error(const char *line1, const char *line2) {
    fb_fill(COL_BG);
    int w = nes_font_width_2x("FS ERR");
    nes_font_draw_2x(g_fb, "FS ERR", (128 - w) / 2, 30, COL_ERR);
    if (line1) nes_font_draw(g_fb, line1, (128 - nes_font_width(line1)) / 2, 60, COL_FG);
    if (line2) nes_font_draw(g_fb, line2, (128 - nes_font_width(line2)) / 2, 72, COL_FG);
    nes_font_draw(g_fb, "MENU: lobby", 30, 116, COL_WARN);
    present_blocking();
}

static void render_hero(int sel) {
    fb_fill(COL_BG);

    /* Top strip — "MPY" banner left, "N/M" counter right. Format
     * the counter by hand; MAX_GAMES is 32 so two decimal digits
     * each are plenty, and GCC's format-truncation analyser won't
     * let snprintf run with the narrow buffer we'd want. */
    nes_font_draw(g_fb, "MPY", 4, 4, COL_TITLE);
    char pos[12];
    int cur = sel + 1;
    int tot = g_game_count;
    size_t pi = 0;
    if (cur >= 10) pos[pi++] = '0' + (cur / 10);
    pos[pi++] = '0' + (cur % 10);
    pos[pi++] = '/';
    if (tot >= 10) pos[pi++] = '0' + (tot / 10);
    pos[pi++] = '0' + (tot % 10);
    pos[pi] = '\0';
    int pw = nes_font_width(pos);
    nes_font_draw(g_fb, pos, 128 - pw - 4, 4, COL_ACCENT);
    /* Thin divider under the top strip. */
    for (int x = 2; x < 126; ++x) g_fb[14 * 128 + x] = COL_DIM;

    /* 64x64 icon slot centred at (x=32, y=18). Smaller icons
     * are centred inside that slot so the layout stays stable. */
    const int slot_x = 32;
    const int slot_y = 18;
    const int slot_s = 64;

    if (g_icon_w > 0 && g_icon_h > 0) {
        int ox = slot_x + (slot_s - g_icon_w) / 2;
        int oy = slot_y + (slot_s - g_icon_h) / 2;
        fb_blit(g_icon_px, g_icon_w, g_icon_h, ox, oy);
    } else {
        render_icon_placeholder(slot_x, slot_y, slot_s,
                                g_title ? g_title[0] : '?');
    }

    /* Prev / next arrow hints flanking the icon. */
    if (g_game_count > 1) {
        if (sel > 0)                nes_font_draw(g_fb, "<", 2,        slot_y + 28, COL_DIM);
        if (sel < g_game_count - 1) nes_font_draw(g_fb, ">", 128 - 6,  slot_y + 28, COL_DIM);
    }

    /* Title in 2x font below the icon. If it overflows the
     * screen, truncate with an ellipsis — marquee would be nicer
     * but adds per-frame state we don't need yet. */
    const char *title = g_title ? g_title : g_games[sel].name;
    char title_buf[40];
    strncpy(title_buf, title, sizeof(title_buf) - 1);
    title_buf[sizeof(title_buf) - 1] = '\0';
    /* Chop glyphs from the end until we fit 124 px. */
    while (nes_font_width_2x(title_buf) > 124 && strlen(title_buf) > 1) {
        title_buf[strlen(title_buf) - 1] = '\0';
    }
    int tw = nes_font_width_2x(title_buf);
    int title_y = slot_y + slot_s + 4;    /* = 86 */
    nes_font_draw_2x(g_fb, title_buf, (128 - tw) / 2, title_y, COL_FG);

    /* Blurb: up to 3 lines of the description, wrapped to 120 px.
     * We concatenate the stored body lines with spaces so the
     * wrap algorithm sees one flowing paragraph and doesn't waste
     * rows on short source lines. */
    int blurb_y = title_y + 12;           /* = 98 */
    int blurb_max_y = 118;
    int blurb_rows = (blurb_max_y - blurb_y) / 7;
    if (blurb_rows > 3) blurb_rows = 3;

    if (g_body_count > 0 && blurb_rows > 0) {
        char blurb[256];
        size_t pos_blurb = 0;
        for (int i = 0; i < g_body_count && pos_blurb < sizeof(blurb) - 1; ++i) {
            const char *l = g_body[i];
            size_t ln = strlen(l);
            if (pos_blurb > 0 && pos_blurb < sizeof(blurb) - 1) {
                blurb[pos_blurb++] = ' ';
            }
            if (pos_blurb + ln >= sizeof(blurb)) ln = sizeof(blurb) - 1 - pos_blurb;
            memcpy(blurb + pos_blurb, l, ln);
            pos_blurb += ln;
        }
        blurb[pos_blurb] = '\0';
        wrap_draw(blurb, 120, 7, blurb_y, blurb_rows,
                  draw_body_line, NULL);
    }

    /* Footer — hint strip. */
    nes_font_draw(g_fb, "A launch  MENU lobby", 4, 121, COL_DIM);

    present_blocking();
}

/* --- selection write ----------------------------------------------- */

static int write_active_game(const char *path) {
    FIL f;
    FRESULT r = f_open(&f, "/.active_game", FA_CREATE_ALWAYS | FA_WRITE);
    if (r != FR_OK) return -1;
    UINT written;
    r = f_write(&f, path, (UINT)strlen(path), &written);
    f_close(&f);
    return (r == FR_OK && written == strlen(path)) ? 0 : -1;
}

/* --- main loop ----------------------------------------------------- */

static bool menu_hold_for_lobby(void) {
    const int HOLD_MS = 800;
    absolute_time_t deadline = make_timeout_time_ms(HOLD_MS);
    while (btn(PIN_MENU)) {
        if (time_reached(deadline)) return true;
        sleep_ms(10);
    }
    return false;
}

int thumbyone_picker_run(void) {
    set_sys_clock_khz(250000, true);
    stdio_init_all();

    nes_lcd_init();
    nes_lcd_backlight(1);
    buttons_init();

    FATFS g_fs;
    FRESULT r = thumbyone_fs_mount(&g_fs);
    if (r != FR_OK) {
        render_error("mount failed", "go to lobby, wipe");
        while (1) {
            if (btn(PIN_MENU) && menu_hold_for_lobby()) {
                thumbyone_handoff_request_lobby();
            }
            sleep_ms(20);
        }
    }

    if (scan_games() < 0) {
        render_error("scan failed", "corrupt /games/?");
        while (1) {
            if (btn(PIN_MENU) && menu_hold_for_lobby()) {
                thumbyone_handoff_request_lobby();
            }
            sleep_ms(20);
        }
    }

    if (g_game_count == 0) {
        render_empty();
        while (1) {
            if (btn(PIN_MENU) && menu_hold_for_lobby()) {
                thumbyone_handoff_request_lobby();
            }
            sleep_ms(20);
        }
    }

    int sel = 0;
    load_selection_assets(sel);
    render_hero(sel);

    bool prev_up = false, prev_down = false;
    bool prev_left = false, prev_right = false;
    bool prev_a = false, prev_menu = false;

    while (1) {
        bool dirty = false;

        /* Up/down AND left/right all step through games — either
         * D-pad axis feels natural on a hero view. */
        if (just_pressed(PIN_UP, &prev_up) ||
            just_pressed(PIN_LEFT, &prev_left)) {
            sel = (sel > 0) ? sel - 1 : g_game_count - 1;
            dirty = true;
        }
        if (just_pressed(PIN_DOWN, &prev_down) ||
            just_pressed(PIN_RIGHT, &prev_right)) {
            sel = (sel + 1) % g_game_count;
            dirty = true;
        }

        if (just_pressed(PIN_A, &prev_a)) {
            const char *chosen = g_games[sel].path;
            if (write_active_game(chosen) < 0) {
                render_error("write failed", "flash locked?");
                sleep_ms(2000);
                render_hero(sel);
                continue;
            }
            f_unmount("");
            nes_lcd_wait_idle();
            nes_lcd_teardown();
            return 0;
        }

        if (just_pressed(PIN_MENU, &prev_menu)) {
            if (menu_hold_for_lobby()) {
                f_unmount("");
                nes_lcd_wait_idle();
                thumbyone_handoff_request_lobby();
            }
        }

        if (dirty) {
            load_selection_assets(sel);
            render_hero(sel);
        }
        sleep_ms(20);
    }
}
