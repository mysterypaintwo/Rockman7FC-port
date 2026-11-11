/* ---------------------------------------------------------------------------
 * Drawing: theme, fonts, the game's pixel font and shape primitives
 *
 * Shapes are drawn anti-aliased with GDI+ when it is available and with
 * plain GDI otherwise. Everything draws into g_dc (set by draw_begin).
 * ------------------------------------------------------------------------- */
#include "settings.h"

/* ---- GDI+ flat API (declared here; mingw's headers are C++ only) --------- */
typedef struct { UINT32 version; void *cb; BOOL no_thread; BOOL no_codecs; } GpStartupInput;
typedef void GpGraphics, GpBrush, GpPath, GpPen;
int WINAPI GdiplusStartup(ULONG_PTR *, const GpStartupInput *, void *);
int WINAPI GdipCreateFromHDC(HDC, GpGraphics **);
int WINAPI GdipDeleteGraphics(GpGraphics *);
int WINAPI GdipSetSmoothingMode(GpGraphics *, int);
int WINAPI GdipSetPixelOffsetMode(GpGraphics *, int);
int WINAPI GdipCreateSolidFill(DWORD, GpBrush **);
int WINAPI GdipDeleteBrush(GpBrush *);
int WINAPI GdipCreatePath(int, GpPath **);
int WINAPI GdipDeletePath(GpPath *);
int WINAPI GdipAddPathArc(GpPath *, float, float, float, float, float, float);
int WINAPI GdipAddPathLine(GpPath *, float, float, float, float);
int WINAPI GdipClosePathFigure(GpPath *);
int WINAPI GdipFillPath(GpGraphics *, GpBrush *, GpPath *);
int WINAPI GdipDrawPath(GpGraphics *, GpPen *, GpPath *);
int WINAPI GdipCreatePen1(DWORD, float, int, GpPen **);
int WINAPI GdipDeletePen(GpPen *);
int WINAPI GdipFillEllipse(GpGraphics *, GpBrush *, float, float, float, float);
int WINAPI GdipDrawEllipse(GpGraphics *, GpPen *, float, float, float, float);

#define GP_OK 0
#define GP_SMOOTHING_ANTIALIAS 4
#define GP_PIXEL_OFFSET_HALF 4
#define GP_FILL_ALTERNATE 0
#define GP_UNIT_WORLD 0

/* ---- theme ---------------------------------------------------------------- */

const theme DARK  = { RGB(13,16,34),  RGB(24,29,56),  RGB(40,47,86),  RGB(234,237,255),
                      RGB(138,146,190), RGB(44,51,92),  RGB(34,40,74),  RGB(48,56,100) };
const theme LIGHT = { RGB(236,239,247), RGB(255,255,255), RGB(220,225,239), RGB(26,30,52),
                      RGB(104,112,146), RGB(214,219,234), RGB(240,242,250), RGB(226,231,246) };
const COLORREF ACCENTS[] = { RGB(61,139,255), RGB(30,200,216), RGB(255,77,94),
                             RGB(60,207,122), RGB(255,154,60), RGB(167,123,255) };
const char *const ACCENT_NAMES[] = { "Blue", "Cyan", "Red", "Green", "Orange", "Purple" };

int Z(int v) { return MulDiv(v, ZOOMS[S.zoom], 100); }

COLORREF mix(COLORREF a, COLORREF b, int pct) {
    return RGB((GetRValue(a) * (100 - pct) + GetRValue(b) * pct) / 100,
               (GetGValue(a) * (100 - pct) + GetGValue(b) * pct) / 100,
               (GetBValue(a) * (100 - pct) + GetBValue(b) * pct) / 100);
}

static DWORD argb(COLORREF c) { return 0xff000000u | (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c); }

/* ---- drawing state -------------------------------------------------------- */

HDC g_dc;
HFONT f_body, f_small, f_bold, f_label, f_big;
HICON g_icon_big, g_icon_small;

static ULONG_PTR g_gp_token;
static GpGraphics *g_gp;        /* NULL: draw with plain GDI */

void draw_startup(void) {
    GpStartupInput gi = { 1, NULL, FALSE, FALSE };
    if (GdiplusStartup(&g_gp_token, &gi, NULL) != GP_OK) g_gp_token = 0;
}

void draw_begin(HDC dc) {
    g_dc = dc;
    g_gp = NULL;
    if (g_gp_token && GdipCreateFromHDC(dc, &g_gp) == GP_OK) {
        GdipSetSmoothingMode(g_gp, GP_SMOOTHING_ANTIALIAS);
        GdipSetPixelOffsetMode(g_gp, GP_PIXEL_OFFSET_HALF);
    } else {
        g_gp = NULL;
    }
}

void draw_end(void) {
    if (g_gp) GdipDeleteGraphics(g_gp);
    g_gp = NULL;
}

void make_fonts(void) {
    const char *face = g_wine ? "Tahoma" : "Segoe UI";
    HFONT *all[] = { &f_body, &f_small, &f_bold, &f_label, &f_big };
    for (int i = 0; i < 5; i++) if (*all[i]) DeleteObject(*all[i]);
    f_body  = CreateFontA(-Z(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    f_small = CreateFontA(-Z(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    f_bold  = CreateFontA(-Z(14), 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    f_label = CreateFontA(-Z(11), 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    f_big   = CreateFontA(-Z(26), 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

/* ---- the game's font ----------------------------------------------------
 * image\text.bmp is a 128x128 8-bit sheet of 8x8 glyphs; palette index 48
 * is the glyph and 13 its shadow. Used for the title. */

#define SHEET 128
#define GLYPH_INK 48
#define GLYPH_SHADOW 13

static unsigned char *g_sheet;   /* top-down rows of palette indexes */

void load_sheet(void) {
    char path[MAX_PATH];
    const release *r = release_for(1)->installed ? release_for(1) : release_for(0);
    if (!r->installed) return;
    port_join(path, r->dir, "image\\text.bmp");
    DWORD n = 0;
    BYTE *buf = port_read_file(path, &n);
    if (!buf) return;
    if (n >= 54 && buf[0] == 'B' && buf[1] == 'M') {
        DWORD off = *(DWORD *)(buf + 10);
        LONG w = *(LONG *)(buf + 18), h = *(LONG *)(buf + 22);
        WORD bpp = *(WORD *)(buf + 28);
        if (w == SHEET && h == SHEET && bpp == 8 && off + SHEET * SHEET <= n) {
            g_sheet = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, SHEET * SHEET);
            for (int y = 0; g_sheet && y < SHEET; y++)          /* BMP rows are bottom-up */
                memcpy(g_sheet + y * SHEET, buf + off + (SHEET - 1 - y) * SHEET, SHEET);
        }
    }
    port_free(buf);
}

int sheet_loaded(void) { return g_sheet != NULL; }

static int glyph_pos(char c, int *gx, int *gy) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'O') { *gx = 8 * (c - 'A' + 1); *gy = 32; return 1; }
    if (c >= 'P' && c <= 'Z') { *gx = 8 * (c - 'P'); *gy = 40; return 1; }
    if (c >= '0' && c <= '9') { *gx = 8 * (c - '0'); *gy = 64; return 1; }
    return 0;
}

/* Draws letters and digits in the game's font, px screen pixels per dot. */
void pixel_text(int x, int y, const char *s, int px, COLORREF ink, COLORREF shadow) {
    if (!g_sheet) return;
    for (int cx = x; *s; s++, cx += 8 * px) {
        int gx, gy;
        if (*s == ' ' || !glyph_pos(*s, &gx, &gy)) continue;
        for (int yy = 0; yy < 8; yy++)
            for (int xx = 0; xx < 8; xx++) {
                unsigned char i = g_sheet[(gy + yy) * SHEET + gx + xx];
                if (i == GLYPH_INK) fill(cx + xx * px, y + yy * px, px, px, ink);
                else if (i == GLYPH_SHADOW) fill(cx + xx * px, y + yy * px, px, px, shadow);
            }
    }
}

/* ---- primitives ----------------------------------------------------------- */

void fill(int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(g_dc, &r, b);
    DeleteObject(b);
}

/* UTF-8 text in a box; flags are DrawText's. */
void text(int x, int y, int w, int h, const char *s, HFONT f, COLORREF c, UINT flags) {
    WCHAR ws[256];
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, 256);
    if (n <= 0) return;
    RECT r = { x, y, x + w, y + h };
    HGDIOBJ old = SelectObject(g_dc, f);
    SetTextColor(g_dc, c);
    SetBkMode(g_dc, TRANSPARENT);
    DrawTextW(g_dc, ws, -1, &r, flags | DT_NOPREFIX);
    SelectObject(g_dc, old);
}

int text_width(const char *s, HFONT f) {
    WCHAR ws[64];
    SIZE sz = { 0, 0 };
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, 64) - 1;
    if (n <= 0) return 0;
    HGDIOBJ old = SelectObject(g_dc, f);
    GetTextExtentPoint32W(g_dc, ws, n, &sz);
    SelectObject(g_dc, old);
    return sz.cx;
}

static void gp_fill_path(GpPath *p, COLORREF c) {
    GpBrush *b = NULL;
    GdipCreateSolidFill(argb(c), &b);
    GdipFillPath(g_gp, b, p);
    GdipDeleteBrush(b);
}

static void gp_draw_path(GpPath *p, COLORREF c, float width) {
    GpPen *pen = NULL;
    GdipCreatePen1(argb(c), width, GP_UNIT_WORLD, &pen);
    GdipDrawPath(g_gp, pen, p);
    GdipDeletePen(pen);
}

static GpPath *round_path(float x, float y, float w, float h, float r) {
    GpPath *p = NULL;
    GdipCreatePath(GP_FILL_ALTERNATE, &p);
    float d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    GdipAddPathArc(p, x, y, d, d, 180, 90);
    GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
    return p;
}

/* Rounded rectangle; the border is drawn only if it differs from the fill. */
void rrect(int x, int y, int w, int h, int r, COLORREF c, COLORREF border) {
    if (g_gp) {
        GpPath *p = round_path((float)x, (float)y, (float)w - 1, (float)h - 1, (float)r);
        gp_fill_path(p, c);
        if (border != c) gp_draw_path(p, border, Z(100) >= 150 ? 1.5f : 1.0f);
        GdipDeletePath(p);
    } else {
        HBRUSH b = CreateSolidBrush(c);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ ob = SelectObject(g_dc, b), op = SelectObject(g_dc, pen);
        RoundRect(g_dc, x, y, x + w, y + h, r * 2, r * 2);
        SelectObject(g_dc, ob);
        SelectObject(g_dc, op);
        DeleteObject(b);
        DeleteObject(pen);
    }
}

void circle(int cx, int cy, int r, COLORREF c) {
    if (g_gp) {
        GpBrush *b = NULL;
        GdipCreateSolidFill(argb(c), &b);
        GdipFillEllipse(g_gp, b, (float)(cx - r), (float)(cy - r), (float)(2 * r), (float)(2 * r));
        GdipDeleteBrush(b);
    } else {
        HBRUSH b = CreateSolidBrush(c);
        HGDIOBJ ob = SelectObject(g_dc, b), op = SelectObject(g_dc, GetStockObject(NULL_PEN));
        Ellipse(g_dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
        SelectObject(g_dc, ob);
        SelectObject(g_dc, op);
        DeleteObject(b);
    }
}

/* The shapes below are decoration and are skipped without GDI+. */

void ring(int cx, int cy, int r, COLORREF c, float width) {
    if (!g_gp) return;
    GpPen *pen = NULL;
    GdipCreatePen1(argb(c), width, GP_UNIT_WORLD, &pen);
    GdipDrawEllipse(g_gp, pen, (float)(cx - r), (float)(cy - r), (float)(2 * r), (float)(2 * r));
    GdipDeletePen(pen);
}

void play_triangle(int x, int y, int s, COLORREF c) {
    if (!g_gp) return;
    GpPath *p = NULL;
    GdipCreatePath(GP_FILL_ALTERNATE, &p);
    GdipAddPathLine(p, (float)x, (float)y, (float)(x + s), (float)y + s / 2.0f);
    GdipAddPathLine(p, (float)(x + s), (float)y + s / 2.0f, (float)x, (float)(y + s));
    GdipClosePathFigure(p);
    gp_fill_path(p, c);
    GdipDeletePath(p);
}

void chevron(int x, int y, int s, COLORREF c) {
    if (!g_gp) return;
    GpPath *p = NULL;
    GdipCreatePath(GP_FILL_ALTERNATE, &p);
    GdipAddPathLine(p, (float)x, (float)y, (float)x + s / 2.0f, (float)y + s / 2.0f);
    GdipAddPathLine(p, (float)x + s / 2.0f, (float)y + s / 2.0f, (float)(x + s), (float)y);
    gp_draw_path(p, c, (float)Z(2));
    GdipDeletePath(p);
}
