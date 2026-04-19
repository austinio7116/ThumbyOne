/*
 * ThumbyOne MPY-slot C picker — hero view + menu overlay.
 *
 * Runs before MicroPython is initialised. Mounts the shared FAT,
 * scans /games/<name>/main.py, and renders a single-game hero view
 * with icon + title + description. MENU opens a menu overlay with
 * battery / disk / firmware info, favourite toggle, sort-order
 * cycling, and the return-to-lobby action.
 *
 * On launch (A), writes the chosen directory to /.active_game and
 * tears down the LCD + SPI/DMA so the Tiny Game Engine can claim
 * them cleanly when the game does `import engine_main`.
 *
 * Per-game assets (all optional):
 *   /games/<name>/icon.bmp                — 16 bpp RGB565 up to 64x64
 *   /games/<name>/arcade_description.txt  — line 1 = title, rest =
 *                                            body; optional "Author: X"
 *                                            line used for sort order.
 *
 * Persisted picker state (under the shared FAT):
 *   /.favs           — newline-separated list of favourite dir names
 *   /.picker_sort    — single byte: sort mode index
 *   /.active_game    — the chosen /games/<name> path (consumed by
 *                       thumbyone_launcher.py after mp_init)
 */
#include "picker.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"

#include "lcd_gc9107.h"
#include "font.h"
#include "picker_bmp.h"
#include "menu_watchdog.h"
#include "ff.h"
#include "thumbyone_fs.h"
#include "thumbyone_handoff.h"

/* --- build-time firmware identity -------------------------------- */
/* THUMBYONE_FW_VERSION is optionally set by the parent CMake via
 * -DTHUMBYONE_FW_VERSION=\"abc1234\". Fallback is a short marker so
 * the info strip always has something to show. */
#ifndef THUMBYONE_FW_VERSION
#define THUMBYONE_FW_VERSION "1.03"
#endif

/* --- button pins (match engine_io_rp3.h) ------------------------- */
#define PIN_LEFT        0
#define PIN_UP          1
#define PIN_RIGHT       2
#define PIN_DOWN        3
#define PIN_LB          6
#define PIN_A          21
#define PIN_RB         22
#define PIN_B          25
#define PIN_MENU       26

/* --- palette ----------------------------------------------------- *
 * Menu-overlay colours deliberately mirror ThumbyNES's nes_menu.c so
 * both slots feel identical from inside the overlay: orange title
 * bar, green cursor row, green progress-bar fill.
 */
#define COL_BG       0x0000
#define COL_PANEL    0x10A2   /* dark blue-grey for hero card    */
#define COL_PANEL_HL 0x20C5   /* hero-view highlight row         */
#define COL_HEAD     0x07FF   /* cyan for the "MPY" hero banner  */
#define COL_FG       0xFFFF   /* pure white for game title       */
#define COL_TEXT     0xDEFB   /* dim off-white for body          */
#define COL_DIM      0x8410   /* grey for footers / hints        */
#define COL_DARK     0x4208   /* very dim grey                   */
#define COL_ACCENT   0xFFE0   /* yellow: pos counter + favourite */
#define COL_HIGHLT   0x07E0   /* green: cursor row + progress    */
#define COL_TITLE    0xFD20   /* orange: menu title bar          */
#define COL_HL_BG    0x0220   /* dim-green cursor row background */
#define COL_BAR_BG   0x39E7   /* progress-bar track background   */
#define COL_WARN     0xFC00   /* orange — MENU hold hint         */
#define COL_ERR      0xF800   /* red — FS error screen           */

/* --- buffers ----------------------------------------------------- */
/* These large picker-only buffers live in the .picker_scratch linker
 * section (defined in memmap_mp_rp2350.ld), which is positioned at
 * the start of the MicroPython GC heap range. They hold data only
 * during the pre-mp_init picker; once gc_init() claims the heap,
 * these bytes are reclaimed as free memory with no code change
 * needed. This saves ~42 KB of otherwise-wasted BSS in the MPY slot.
 * Non-slot builds put nothing in this section, so it's a no-op
 * for standalone MicroPython. */
#define PICKER_SCRATCH_ATTR \
    __attribute__((section(".picker_scratch"), aligned(4)))

static uint16_t g_fb[128 * 128] PICKER_SCRATCH_ATTR;

#define ICON_MAX    64
static uint16_t g_icon_px[ICON_MAX * ICON_MAX] PICKER_SCRATCH_ATTR;
static int      g_icon_w;
static int      g_icon_h;

/* Description storage: full raw text, plus parsed pointers. */
#define DESC_CAP       512
#define DESC_MAX_LINES 8
static char   g_desc_buf[DESC_CAP + 1];
static char  *g_title;
static char  *g_body[DESC_MAX_LINES];
static int    g_body_count;
static char  *g_author;    /* points inside g_desc_buf or NULL */

/* --- game list --------------------------------------------------- */
#define MAX_GAMES        32
#define MAX_NAME_LEN     32
#define PICKER_PATH_CAP  64
#define MAX_AUTHOR_LEN   24

typedef struct {
    char name[MAX_NAME_LEN + 1];
    char path[PICKER_PATH_CAP];
    char author[MAX_AUTHOR_LEN + 1];   /* cached for sort-by-author */
    bool favourite;
} picker_game_t;

static picker_game_t g_games[MAX_GAMES] PICKER_SCRATCH_ATTR;
static int g_game_count = 0;
/* Ordered indices into g_games[]. Sort operations rewrite this
 * without touching the underlying records — keeps favourites +
 * author lookups O(1) by direct index. */
static int g_order[MAX_GAMES];

/* --- sort ordering ----------------------------------------------- */
typedef enum {
    SORT_NAME   = 0,
    SORT_FAV    = 1,
    SORT_AUTHOR = 2,
    SORT_COUNT  = 3,
} sort_mode_t;
static sort_mode_t g_sort = SORT_NAME;
static const char * const sort_label[SORT_COUNT] = {
    "Name", "Fav first", "Author",
};

/* --- favourites -------------------------------------------------- */
#define FAVS_PATH "/.favs"
static bool g_favs_dirty = false;

/* --- menu state -------------------------------------------------- *
 * The overlay is modelled on ThumbyNES's generic menu (nes_menu.c):
 * a darkened copy of the hero view as the backdrop, orange title
 * bar, green cursor row, INFO rows with progress bars, interactive
 * rows underneath. Favourite-toggle is NOT a menu item — it's bound
 * directly to B in the hero view, which feels far more immediate
 * than drilling into a menu to flip a flag. */
typedef enum {
    /* INFO rows (not selectable — cursor skips them). */
    MI_BATT = 0,
    MI_DISK,
    MI_BY,
    MI_FW,
    /* Selectable rows. */
    MI_SORT,
    MI_VOL,       /* slider: LEFT/RIGHT adjusts /.volume */
    MI_BRIGHT,    /* slider: LEFT/RIGHT adjusts /.brightness (live) */
    MI_CLOSE,
    MI_LOBBY,     /* LAST — "press UP from top to wrap to back-out" */
    MI_COUNT,
} menu_item_t;

/* Is this row picker-cursor-selectable? */
static bool menu_item_selectable(menu_item_t it) {
    switch (it) {
    case MI_SORT: case MI_VOL: case MI_BRIGHT:
    case MI_LOBBY: case MI_CLOSE: return true;
    default: return false;
    }
}

static bool g_menu_open = false;
static int  g_menu_cursor = MI_SORT;   /* first selectable row */
static bool g_menu_lobby_requested = false;
/* Scratch copy of the hero frame used as the darkened backdrop
 * behind the menu panel. Filled once on menu-open so each subsequent
 * redraw can restore from a clean dimmed frame without re-rendering
 * the hero view (which would also repaint over the panel). */
/* Menu backdrop is synthesised on demand (re-render hero → darken
 * in place inside g_fb) rather than cached in its own 32 KB BSS
 * buffer. The redraw cost is trivial vs. the RAM saving — 32 KB
 * goes back to the MicroPython GC heap, which matters for games
 * that import-heavy at startup (Thumbalaga etc.). */

/* --- button helpers --------------------------------------------- */
static bool btn(uint pin) { return !gpio_get(pin); }

static void buttons_init(void) {
    const uint pins[] = {
        PIN_LEFT, PIN_UP, PIN_RIGHT, PIN_DOWN,
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

/* --- battery ---------------------------------------------------- *
 * Delegates to common/lib/thumbyone_battery so the MPY picker's
 * reading matches the lobby / NES / P8 / DOOM readout exactly. */
#include "thumbyone_battery.h"

static float battery_voltage(void)  { float v = 0.0f;  thumbyone_battery_read(NULL, NULL, &v); return v; }
static bool  battery_charging(void) { bool  c = false; thumbyone_battery_read(NULL, &c,   NULL); return c; }
static int   battery_percent(void) {
    int p = 0;
    thumbyone_battery_read(&p, NULL, NULL);
    return p;
}

/* --- screen helpers --------------------------------------------- */
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

static void fb_hline(int x, int y, int w, uint16_t c) {
    fb_rect(x, y, w, 1, c);
}

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

/* --- string helpers --------------------------------------------- */
static size_t rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        s[--n] = '\0';
    }
    return n;
}

/* Fill buf with decimal `v`. Buf must be at least 6 chars for values
 * up to 99999. Returns length. Used where snprintf triggers format-
 * truncation warnings at -Werror with small bufs. */
static int int_to_str(int v, char *buf) {
    if (v < 0) v = 0;
    char tmp[8];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    else { while (v > 0 && n < 7) { tmp[n++] = '0' + (v % 10); v /= 10; } }
    for (int i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

/* Build "/games/<name>/<leaf>". Returns 0 on success, -1 if the
 * buffer is too small to hold the result. */
static int build_asset_path(char *out, size_t cap,
                            const char *game_path, const char *leaf) {
    size_t gp_len   = strlen(game_path);
    size_t leaf_len = strlen(leaf);
    if (gp_len + 1 + leaf_len + 1 > cap) return -1;
    memcpy(out, game_path, gp_len);
    out[gp_len] = '/';
    memcpy(out + gp_len + 1, leaf, leaf_len + 1);
    return 0;
}

/* --- per-game asset load ---------------------------------------- */

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

/* Extract the author name from a description buffer in place.
 * Looks for a line starting with "Author:" (case-insensitive) and
 * returns a pointer into the buffer to the trimmed value, or NULL. */
static char *find_author_line(char *start) {
    char *p = start;
    while (*p) {
        /* Skip leading whitespace on the line. */
        while (*p == ' ' || *p == '\t') ++p;
        /* Match "Author" case-insensitively. */
        if ((p[0] == 'A' || p[0] == 'a') &&
            (p[1] == 'u' || p[1] == 'U') &&
            (p[2] == 't' || p[2] == 'T') &&
            (p[3] == 'h' || p[3] == 'H') &&
            (p[4] == 'o' || p[4] == 'O') &&
            (p[5] == 'r' || p[5] == 'R')) {
            char *q = p + 6;
            while (*q == ' ' || *q == '\t' || *q == ':') ++q;
            /* Scan to newline, NUL-terminate there. */
            char *val = q;
            while (*q && *q != '\n' && *q != '\r') ++q;
            if (*q) *q++ = '\0';
            rtrim(val);
            if (*val) return val;
        }
        /* Advance to next line. */
        while (*p && *p != '\n') ++p;
        if (*p == '\n') ++p;
    }
    return NULL;
}

/* Load /games/<name>/arcade_description.txt into g_desc_buf and
 * split into title + body + author. Title falls back to dir name. */
static void load_description(const picker_game_t *g) {
    g_title = (char *)g->name;
    g_body_count = 0;
    g_author = NULL;

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

    /* Extract "Author:" before we tokenise on '\n', so find_author_line
     * walks the pristine buffer. We'll then re-scan for title + body
     * (the body scan will skip the author line naturally since it
     * contains a colon and tends to be near the bottom). */
    g_author = find_author_line(g_desc_buf);

    char *title = NULL;
    char *p = g_desc_buf;
    while (*p && g_body_count < DESC_MAX_LINES) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') ++p;
        if (*p == '\r') { *p = '\0'; ++p; }
        if (*p == '\n') { *p = '\0'; ++p; }
        rtrim(line);
        if (*line == '\0') continue;
        /* Skip the Author line when building the body — the menu
         * shows it separately. Match case-insensitively. */
        if ((line[0] == 'A' || line[0] == 'a') &&
            strlen(line) > 7 &&
            (line[1] == 'u' || line[1] == 'U') &&
            (line[2] == 't' || line[2] == 'T') &&
            (line[3] == 'h' || line[3] == 'H') &&
            (line[4] == 'o' || line[4] == 'O') &&
            (line[5] == 'r' || line[5] == 'R')) {
            /* Looks like the Author line; already captured. */
            continue;
        }
        if (!title) {
            title = line;
        } else {
            g_body[g_body_count++] = line;
        }
    }
    if (title) g_title = title;
}

static void load_selection_assets(int order_idx) {
    if (order_idx < 0 || order_idx >= g_game_count) {
        g_icon_w = g_icon_h = 0;
        g_title = NULL;
        g_body_count = 0;
        g_author = NULL;
        return;
    }
    int real = g_order[order_idx];
    load_icon(g_games[real].path);
    load_description(&g_games[real]);
}

/* --- favourites -------------------------------------------------- */

/* Look a name up in /.favs via streaming read. Returns true if the
 * exact name is present as a line. */
static bool favs_contains(const char *name) {
    FIL f;
    if (f_open(&f, FAVS_PATH, FA_READ) != FR_OK) return false;
    char line[MAX_NAME_LEN + 2];
    size_t pos = 0;
    UINT br = 0;
    char ch;
    bool match = false;
    while (f_read(&f, &ch, 1, &br) == FR_OK && br == 1) {
        if (ch == '\n' || ch == '\r') {
            line[pos] = '\0';
            if (pos > 0 && strcmp(line, name) == 0) { match = true; break; }
            pos = 0;
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = ch;
        }
    }
    if (!match && pos > 0) {
        line[pos] = '\0';
        if (strcmp(line, name) == 0) match = true;
    }
    f_close(&f);
    return match;
}

/* Write the favourite flags back to /.favs. Rebuilds the whole
 * file; small enough (<= MAX_GAMES * 33 bytes) to do in one shot. */
static void favs_save(void) {
    if (!g_favs_dirty) return;
    FIL f;
    if (f_open(&f, FAVS_PATH, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        g_favs_dirty = false;   /* best-effort — don't re-try forever */
        return;
    }
    for (int i = 0; i < g_game_count; ++i) {
        if (!g_games[i].favourite) continue;
        UINT wn = 0;
        f_write(&f, g_games[i].name, strlen(g_games[i].name), &wn);
        f_write(&f, "\n", 1, &wn);
    }
    f_close(&f);
    g_favs_dirty = false;
}

/* Populate g_games[i].favourite from /.favs at startup. */
static void favs_load_all(void) {
    for (int i = 0; i < g_game_count; ++i) {
        g_games[i].favourite = favs_contains(g_games[i].name);
    }
    g_favs_dirty = false;
}

/* --- sort persistence ------------------------------------------- */
#define SORT_PATH "/.picker_sort"

static void sort_mode_load(void) {
    FIL f;
    if (f_open(&f, SORT_PATH, FA_READ) != FR_OK) return;
    UINT br = 0;
    uint8_t b = 0;
    f_read(&f, &b, 1, &br);
    f_close(&f);
    if (br == 1 && b < SORT_COUNT) g_sort = (sort_mode_t)b;
}

static void sort_mode_save(void) {
    FIL f;
    if (f_open(&f, SORT_PATH, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return;
    uint8_t b = (uint8_t)g_sort;
    UINT wn = 0;
    f_write(&f, &b, 1, &wn);
    f_close(&f);
}

/* --- /games/ scan ----------------------------------------------- */

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

/* Read the game's author (if any) from arcade_description.txt and
 * cache it on the record. Kept short so the sort comparator can
 * just strcasecmp(a->author, b->author) without re-parsing. */
static void load_author_into(picker_game_t *g) {
    g->author[0] = '\0';
    char path[PICKER_PATH_CAP + 32];
    if (build_asset_path(path, sizeof(path), g->path, "arcade_description.txt") < 0) {
        return;
    }
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return;
    /* Peek the file into a local scratch; the loader reuses
     * g_desc_buf at render time, so we mustn't clobber it here. */
    char buf[DESC_CAP + 1];
    UINT br = 0;
    f_read(&f, buf, DESC_CAP, &br);
    f_close(&f);
    buf[br] = '\0';
    char *author = find_author_line(buf);
    if (author) {
        size_t n = strlen(author);
        if (n > MAX_AUTHOR_LEN) n = MAX_AUTHOR_LEN;
        memcpy(g->author, author, n);
        g->author[n] = '\0';
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
        g->favourite = false;
        load_author_into(g);
    }
    f_closedir(&dir);
    return 0;
}

/* --- sort ordering ---------------------------------------------- */

static int cmp_name(int a, int b) {
    return strcasecmp(g_games[a].name, g_games[b].name);
}

static int cmp_fav_then_name(int a, int b) {
    int fa = g_games[a].favourite ? 0 : 1;
    int fb = g_games[b].favourite ? 0 : 1;
    if (fa != fb) return fa - fb;
    return cmp_name(a, b);
}

static int cmp_author_then_name(int a, int b) {
    /* Games with no known author sink to the bottom. */
    int ea = g_games[a].author[0] ? 0 : 1;
    int eb = g_games[b].author[0] ? 0 : 1;
    if (ea != eb) return ea - eb;
    int c = strcasecmp(g_games[a].author, g_games[b].author);
    if (c != 0) return c;
    return cmp_name(a, b);
}

/* Rebuild g_order[] according to g_sort. Bubble sort — list is
 * at most MAX_GAMES (32), readability over speed. */
static void apply_sort(void) {
    for (int i = 0; i < g_game_count; ++i) g_order[i] = i;
    int (*cmp)(int, int) = cmp_name;
    if (g_sort == SORT_FAV)    cmp = cmp_fav_then_name;
    if (g_sort == SORT_AUTHOR) cmp = cmp_author_then_name;
    for (int i = 0; i < g_game_count - 1; ++i) {
        for (int j = 0; j < g_game_count - 1 - i; ++j) {
            if (cmp(g_order[j], g_order[j + 1]) > 0) {
                int tmp = g_order[j];
                g_order[j] = g_order[j + 1];
                g_order[j + 1] = tmp;
            }
        }
    }
}

/* Find the order index for a given real index — after a sort change
 * we want the cursor to stay on the same game. Returns 0 if missing. */
static int order_index_for(int real) {
    for (int i = 0; i < g_game_count; ++i) {
        if (g_order[i] == real) return i;
    }
    return 0;
}

/* --- hero view -------------------------------------------------- */

typedef void (*line_draw_fn)(const char *line, int y, void *user);

/* Word-wrap driver: emits chunks of `s` that each fit within
 * `max_px`. Breaks on spaces where possible; hard-wraps long
 * unbroken runs at the pixel limit. */
static void wrap_draw(const char *s, int max_px, int line_h,
                      int y_start, int max_lines,
                      line_draw_fn draw, void *user) {
    if (!s || !*s) return;
    char tmp[64];
    int lines_drawn = 0;
    const char *cur = s;

    while (*cur && lines_drawn < max_lines) {
        int best_break = 0;
        int i = 0;
        int last_space = -1;
        while (cur[i] && i < (int)sizeof(tmp) - 1) {
            if (cur[i] == ' ') last_space = i;
            tmp[i] = cur[i];
            tmp[i + 1] = '\0';
            if (nes_font_width(tmp) > max_px) {
                best_break = (last_space > 0) ? last_space : i;
                break;
            }
            ++i;
        }
        if (cur[i] == '\0') best_break = i;
        if (best_break == 0) break;   /* defensive — never infinite-loop */

        char saved = tmp[best_break];
        tmp[best_break] = '\0';
        rtrim(tmp);
        draw(tmp, y_start + lines_drawn * line_h, user);
        tmp[best_break] = saved;
        ++lines_drawn;

        cur += best_break;
        while (*cur == ' ') ++cur;
    }
}

static void draw_body_line(const char *line, int y, void *user) {
    (void)user;
    nes_font_draw(g_fb, line, 4, y, COL_TEXT);
}

static void render_icon_placeholder(int x, int y, int side, char init) {
    fb_rect(x, y, side, side, COL_PANEL);
    char s[2] = { init ? init : '?', 0 };
    int sw = nes_font_width_2x(s);
    nes_font_draw_2x(g_fb, s, x + (side - sw) / 2, y + (side - 10) / 2, COL_FG);
}

static void render_empty(void) {
    fb_fill(COL_BG);
    int w = nes_font_width_2x("MPY");
    nes_font_draw_2x(g_fb, "MPY", (128 - w) / 2, 10, COL_HEAD);
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

/* Render the hero view for the game at g_order[sel]. */
/* Draw the hero layout into g_fb WITHOUT pushing to the LCD. Used
 * directly by render_menu() so the darken + overlay can happen
 * before the single present — otherwise the user sees a full-bright
 * flash of the hero frame every time the menu redraws (every cursor
 * move). render_hero() below is the wrapper that draws + presents. */
static void render_hero_fb(int sel) {
    fb_fill(COL_BG);

    /* Top strip: "MPY" banner left, pos + favourite-star right. */
    nes_font_draw(g_fb, "MPY", 4, 4, COL_HEAD);

    int real = (g_game_count > 0) ? g_order[sel] : -1;

    char pos[12];
    int cur = sel + 1, tot = g_game_count;
    size_t pi = 0;
    if (cur >= 10) pos[pi++] = '0' + (cur / 10);
    pos[pi++] = '0' + (cur % 10);
    pos[pi++] = '/';
    if (tot >= 10) pos[pi++] = '0' + (tot / 10);
    pos[pi++] = '0' + (tot % 10);
    pos[pi] = '\0';
    int pw = nes_font_width(pos);
    nes_font_draw(g_fb, pos, 128 - pw - 4, 4, COL_ACCENT);

    /* Favourite star just left of the position counter. */
    if (real >= 0 && g_games[real].favourite) {
        nes_font_draw(g_fb, "*", 128 - pw - 10, 4, COL_ACCENT);
    }

    fb_hline(2, 14, 124, COL_DIM);

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

    if (g_game_count > 1) {
        if (sel > 0)                nes_font_draw(g_fb, "<", 2,        slot_y + 28, COL_DIM);
        if (sel < g_game_count - 1) nes_font_draw(g_fb, ">", 128 - 6,  slot_y + 28, COL_DIM);
    }

    /* Title below the icon, 2x font, truncated to fit. */
    const char *title = g_title ? g_title
                                : (real >= 0 ? g_games[real].name : "");
    char title_buf[40];
    strncpy(title_buf, title, sizeof(title_buf) - 1);
    title_buf[sizeof(title_buf) - 1] = '\0';
    while (nes_font_width_2x(title_buf) > 124 && strlen(title_buf) > 1) {
        title_buf[strlen(title_buf) - 1] = '\0';
    }
    int tw = nes_font_width_2x(title_buf);
    int title_y = slot_y + slot_s + 4;
    nes_font_draw_2x(g_fb, title_buf,
                     (128 - tw) / 2, title_y,
                     real >= 0 && g_games[real].favourite ? COL_ACCENT : COL_FG);

    /* Blurb: up to 3 wrapped lines below the title. Top-capped at
     * y=116 so it doesn't bleed into the footer bar below. */
    int blurb_y = title_y + 12;
    int blurb_max_y = 116;
    int blurb_rows = (blurb_max_y - blurb_y) / 7;
    if (blurb_rows > 3) blurb_rows = 3;

    if (g_body_count > 0 && blurb_rows > 0) {
        char blurb[256];
        size_t bp = 0;
        for (int i = 0; i < g_body_count && bp < sizeof(blurb) - 1; ++i) {
            const char *l = g_body[i];
            size_t ln = strlen(l);
            if (bp > 0 && bp < sizeof(blurb) - 1) blurb[bp++] = ' ';
            if (bp + ln >= sizeof(blurb)) ln = sizeof(blurb) - 1 - bp;
            memcpy(blurb + bp, l, ln);
            bp += ln;
        }
        blurb[bp] = '\0';
        wrap_draw(blurb, 120, 7, blurb_y, blurb_rows,
                  draw_body_line, NULL);
    }

    /* Footer bar — mirrors the lobby style: navy strip at y=119..127
     * with a cyan accent line at y=118 and the action hint in cyan
     * centred on the navy. Keeps every ThumbyOne screen visually
     * consistent. */
    fb_rect(0, 119, 128, 9, 0x0008);             /* navy */
    fb_hline(0, 118, 128, COL_HEAD);             /* cyan accent */
    const char *hint = "A play B fav MENU";
    int hw = nes_font_width(hint);
    nes_font_draw(g_fb, hint, (128 - hw) / 2, 121, COL_HEAD);
}

/* Public hero render: draw + push to LCD. */
static void render_hero(int sel) {
    render_hero_fb(sel);
    present_blocking();
}

/* --- menu overlay ------------------------------------------------ */

/* Disk-usage query + formatting live in the shared helper so the
 * MPY picker's "2.3M/9.6M" row matches the lobby / NES / P8 picker
 * menus to the byte. */
#include "thumbyone_fs_stats.h"

/* Global settings + backlight — the MPY picker applies the saved
 * /.brightness on entry (LCD driver came up at full) and consults
 * /.volume for the engine-bridge logic in thumbyone_launcher. */
#include "thumbyone_settings.h"
#include "thumbyone_backlight.h"

/* In-place darken the framebuffer to ~1/4 brightness, per-channel.
 * Copy of ThumbyNES nes_menu.c's darken_fb — kept verbatim so the
 * overlay backdrop intensity matches byte-for-byte. */
static void darken_fb(uint16_t *fb) {
    for (int i = 0; i < 128 * 128; i++) {
        uint16_t p = fb[i];
        uint32_t r = (p >> 11) & 0x1F;
        uint32_t g = (p >>  5) & 0x3F;
        uint32_t b = (p      ) & 0x1F;
        r >>= 2; g >>= 2; b >>= 2;
        fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

/* Thin fill bar — no outline. Used as the INFO-row progress strip
 * (same shape/size as ThumbyNES). */
static void draw_thin_bar(int x, int y, int w, int h,
                          int value, int vmin, int vmax,
                          uint16_t fg, uint16_t bg) {
    fb_rect(x, y, w, h, bg);
    int span = vmax - vmin;
    if (span <= 0) return;
    int v = value - vmin;
    if (v < 0) v = 0;
    if (v > span) v = span;
    int fill_w = (w * v) / span;
    if (fill_w > 0) fb_rect(x, y, fill_w, h, fg);
}

/* NES-style menu layout constants. */
#define M_TITLE_H      11
#define M_FOOTER_H      8
#define M_ROW_H        10
#define M_ITEMS_TOP    (M_TITLE_H + 8 /* subtitle */ + 1)

/* Build one row's value-column text and optional bar parameters.
 * `val_out` must hold >= 24 chars. `bar_value` etc. are set to
 * control a progress-bar strip; set `bar_max=0` to skip the bar. */
typedef struct {
    char      val[24];
    uint16_t  val_col;
    int       bar_value;
    int       bar_min;
    int       bar_max;
} menu_row_render_t;

static void menu_build_batt_row(menu_row_render_t *r) {
    int pct = battery_percent();
    float v = battery_voltage();
    bool chg = battery_charging();
    int vmv = (int)(v * 100.0f + 0.5f);
    int vwhole = vmv / 100;
    int vhund  = vmv % 100;
    size_t k = 0;
    if (chg) {
        memcpy(r->val, "CHRG ", 5); k = 5;
    } else {
        char pbuf[6]; int_to_str(pct, pbuf);
        size_t pl = strlen(pbuf);
        memcpy(r->val, pbuf, pl); k = pl;
        r->val[k++] = '%'; r->val[k++] = ' ';
    }
    char vbuf[10]; int_to_str(vwhole, vbuf);
    size_t vl = strlen(vbuf);
    vbuf[vl++] = '.';
    if (vhund < 10) vbuf[vl++] = '0';
    int_to_str(vhund, vbuf + vl); vl = strlen(vbuf);
    vbuf[vl++] = 'V'; vbuf[vl] = 0;
    size_t vbl = strlen(vbuf);
    if (k + vbl >= sizeof(r->val)) vbl = sizeof(r->val) - 1 - k;
    memcpy(r->val + k, vbuf, vbl); r->val[k + vbl] = 0;
    r->val_col   = chg ? COL_HIGHLT : (pct < 15 ? COL_ERR : COL_TEXT);
    r->bar_value = pct;
    r->bar_min   = 0;
    r->bar_max   = 100;
}

static void menu_build_disk_row(menu_row_render_t *r) {
    uint64_t used_b = 0, total_b = 0;
    thumbyone_fs_get_usage(&used_b, NULL, &total_b);
    thumbyone_fs_fmt_used_total(used_b, total_b, r->val, sizeof(r->val));
    r->val_col = COL_TEXT;
    /* Bar direction matches the text: fills with used (fuller bar
     * = less free space). KB-scaled to fit bar_value's int range
     * with plenty of headroom on our 9.6 MB volume. */
    r->bar_value = (int)(used_b  / 1024);
    r->bar_min   = 0;
    r->bar_max   = total_b > 0 ? (int)(total_b / 1024) : 1;
}

static void menu_build_by_row(menu_row_render_t *r) {
    const char *author = g_author ? g_author : "-";
    strncpy(r->val, author, sizeof(r->val) - 1);
    r->val[sizeof(r->val) - 1] = 0;
    /* Value column must fit in ~64 px (128 - 10 label - 54 margin). */
    while (nes_font_width(r->val) > 84 && strlen(r->val) > 1) {
        r->val[strlen(r->val) - 1] = 0;
    }
    r->val_col = COL_TEXT;
    r->bar_max = 0;
}

static void menu_build_fw_row(menu_row_render_t *r) {
    strncpy(r->val, "MPY " THUMBYONE_FW_VERSION, sizeof(r->val) - 1);
    r->val[sizeof(r->val) - 1] = 0;
    r->val_col = COL_TEXT;
    r->bar_max = 0;
}

static const char *menu_label(menu_item_t it) {
    switch (it) {
    case MI_BATT:   return "batt";
    case MI_DISK:   return "disk";
    case MI_BY:     return "by";
    case MI_FW:     return "fw";
    case MI_SORT:   return "sort";
    case MI_VOL:    return "VOLUME";
    case MI_BRIGHT: return "BRIGHTNESS";
    case MI_LOBBY:  return "back to lobby";
    case MI_CLOSE:  return "close";
    case MI_COUNT:  return "";
    }
    return "";
}

/* Thick right-aligned outlined slider — matches the lobby MENU
 * overlay's volume/brightness widget and the NES/P8 slot menu
 * slider. Used for MI_VOL / MI_BRIGHT rows in the MPY picker menu
 * so all three menu systems (lobby + slot in-game + MPY picker)
 * render the same adjustable-value affordance. */
static void draw_thick_slider(int x, int y, int w, int h,
                              int value, int vmax,
                              uint16_t fg, uint16_t bg) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            g_fb[yy * 128 + xx] = bg;
    for (int i = 0; i < w; ++i) {
        g_fb[y * 128 + x + i] = fg;
        g_fb[(y + h - 1) * 128 + x + i] = fg;
    }
    for (int j = 0; j < h; ++j) {
        g_fb[(y + j) * 128 + x] = fg;
        g_fb[(y + j) * 128 + x + w - 1] = fg;
    }
    if (vmax <= 0) return;
    int v = value < 0 ? 0 : (value > vmax ? vmax : value);
    int fill_w = ((w - 2) * v) / vmax;
    for (int j = 0; j < h - 2; ++j)
        for (int i = 0; i < fill_w; ++i)
            g_fb[(y + 1 + j) * 128 + (x + 1 + i)] = fg;
}

/* Live in-menu values for the slider rows. Loaded at menu-open
 * from /.volume + /.brightness. Saved back on close if moved. */
static int g_menu_vol = 0;
static int g_menu_bri = 0;
static int g_menu_vol_initial = 0;
static int g_menu_bri_initial = 0;

static void render_menu(int sel) {
    /* Synthesise the backdrop: re-render the hero into g_fb, then
     * darken in place. Saves the 32 KB cache buffer we used to
     * keep for this. Important: use the _fb variant that doesn't
     * push to the LCD — otherwise the user sees a full-bright
     * flash of the hero between this call and the final present
     * at the bottom of render_menu. */
    render_hero_fb(sel);
    darken_fb(g_fb);

    /* Title bar — black strip with orange underline. */
    fb_rect(0, 0, 128, M_TITLE_H, COL_BG);
    fb_rect(0, M_TITLE_H - 1, 128, 1, COL_TITLE);
    nes_font_draw(g_fb, "MENU", 2, 2, COL_TITLE);

    /* Subtitle — current game name in dim grey. */
    int real = g_order[sel];
    char subtitle[24];
    strncpy(subtitle, g_games[real].name, sizeof(subtitle) - 1);
    subtitle[sizeof(subtitle) - 1] = 0;
    if (nes_font_width(subtitle) > 124) {
        while (nes_font_width(subtitle) > 124 && strlen(subtitle) > 1) {
            subtitle[strlen(subtitle) - 1] = 0;
        }
    }
    nes_font_draw(g_fb, subtitle, 2, M_TITLE_H, COL_DIM);

    /* Items */
    for (int i = 0; i < MI_COUNT; ++i) {
        int y = M_ITEMS_TOP + i * M_ROW_H;
        bool is_cursor = (i == g_menu_cursor);
        if (is_cursor) {
            fb_rect(0, y - 1, 128, M_ROW_H, COL_HL_BG);
        }
        uint16_t fg = is_cursor ? COL_HIGHLT : COL_FG;
        if (is_cursor) nes_font_draw(g_fb, ">", 1, y + 1, fg);
        nes_font_draw(g_fb, menu_label((menu_item_t)i), 7, y + 1, fg);

        menu_row_render_t row = { .val = {0}, .val_col = fg, .bar_max = 0 };
        switch ((menu_item_t)i) {
        case MI_BATT:  menu_build_batt_row(&row); break;
        case MI_DISK:  menu_build_disk_row(&row); break;
        case MI_BY:    menu_build_by_row  (&row); break;
        case MI_FW:    menu_build_fw_row  (&row); break;
        case MI_SORT:
            strncpy(row.val, sort_label[g_sort], sizeof(row.val) - 1);
            row.val[sizeof(row.val) - 1] = 0;
            row.val_col = fg;
            break;
        case MI_VOL:
        case MI_BRIGHT:
            /* Rendered below with a thick right-aligned slider —
             * no row.val text, no thin bar. */
            break;
        case MI_LOBBY:
        case MI_CLOSE:
        case MI_COUNT:
            break;
        }

        /* Thick right-aligned slider for MI_VOL / MI_BRIGHT.
         * Matches the lobby MENU overlay + the NES/P8 slot menus
         * so all three systems show the same widget. */
        if (i == MI_VOL) {
            draw_thick_slider(128 - 32, y + 1, 28, M_ROW_H - 2,
                              g_menu_vol, THUMBYONE_VOLUME_MAX,
                              fg, COL_BAR_BG);
        } else if (i == MI_BRIGHT) {
            draw_thick_slider(128 - 32, y + 1, 28, M_ROW_H - 2,
                              g_menu_bri, THUMBYONE_BRIGHTNESS_MAX,
                              fg, COL_BAR_BG);
        }

        /* Value text aligned to the right of the row. Cursor row
         * highlights the value in green too so the whole row pops. */
        if (row.val[0]) {
            int vw = nes_font_width(row.val);
            uint16_t vc = is_cursor ? COL_HIGHLT : row.val_col;
            nes_font_draw(g_fb, row.val, 128 - vw - 2, y + 1, vc);
        }

        /* Thin progress strip along the bottom of INFO rows with
         * a meaningful range. Skips author / firmware / CHOICE /
         * ACTION rows (bar_max == 0). */
        if (row.bar_max > 0) {
            int bar_x = 8;
            int bar_y = y + M_ROW_H - 3;
            int bar_w = 128 - 16;
            draw_thin_bar(bar_x, bar_y, bar_w, 2,
                          row.bar_value, row.bar_min, row.bar_max,
                          COL_HIGHLT, COL_BAR_BG);
        }
    }

    /* Footer — orange underline + hint for the current cursor row. */
    fb_rect(0, 128 - M_FOOTER_H, 128, M_FOOTER_H, COL_BG);
    fb_rect(0, 128 - M_FOOTER_H, 128, 1, COL_TITLE);
    const char *hint;
    switch ((menu_item_t)g_menu_cursor) {
    case MI_SORT:   hint = "<> sort  A cycle";  break;
    case MI_VOL:    hint = "<> adjust";         break;
    case MI_BRIGHT: hint = "<> adjust";         break;
    case MI_LOBBY:  hint = "A return to lobby"; break;
    case MI_CLOSE:  hint = "A close";           break;
    default:        hint = "A select  B close"; break;
    }
    int hw = nes_font_width(hint);
    nes_font_draw(g_fb, hint, (128 - hw) / 2,
                   128 - M_FOOTER_H + 1, COL_DIM);

    present_blocking();
}

/* Advance cursor in dir +1 or -1, skipping non-selectable rows. */
static void menu_cursor_seek(int dir) {
    int start = g_menu_cursor;
    for (int tries = 0; tries < MI_COUNT; ++tries) {
        g_menu_cursor = (g_menu_cursor + dir + MI_COUNT) % MI_COUNT;
        if (menu_item_selectable((menu_item_t)g_menu_cursor)) return;
    }
    g_menu_cursor = start;
}

static void toggle_favourite(int sel) {
    int real = g_order[sel];
    g_games[real].favourite = !g_games[real].favourite;
    g_favs_dirty = true;
}

static void cycle_sort(void) {
    g_sort = (sort_mode_t)((g_sort + 1) % SORT_COUNT);
}

/* --- selection write --------------------------------------------- */

static int write_active_game(const char *path) {
    FIL f;
    FRESULT r = f_open(&f, "/.active_game", FA_CREATE_ALWAYS | FA_WRITE);
    if (r != FR_OK) return -1;
    UINT written;
    r = f_write(&f, path, (UINT)strlen(path), &written);
    f_close(&f);
    return (r == FR_OK && written == strlen(path)) ? 0 : -1;
}

/* --- main loop --------------------------------------------------- */

int thumbyone_picker_run(void) {
    set_sys_clock_khz(250000, true);
    stdio_init_all();

    nes_lcd_init();
    nes_lcd_backlight(1);
    buttons_init();

    FATFS g_fs;
    FRESULT r = thumbyone_fs_mount(&g_fs);
    if (r == FR_OK) {
        /* Apply the ThumbyOne system-wide brightness — same
         * /.brightness the lobby + NES + P8 pick up. Has to happen
         * after the FAT mount; the LCD driver came up at full. */
        thumbyone_backlight_set(thumbyone_settings_load_brightness());
    }
    if (r != FR_OK) {
        render_error("mount failed", "go to lobby, wipe");
        while (1) {
            sleep_ms(20);
            if (btn(PIN_MENU)) {
                /* In the error path, MENU-hold routes to lobby
                 * without the overlay — nothing else would work. */
                absolute_time_t d = make_timeout_time_ms(800);
                bool held = false;
                while (btn(PIN_MENU)) {
                    if (time_reached(d)) { held = true; break; }
                    sleep_ms(10);
                }
                if (held) thumbyone_handoff_request_lobby();
            }
        }
    }

    if (scan_games() < 0) {
        render_error("scan failed", "corrupt /games/?");
        while (1) { sleep_ms(20); }
    }

    if (g_game_count == 0) {
        render_empty();
        while (1) {
            sleep_ms(20);
            if (btn(PIN_MENU)) {
                absolute_time_t d = make_timeout_time_ms(800);
                bool held = false;
                while (btn(PIN_MENU)) {
                    if (time_reached(d)) { held = true; break; }
                    sleep_ms(10);
                }
                if (held) thumbyone_handoff_request_lobby();
            }
        }
    }

    favs_load_all();
    sort_mode_load();
    apply_sort();

    int sel = 0;
    load_selection_assets(sel);
    render_hero(sel);

    bool prev_up = false, prev_down = false;
    bool prev_left = false, prev_right = false;
    bool prev_a = false, prev_b = false, prev_menu = false;

    while (1) {
        bool dirty = false;

        if (g_menu_open) {
            /* --- menu input ---
             * On slider rows (MI_VOL / MI_BRIGHT): LEFT/RIGHT adjust
             * the slider value with autorepeat (same 300/60 ms
             * timing as the lobby + NES/P8 slot menus). On other
             * rows: LEFT/RIGHT is treated as cursor navigation so
             * you can still use it for consistency with the hero
             * view. UP/DOWN always navigates. */
            bool on_slider = (g_menu_cursor == MI_VOL ||
                              g_menu_cursor == MI_BRIGHT);
            bool lt_edge = just_pressed(PIN_LEFT,  &prev_left);
            bool rt_edge = just_pressed(PIN_RIGHT, &prev_right);

            /* Autorepeat book-keeping for LEFT/RIGHT when on slider. */
            static uint32_t ar_lt_next_us = 0;
            static uint32_t ar_rt_next_us = 0;
            const uint32_t AR_DELAY_US = 300u * 1000u;
            const uint32_t AR_STEP_US  =  60u * 1000u;
            uint32_t now_us = (uint32_t)time_us_64();
            bool lt_rep = false, rt_rep = false;
            if (on_slider) {
                bool lt_held = !gpio_get(PIN_LEFT);
                bool rt_held = !gpio_get(PIN_RIGHT);
                if (lt_edge) ar_lt_next_us = now_us + AR_DELAY_US;
                if (rt_edge) ar_rt_next_us = now_us + AR_DELAY_US;
                if (lt_held && !lt_edge && (int32_t)(now_us - ar_lt_next_us) >= 0) {
                    lt_rep = true; ar_lt_next_us = now_us + AR_STEP_US;
                }
                if (rt_held && !rt_edge && (int32_t)(now_us - ar_rt_next_us) >= 0) {
                    rt_rep = true; ar_rt_next_us = now_us + AR_STEP_US;
                }
            }

            if (just_pressed(PIN_UP, &prev_up)) {
                menu_cursor_seek(-1);
                dirty = true;
            }
            if (just_pressed(PIN_DOWN, &prev_down)) {
                menu_cursor_seek(+1);
                dirty = true;
            }

            if (lt_edge || lt_rep) {
                if (g_menu_cursor == MI_VOL) {
                    if (g_menu_vol > 0) { g_menu_vol--; dirty = true; }
                } else if (g_menu_cursor == MI_BRIGHT) {
                    int step = 12;
                    if (g_menu_bri > 0) {
                        g_menu_bri -= step;
                        if (g_menu_bri < 0) g_menu_bri = 0;
                        /* Live-apply brightness so the user sees the
                         * dim/brighten as they slide. */
                        thumbyone_backlight_set((uint8_t)g_menu_bri);
                        dirty = true;
                    }
                } else if (lt_edge) {
                    /* Non-slider rows: LEFT acts as UP for nav. */
                    menu_cursor_seek(-1);
                    dirty = true;
                }
            }
            if (rt_edge || rt_rep) {
                if (g_menu_cursor == MI_VOL) {
                    if (g_menu_vol < THUMBYONE_VOLUME_MAX) { g_menu_vol++; dirty = true; }
                } else if (g_menu_cursor == MI_BRIGHT) {
                    int step = 12;
                    if (g_menu_bri < THUMBYONE_BRIGHTNESS_MAX) {
                        g_menu_bri += step;
                        if (g_menu_bri > THUMBYONE_BRIGHTNESS_MAX) g_menu_bri = THUMBYONE_BRIGHTNESS_MAX;
                        thumbyone_backlight_set((uint8_t)g_menu_bri);
                        dirty = true;
                    }
                } else if (rt_edge) {
                    menu_cursor_seek(+1);
                    dirty = true;
                }
            }

            if (just_pressed(PIN_A, &prev_a)) {
                switch ((menu_item_t)g_menu_cursor) {
                case MI_SORT: {
                    int real = g_order[sel];
                    cycle_sort();
                    apply_sort();
                    sel = order_index_for(real);
                    dirty = true;
                    break;
                }
                case MI_LOBBY:
                    g_menu_lobby_requested = true;
                    break;
                case MI_CLOSE:
                    g_menu_open = false;
                    break;
                default: break;   /* INFO rows + sliders: A is a no-op */
                }
            }
            /* B or MENU dismiss the overlay (NES menu convention). */
            if (just_pressed(PIN_B, &prev_b) ||
                just_pressed(PIN_MENU, &prev_menu)) {
                g_menu_open = false;
            }

            /* On close: persist volume / brightness if they moved.
             * The shared-FAT write is synchronous via thumbyone_disk,
             * so no flush needed here. */
            if (!g_menu_open) {
                if (g_menu_vol != g_menu_vol_initial) {
                    thumbyone_settings_save_volume((uint8_t)g_menu_vol);
                }
                if (g_menu_bri != g_menu_bri_initial) {
                    thumbyone_settings_save_brightness((uint8_t)g_menu_bri);
                    /* Already applied live via on_change; this just
                     * persists. */
                }
                load_selection_assets(sel);
                render_hero(sel);
            }

            if (g_menu_lobby_requested) {
                favs_save();
                sort_mode_save();
                f_unmount("");
                nes_lcd_wait_idle();
                thumbyone_handoff_request_lobby();
                /* does not return */
            }

            if (dirty && g_menu_open) render_menu(sel);
        } else {
            /* --- hero input --- */
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

            /* B toggles favourite for the current game — immediate,
             * no drilling into the menu. The hero view updates the
             * star + title colour on the next redraw. */
            if (just_pressed(PIN_B, &prev_b)) {
                toggle_favourite(sel);
                dirty = true;
            }

            if (just_pressed(PIN_A, &prev_a)) {
                int real = g_order[sel];
                const char *chosen = g_games[real].path;
                if (write_active_game(chosen) < 0) {
                    render_error("write failed", "flash locked?");
                    sleep_ms(2000);
                    render_hero(sel);
                    continue;
                }
                favs_save();
                sort_mode_save();
                f_unmount("");
                nes_lcd_wait_idle();
                nes_lcd_teardown();
                /* Arm the MENU-long-hold watchdog so the user can
                 * bail back to the top-level lobby from inside a
                 * MicroPython game — games own the LCD + button
                 * polling from here on, but our background timer
                 * still fires from IRQ context. */
                thumbyone_menu_watchdog_install();
                return 0;
            }

            if (just_pressed(PIN_MENU, &prev_menu)) {
                g_menu_open = true;
                g_menu_cursor = MI_SORT;
                /* Snapshot the ThumbyOne system-wide settings so the
                 * slider rows reflect current state. Save on close
                 * only if moved. */
                g_menu_vol = thumbyone_settings_load_volume();
                g_menu_bri = thumbyone_settings_load_brightness();
                g_menu_vol_initial = g_menu_vol;
                g_menu_bri_initial = g_menu_bri;
                render_menu(sel);
            }

            if (dirty) {
                load_selection_assets(sel);
                render_hero(sel);
            }
        }

        sleep_ms(20);
    }
}
