/* ---------------------------------------------------------------------------
 * Rockman7FC.exe: what the settings window's files share
 *
 *   main.c         entry point, command line, Wine detection, log
 *   releases.c     installed releases, adding one, the proxy, launching
 *   model.c        the settings (S) and reading/writing port.ini
 *   pad_devices.c  names of connected controllers for the device list
 *   draw.c         theme, fonts and drawing primitives
 *   widgets.c      clickable regions and the controls built on them
 *   layout.c       where everything goes in the window
 *   window.c       mouse/keyboard handling and the window procedure
 * ------------------------------------------------------------------------- */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "port_common.h"
#include "port_pad.h"

#define EXIT_PLAY 10             /* Play under Wine: the shell launcher starts the game */
#define DESIGN_W 780             /* client size at 100% zoom */
#define DESIGN_H 760

enum { TIMER_LISTEN_PAD = 1,     /* polls the pad while a pad slot waits for input */
       TIMER_PAD_CHECK };        /* is a controller connected? */

/* ---- main.c -------------------------------------------------------------- */
extern char g_root[MAX_PATH];    /* folder of this exe, trailing '\' */
extern char g_ini[MAX_PATH];
extern int  g_wine;              /* running under Wine */
extern int  g_linux;             /* Wine on a Linux host */
extern HWND g_wnd;

void log_root(const char *msg);  /* appends to port.log next to this exe */

static inline int ini_int(const char *sec, const char *key, int def, int lo, int hi) {
    return port_ini_int(g_ini, sec, key, def, lo, hi);
}

/* ---- releases.c ----------------------------------------------------------
 * Folder layout: <root>\ja\ (rm7fc.exe) and <root>\en\ (mm7fc.exe), or a
 * single release with this exe dropped straight into its folder. What is
 * installed is looked up by releases_refresh() and cached, since the window
 * shows it on every repaint. */
typedef struct {
    int  installed;
    char dir[MAX_PATH];          /* game folder, trailing '\' */
    char ver[40];                /* "ver.final2" from readme.txt, or "" */
    int  year;                   /* year of the game executable, or 0 */
} release;

void releases_refresh(void);
const release *release_for(int en);        /* 0 = Japanese, 1 = English */
int any_release(void);
const char *import_release(const char *srcdir, char *which);   /* NULL or an error */
const char *launch_game(int en);                                /* NULL or an error */

/* ---- model.c ------------------------------------------------------------- */
typedef struct {
    int scale;                   /* window scale 1..16 */
    int full;                    /* 0 window, 1 fullscreen integer, 2 stretched */
    int lang_en;                 /* text and menus from the English release */
    int track_en;                /* soundtrack from the English release */
    int music, music_vol, fx, fx_vol;
    int keys[PORT_ACTION_COUNT][PORT_KEYS_PER_ACTION];   /* VK, 0 = none */
    int pad[PORT_ACTION_COUNT][PORT_KEYS_PER_ACTION];    /* pad input + 1, 0 = none */
    int pad_on;
    int pad_dev;                 /* PAD_ANY / PAD_XINPUT(n) / PAD_JOY(n) */
    char pad_dev_name[96];
    int vdesk, gdi, gamescope;   /* Wine options */
    int dark, accent, zoom;      /* the window's own look; zoom indexes ZOOMS */
} settings;

extern settings S;

#define ZOOM_COUNT 4
extern const int ZOOMS[ZOOM_COUNT];

void load_settings(void);
void save_settings(void);
void default_keys(void);
void default_pad(void);
void restore_defaults(void);     /* everything except the window's look */
void compact_slots(int slots[PORT_KEYS_PER_ACTION]);   /* alternate moves up if main is empty */

/* ---- pad_devices.c ------------------------------------------------------- */
typedef struct { int dev; char name[96]; } pad_entry;
int pad_list_devices(pad_entry *out, int max);

/* ---- draw.c -------------------------------------------------------------- */
typedef struct { COLORREF bg, card, edge, text, muted, track, chip, chip_hi; } theme;
extern const theme DARK, LIGHT;

#define ACCENT_COUNT 6
extern const COLORREF ACCENTS[ACCENT_COUNT];
extern const char *const ACCENT_NAMES[ACCENT_COUNT];

#define T (S.dark ? &DARK : &LIGHT)      /* current theme */
#define ACC (ACCENTS[S.accent])          /* current accent colour */
#define WHITE RGB(255, 255, 255)
#define ERROR_RED RGB(255, 99, 110)

extern HDC g_dc;                         /* target of the drawing functions */
extern HFONT f_body, f_small, f_bold, f_label, f_big;
extern HICON g_icon_big, g_icon_small;

int Z(int v);                            /* scales a 100% size by the zoom */
COLORREF mix(COLORREF a, COLORREF b, int pct);

void draw_startup(void);
void draw_begin(HDC dc);
void draw_end(void);
void make_fonts(void);
void load_sheet(void);
int  sheet_loaded(void);

void fill(int x, int y, int w, int h, COLORREF c);
void pixel_text(int x, int y, const char *s, int px, COLORREF ink, COLORREF shadow);
void text(int x, int y, int w, int h, const char *s, HFONT f, COLORREF c, UINT flags);
int  text_width(const char *s, HFONT f);
void rrect(int x, int y, int w, int h, int r, COLORREF c, COLORREF border);
void circle(int cx, int cy, int r, COLORREF c);
void ring(int cx, int cy, int r, COLORREF c, float width);
void play_triangle(int x, int y, int s, COLORREF c);
void chevron(int x, int y, int s, COLORREF c);

/* ---- widgets.c ----------------------------------------------------------- */

/* What a clickable region is. a and b say which one (see the enums below). */
enum { H_SEG = 1,     /* segmented control: a = G_*, b = segment */
       H_TOGGLE,      /* a = O_* */
       H_SLIDER,      /* a = O_* */
       H_KEY,         /* key/button slot: a = action, b = slot */
       H_RESETKEYS,
       H_SWATCH,      /* accent colour: a = index */
       H_BUTTON,      /* a = B_* */
       H_DROP,        /* dropdown box: a = D_* */
       H_DROPITEM,    /* open list item: a = D_*, b = index (-1 = padding) */
       H_PCT };       /* volume percentage (double-click edits): a = O_* */
enum { D_SIZE = 1, D_DEVICE };
enum { G_LANG = 1, G_TRACK, G_RENDER, G_THEME, G_ZOOM, G_TAB };
enum { O_MUSIC = 1, O_FX, O_VDESK, O_GAMESCOPE, O_PAD };
enum { B_PLAY = 1, B_CLOSE, B_ADD, B_DEFAULTS };

typedef struct { RECT r; int kind, a, b, enabled; } hit;
typedef struct { int value; char label[112]; int enabled; } drop_item;

#define MAX_HITS 96
#define MAX_DROP_ITEMS 20

/* State shared by drawing and input handling. */
typedef struct {
    hit hits[MAX_HITS];          /* rebuilt on every paint */
    int nhits;
    int hover;                   /* hit under the mouse, -1 none */
    int drag;                    /* slider being dragged (O_*), -1 none */
    int listen_a, listen_s;      /* slot waiting for a key/button; listen_a -1 none */
    int tab;                     /* 0 keyboard, 1 gamepad */
    pad_bits pad_base;           /* buttons already held when listening began */
    int pad_src;                 /* PAD_SRC_*, -1 not checked yet */
    int confirm_reset;           /* "Restore defaults" clicked once */
    int drop;                    /* open dropdown (D_*), 0 none */
    RECT drop_rect;              /* the open dropdown's box */
    drop_item items[MAX_DROP_ITEMS];   /* its list, built when it opens */
    int nitems;
    int max_scale;               /* largest integer scale that fits a monitor */
    char status[256];            /* footer message */
    int status_err;
} ui_state;

extern ui_state U;

int  add_hit(int x, int y, int w, int h, int kind, int a, int b, int enabled);
int  hit_at(int x, int y);
int  is_hover(int kind, int a, int b);
void status(const char *msg, int err);
void measure_monitors(void);
void open_dropdown(int id);

void card(int x, int y, int w, int h, const char *title);
void row_label(int x, int y, int h, const char *label, const char *sub);
void segmented(int x, int y, int w, int h, int group, const char *const *labels,
               const int *enabled, int n, int sel);
void toggle(int x, int y, int id, int on);
void slider(int x, int y, int w, int id, int value, int enabled);
void dropdown(int x, int y, int w, int h, int id, const char *label);
void dropdown_list(int ch);
void key_chip(int x, int y, int w, int h, int a, int s);
void button(int x, int y, int w, int h, int id, const char *label, int primary);

/* ---- layout.c ------------------------------------------------------------ */
void paint(HDC dc, int cw, int ch);

/* ---- window.c ------------------------------------------------------------ */
LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp);

#endif
