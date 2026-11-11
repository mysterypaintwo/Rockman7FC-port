/* ---------------------------------------------------------------------------
 * The game window: quitting and display scaling
 *
 * The game's main window is a Delphi form of class TForm1.
 *
 * Quitting: the form's window procedure is wrapped so that every way of
 * asking it to close works, including macOS Cmd+Q (Wine delivers it as
 * Alt+Q, or as WM_QUERYENDSESSION / WM_ENDSESSION) and closing a borderless
 * fullscreen window.
 *
 * Scaling: every frame the game copies its finished picture to the form with
 * one 1:1 StretchDIBits call. For sizes the engine does not offer (above 3x,
 * fullscreen), the engine runs at 1x and that call is stretched: to an
 * integer multiple with nearest-neighbour sampling, centred, with black bars
 * in fullscreen (or to the largest 8:7 rectangle with Fullscreen=2). The form
 * is resized whenever the game sizes it.
 * ------------------------------------------------------------------------- */
#include "proxy.h"

static int is_game_form(HWND w) {
    char cls[16];
    return w && GetClassNameA(w, cls, sizeof cls) && !lstrcmpA(cls, "TForm1");
}

/* ---- quitting ------------------------------------------------------------ */

static WNDPROC g_form_proc;
static HWND g_form;

static int on_mac(void) {
    static int mac = -1;                       /* Wine on a macOS host */
    if (mac < 0) {
        const char *host = port_wine_host();
        mac = host && port_ieq(host, "Darwin");
    }
    return mac;
}

__attribute__((noreturn)) static void quit(const char *why) {
    plog("quit: %s", why);
    ExitProcess(0);
}

/* Cmd+Q under Wine on macOS: a WM_SYSKEYDOWN for Q (Alt+Q), followed by the
 * SC_KEYMENU accelerator it triggers. */
static int is_cmd_q(UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_SYSKEYDOWN) {
        if ((wp & 0xff) != 'Q') return 0;
    } else if (m == WM_SYSCOMMAND) {
        if ((wp & 0xfff0) != SC_KEYMENU || (lp != 'q' && lp != 'Q')) return 0;
    } else {
        return 0;
    }
    return on_mac();
}

static LRESULT CALLBACK form_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (g_trace && (m == WM_CLOSE || m == WM_QUERYENDSESSION || m == WM_ENDSESSION || m == WM_SYSCOMMAND ||
                    m == WM_DESTROY || m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_ACTIVATEAPP))
        plog("trace: window message %04x wp=%lx lp=%lx", m, (unsigned long)wp, (unsigned long)lp);
    switch (m) {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) quit("session end requested");
        break;
    case WM_CLOSE: {
        plog("quit: window close requested");
        LRESULT r = CallWindowProcA(g_form_proc, w, m, wp, lp);
        if (IsWindow(w)) ExitProcess(0);     /* the game ignored it */
        return r;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_CLOSE) quit("close command");
        break;
    }
    if (is_cmd_q(m, wp, lp)) quit("Cmd+Q");
    return CallWindowProcA(g_form_proc, w, m, wp, lp);
}

static void watch_form(HWND w) {
    if (g_form || !w) return;
    HWND top = GetAncestor(w, GA_ROOT);
    if (!is_game_form(top)) return;
    g_form = top;
    g_form_proc = (WNDPROC)SetWindowLongPtrA(top, GWLP_WNDPROC, (LONG_PTR)form_proc);
    plog("window: game window found, quit handling on");
}

void window_check_message(const MSG *m) {
    if (!g_form) watch_form(m->hwnd);
    /* Cmd+Q is caught here as well, before key remapping could change it */
    if (g_form && m->message == WM_SYSKEYDOWN && is_cmd_q(m->message, m->wParam, m->lParam))
        quit("Cmd+Q");
}

/* ---- display scaling ----------------------------------------------------- */

typedef int  (WINAPI *PFN_STRETCHDIB)(HDC, int, int, int, int, int, int, int, int, const void *, const BITMAPINFO *, UINT, DWORD);
typedef BOOL (WINAPI *PFN_SETWINDOWPOS)(HWND, HWND, int, int, int, int, UINT);
static PFN_STRETCHDIB real_StretchDIBits;
static PFN_SETWINDOWPOS real_SetWindowPos;
static int g_scale;            /* custom integer scale, 0 = engine's own */
static int g_fullscreen;       /* 0 window, 1 integer, 2 stretched */
static volatile LONG g_trace_n;
static volatile LONG g_fixing; /* inside fix_window: ignore our own SetWindowPos */

static int min_int(int a, int b) { return a < b ? a : b; }

/* Largest integer scale whose window fits the monitor's work area (or the
 * whole monitor in fullscreen). */
static int fit_scale(HWND w, int want, RECT *area) {
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoA(mon, &mi)) { SetRect(&mi.rcWork, 0, 0, 1920, 1040); mi.rcMonitor = mi.rcWork; }
    *area = g_fullscreen ? mi.rcMonitor : mi.rcWork;
    int aw = area->right - area->left, ah = area->bottom - area->top;
    if (g_fullscreen) {
        int n = min_int(aw / GAME_W, ah / GAME_H);
        return n < 1 ? 1 : n;
    }
    RECT fr = { 0, 0, 0, 0 };
    AdjustWindowRectEx(&fr, (DWORD)GetWindowLongA(w, GWL_STYLE), FALSE, (DWORD)GetWindowLongA(w, GWL_EXSTYLE));
    int n = want;
    while (n > 1 && (GAME_W * n + (fr.right - fr.left) > aw || GAME_H * n + (fr.bottom - fr.top) > ah)) n--;
    return n;
}

static void fix_window(HWND w) {
    if (InterlockedExchange(&g_fixing, 1)) return;
    RECT area;
    if (g_fullscreen) {
        LONG st = GetWindowLongA(w, GWL_STYLE);
        LONG nst = (st & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP;
        if (nst != st) SetWindowLongA(w, GWL_STYLE, nst);
        fit_scale(w, g_scale, &area);
        real_SetWindowPos(w, NULL, area.left, area.top, area.right - area.left, area.bottom - area.top,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        int n = fit_scale(w, g_scale, &area);
        RECT r = { 0, 0, GAME_W * n, GAME_H * n };
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongA(w, GWL_STYLE), FALSE, (DWORD)GetWindowLongA(w, GWL_EXSTYLE));
        int ww = r.right - r.left, wh = r.bottom - r.top;
        int x = area.left + (area.right - area.left - ww) / 2, y = area.top + (area.bottom - area.top - wh) / 2;
        real_SetWindowPos(w, NULL, x < area.left ? area.left : x, y < area.top ? area.top : y, ww, wh,
                          SWP_NOZORDER | SWP_NOACTIVATE);
        static int logged;
        if (!logged++) plog("display: %dx window (%dx%d)%s", n, GAME_W * n, GAME_H * n,
                            n < g_scale ? ", reduced to fit the screen" : "");
    }
    InterlockedExchange(&g_fixing, 0);
}

static BOOL WINAPI hk_SetWindowPos(HWND wnd, HWND after, int x, int y, int cx, int cy, UINT f) {
    BOOL r = real_SetWindowPos(wnd, after, x, y, cx, cy, f);
    if (g_trace) plog("trace: SetWindowPos %08lx %d,%d %dx%d flags=%x", (unsigned long)(ULONG_PTR)wnd, x, y, cx, cy, f);
    if (g_scale && !(f & SWP_NOSIZE) && !g_fixing && is_game_form(wnd)) fix_window(wnd);
    return r;
}

/* Where the picture goes inside the client area: an integer multiple
 * (windows, Fullscreen=1) or the largest 8:7 rectangle that fits
 * (Fullscreen=2, stretched). */
static void game_area(HWND wnd, RECT *g) {
    RECT c;
    GetClientRect(wnd, &c);
    int gw, gh;
    if (g_fullscreen == 2) {
        if ((long long)c.right * GAME_H > (long long)c.bottom * GAME_W) { gh = c.bottom; gw = c.bottom * GAME_W / GAME_H; }
        else { gw = c.right; gh = c.right * GAME_H / GAME_W; }
    } else {
        int n = min_int(c.right / GAME_W, c.bottom / GAME_H);
        if (n < 1) n = 1;
        gw = GAME_W * n;
        gh = GAME_H * n;
    }
    g->left = (c.right - gw) / 2;
    g->top = (c.bottom - gh) / 2;
    g->right = g->left + gw;
    g->bottom = g->top + gh;
}

static void fill_black(HDC d, int l, int t, int r, int b) {
    if (r <= l || b <= t) return;
    RECT rc = { l, t, r, b };
    FillRect(d, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
}

static int WINAPI hk_StretchDIBits(HDC d, int x, int y, int w, int h, int sx, int sy, int sw, int sh,
                                   const void *bits, const BITMAPINFO *bi, UINT use, DWORD rop) {
    HWND wnd = WindowFromDC(d);
    if (g_trace && InterlockedIncrement(&g_trace_n) < 200)
        plog("trace: StretchDIBits %d,%d %dx%d src %dx%d form=%d", x, y, w, h, sw, sh, is_game_form(wnd));
    /* The game copies its frame 1:1, at 0,0 normally and at a small offset
     * while the screen shakes. Both are mapped into the game area. */
    if (g_scale && w == sw && h == sh && is_game_form(wnd)) {
        RECT c, g;
        GetClientRect(wnd, &c);
        game_area(wnd, &g);
        int gw = g.right - g.left, gh = g.bottom - g.top;
        int dl = g.left + MulDiv(x, gw, GAME_W), dt = g.top + MulDiv(y, gh, GAME_H);
        int dr = g.left + MulDiv(x + w, gw, GAME_W), db = g.top + MulDiv(y + h, gh, GAME_H);
        /* letterbox bars */
        fill_black(d, 0, 0, c.right, g.top);
        fill_black(d, 0, g.bottom, c.right, c.bottom);
        fill_black(d, 0, g.top, g.left, g.bottom);
        fill_black(d, g.right, g.top, c.right, g.bottom);
        /* what a shake leaves uncovered */
        fill_black(d, g.left, g.top, g.right, dt);
        fill_black(d, g.left, db, g.right, g.bottom);
        fill_black(d, g.left, dt, dl, db);
        fill_black(d, dr, dt, g.right, db);
        int saved = SaveDC(d);
        IntersectClipRect(d, g.left, g.top, g.right, g.bottom);
        SetStretchBltMode(d, COLORONCOLOR);
        int r = real_StretchDIBits(d, dl, dt, dr - dl, db - dt, sx, sy, sw, sh, bits, bi, use, rop);
        if (saved) RestoreDC(d, saved);
        return r;
    }
    return real_StretchDIBits(d, x, y, w, h, sx, sy, sw, sh, bits, bi, use, rop);
}

void install_display_hooks(void) {
    int want = ini_int("Display", "Scale", 3, 1, 16);
    g_fullscreen = ini_int("Display", "Fullscreen", 0, 0, 2);
    g_scale = (want > 3 || g_fullscreen) ? want : 0;
    if (!g_scale && !g_trace) return;
    HMODULE g = GetModuleHandleA("gdi32.dll"), u = GetModuleHandleA("user32.dll");
    if (!g || !u) { g_scale = 0; return; }
    real_StretchDIBits = (PFN_STRETCHDIB)(void *)GetProcAddress(g, "StretchDIBits");
    real_SetWindowPos = (PFN_SETWINDOWPOS)(void *)GetProcAddress(u, "SetWindowPos");
    if (!real_StretchDIBits || !real_SetWindowPos) { g_scale = 0; return; }
    int a = patch_iat((void *)real_StretchDIBits, (void *)hk_StretchDIBits);
    int b = patch_iat((void *)real_SetWindowPos, (void *)hk_SetWindowPos);
    if (g_scale) plog("display: %s, scale %dx (hooks %d/%d)",
                      g_fullscreen == 2 ? "fullscreen stretched" : g_fullscreen ? "fullscreen integer" : "window", want, a, b);
    if (!a || !b) plog("display: hooks missing, using the engine's 1x");
}
