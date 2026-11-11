/* ---------------------------------------------------------------------------
 * Widgets
 *
 * The window has no child controls (except the inline number editor). Each
 * paint draws every widget and records a "hit" rectangle for it; mouse input
 * is matched against those. Hits added later sit on top.
 * ------------------------------------------------------------------------- */
#include "settings.h"

ui_state U = { .hover = -1, .drag = -1, .listen_a = -1, .pad_src = -1, .max_scale = 3 };

/* ---- hits and status ---------------------------------------------------- */

int add_hit(int x, int y, int w, int h, int kind, int a, int b, int enabled) {
    if (U.nhits >= MAX_HITS) return -1;
    hit *t = &U.hits[U.nhits];
    SetRect(&t->r, x, y, x + w, y + h);
    t->kind = kind;
    t->a = a;
    t->b = b;
    t->enabled = enabled;
    return U.nhits++;
}

int hit_at(int x, int y) {
    POINT p = { x, y };
    for (int i = U.nhits - 1; i >= 0; i--) if (PtInRect(&U.hits[i].r, p)) return i;
    return -1;
}

int is_hover(int kind, int a, int b) {
    if (U.hover < 0) return 0;
    const hit *h = &U.hits[U.hover];
    return h->kind == kind && h->a == a && h->b == b;
}

void status(const char *msg, int err) {
    lstrcpynA(U.status, msg, sizeof U.status);
    U.status_err = err;
    if (g_wnd) InvalidateRect(g_wnd, NULL, FALSE);
}

/* ---- simple widgets ------------------------------------------------------- */

void card(int x, int y, int w, int h, const char *title) {
    rrect(x, y, w, h, Z(12), T->card, T->edge);
    char up[64];
    lstrcpynA(up, title, sizeof up);
    CharUpperA(up);
    text(x + Z(18), y + Z(14), w - Z(36), Z(16), up, f_label, T->muted, DT_LEFT | DT_SINGLELINE);
}

/* A row's label, with an optional smaller line under it. */
void row_label(int x, int y, int h, const char *label, const char *sub) {
    if (sub) {
        text(x, y + h / 2 - Z(17), Z(150), Z(18), label, f_body, T->text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        text(x, y + h / 2 + Z(1), Z(150), Z(16), sub, f_small, T->muted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    } else {
        text(x, y, Z(150), h, label, f_body, T->text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

/* n side-by-side choices; enabled may be NULL (all enabled). */
void segmented(int x, int y, int w, int h, int group, const char *const *labels,
               const int *enabled, int n, int sel) {
    rrect(x, y, w, h, Z(8), T->chip, T->edge);
    int seg = w / n;
    for (int i = 0; i < n; i++) {
        int sx = x + i * seg, sw = (i == n - 1) ? w - i * seg : seg;
        int en = enabled ? enabled[i] : 1;
        if (i == sel) rrect(sx + Z(3), y + Z(3), sw - Z(6), h - Z(6), Z(6), ACC, ACC);
        else if (en && is_hover(H_SEG, group, i)) rrect(sx + Z(3), y + Z(3), sw - Z(6), h - Z(6), Z(6), T->chip_hi, T->chip_hi);
        COLORREF c = i == sel ? WHITE : en ? T->text : mix(T->muted, T->chip, 45);
        text(sx, y, sw, h, labels[i], i == sel ? f_bold : f_body, c, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        add_hit(sx, y, sw, h, H_SEG, group, i, en);
    }
}

void toggle(int x, int y, int id, int on) {
    int w = Z(42), h = Z(24);
    COLORREF track = on ? ACC : T->track;
    if (is_hover(H_TOGGLE, id, 0)) track = mix(track, T->text, 12);
    rrect(x, y, w, h, h / 2, track, track);
    int r = h / 2 - Z(3);
    circle(on ? x + w - h / 2 : x + h / 2, y + h / 2, r, WHITE);
    add_hit(x, y, w, h, H_TOGGLE, id, 0, 1);
}

/* 0..100. The hit is wider than the track so the knob can be grabbed at the
 * ends; window.c's set_slider undoes that margin. */
void slider(int x, int y, int w, int id, int value, int enabled) {
    int h = Z(28), cy = y + h / 2, th = Z(6);
    COLORREF fillc = enabled ? ACC : mix(T->muted, T->card, 40);
    rrect(x, cy - th / 2, w, th, th / 2, T->track, T->track);
    int px = x + MulDiv(w, value, 100);
    if (px - x >= th) rrect(x, cy - th / 2, px - x, th, th / 2, fillc, fillc);
    int r = (U.drag == id || is_hover(H_SLIDER, id, 0)) ? Z(9) : Z(8);
    circle(px, cy, r, WHITE);
    ring(px, cy, r, fillc, (float)Z(2));
    add_hit(x - Z(8), y, w + Z(16), h, H_SLIDER, id, 0, 1);
}

void key_chip(int x, int y, int w, int h, int a, int s) {
    int v = U.tab ? S.pad[a][s] : S.keys[a][s];
    int listening = U.listen_a == a && U.listen_s == s;
    int hover = is_hover(H_KEY, a, s);
    char name[32];
    if (listening) {
        rrect(x, y, w, h, Z(7), mix(T->chip, ACC, 18), ACC);
        text(x, y, w, h, U.tab ? "Press a button…" : "Press a key…", f_small, ACC, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    } else if (v) {
        rrect(x, y, w, h, Z(7), hover ? T->chip_hi : T->chip, hover ? ACC : T->edge);
        if (U.tab) lstrcpynA(name, PAD_NAMES[v - 1][1], sizeof name);
        else port_key_name(v, name);
        text(x, y, w, h, name, U.tab ? f_small : f_bold, T->text, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    } else {
        rrect(x, y, w, h, Z(7), hover ? T->chip_hi : T->card, hover ? ACC : T->edge);
        text(x, y, w, h, s ? "+ alt" : "unbound", f_small, T->muted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    add_hit(x, y, w, h, H_KEY, a, s, 1);
}

void button(int x, int y, int w, int h, int id, const char *label, int primary) {
    int hover = is_hover(H_BUTTON, id, 0);
    COLORREF bg = primary ? ACC : T->chip;
    if (hover) bg = primary ? mix(ACC, WHITE, 14) : T->chip_hi;
    rrect(x, y, w, h, Z(9), bg, primary ? bg : T->edge);
    COLORREF tc = primary ? WHITE : T->text;
    if (id == B_PLAY) {                          /* ▶ Play, centred together */
        int ts = Z(11), tw = text_width(label, f_bold);
        int sx = x + (w - (ts + Z(9) + tw)) / 2;
        play_triangle(sx, y + (h - ts) / 2, ts, tc);
        text(sx + ts + Z(9), y, tw + Z(4), h, label, f_bold, tc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    } else {
        text(x, y, w, h, label, f_bold, tc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    add_hit(x, y, w, h, H_BUTTON, id, 0, 1);
}

/* ---- dropdowns ------------------------------------------------------------
 * A dropdown's list is built once when it opens (listing controllers is
 * slow), and the same list is used for drawing and for picking. */

static BOOL CALLBACK monitor_cb(HMONITOR m, HDC dc, LPRECT r, LPARAM lp) {
    (void)dc; (void)r; (void)lp;
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(m, &mi)) return TRUE;
    /* leave room for the title bar and window frame */
    int w = mi.rcWork.right - mi.rcWork.left - 16, h = mi.rcWork.bottom - mi.rcWork.top - 40;
    int n = w / GAME_W < h / GAME_H ? w / GAME_W : h / GAME_H;
    if (n > U.max_scale) U.max_scale = n;
    return TRUE;
}

void measure_monitors(void) {
    U.max_scale = 3;
    EnumDisplayMonitors(NULL, NULL, monitor_cb, 0);
    if (U.max_scale > 16) U.max_scale = 16;
}

static drop_item *add_item(int value, int enabled) {
    if (U.nitems >= MAX_DROP_ITEMS) return NULL;
    drop_item *it = &U.items[U.nitems++];
    it->value = value;
    it->enabled = enabled;
    it->label[0] = 0;
    return it;
}

/* Window sizes, then the two fullscreen modes (values 0 and -1). */
static void build_size_items(void) {
    int top = U.max_scale > S.scale ? U.max_scale : S.scale;
    drop_item *it;
    for (int k = 1; k <= top && U.nitems < MAX_DROP_ITEMS - 2; k++)
        if ((it = add_item(k, 1)))
            wsprintfA(it->label, "%d×   %d × %d%s", k, GAME_W * k, GAME_H * k, k > U.max_scale ? "  (too big)" : "");
    if ((it = add_item(0, 1))) lstrcpyA(it->label, "Fullscreen (Integer)");
    if ((it = add_item(-1, 1))) lstrcpyA(it->label, "Fullscreen (Stretched)");
}

/* "Any controller", the connected ones, and the saved one if it is gone. */
static void build_device_items(void) {
    pad_entry pe[12];
    int found = pad_list_devices(pe, 12), have_sel = S.pad_dev == PAD_ANY;
    drop_item *it;
    if ((it = add_item(PAD_ANY, 1))) lstrcpyA(it->label, "Any controller");
    for (int i = 0; i < found && U.nitems < MAX_DROP_ITEMS - 1; i++) {
        if ((it = add_item(pe[i].dev, 1))) lstrcpynA(it->label, pe[i].name, sizeof it->label);
        if (pe[i].dev == S.pad_dev) have_sel = 1;
    }
    if (!have_sel && (it = add_item(S.pad_dev, 1)))
        wsprintfA(it->label, "%s (not connected)", S.pad_dev_name[0] ? S.pad_dev_name : "Saved controller");
}

void open_dropdown(int id) {
    U.nitems = 0;
    if (id == D_SIZE) {
        measure_monitors();
        build_size_items();
    } else if (id == D_DEVICE) {
        build_device_items();
    }
    U.drop = id;
}

void dropdown(int x, int y, int w, int h, int id, const char *label) {
    int open = U.drop == id, hover = is_hover(H_DROP, id, 0);
    rrect(x, y, w, h, Z(8), hover || open ? T->chip_hi : T->chip, open ? ACC : T->edge);
    text(x + Z(12), y, w - Z(36), h, label, f_body, T->text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    chevron(x + w - Z(22), y + h / 2 - Z(3), Z(10), T->muted);
    if (open) SetRect(&U.drop_rect, x, y, x + w, y + h);
    add_hit(x, y, w, h, H_DROP, id, 0, 1);
}

/* The open list. Drawn last so it sits on top of everything (and its hits
 * win); opens upwards if there is no room below. */
void dropdown_list(int ch) {
    if (!U.drop) return;
    int n = U.nitems;
    int ih = Z(30), x = U.drop_rect.left, w = U.drop_rect.right - U.drop_rect.left;
    if (U.drop == D_DEVICE && w < Z(260)) w = Z(260);
    int hgt = n * ih + Z(8), y = U.drop_rect.bottom + Z(4);
    if (y + hgt > ch - Z(8)) y = U.drop_rect.top - Z(4) - hgt;
    if (y < Z(8)) y = Z(8);
    COLORREF shadow = mix(T->bg, RGB(0, 0, 0), 40);
    rrect(x + Z(2), y + Z(3), w, hgt, Z(10), shadow, shadow);
    rrect(x, y, w, hgt, Z(10), T->card, ACC);
    int cur = U.drop == D_SIZE ? (S.full == 2 ? -1 : S.full ? 0 : S.scale) : S.pad_dev;
    for (int i = 0; i < n; i++) {
        const drop_item *it = &U.items[i];
        int iy = y + Z(4) + i * ih;
        int sel = it->value == cur, hov = is_hover(H_DROPITEM, U.drop, i);
        if (sel || hov) rrect(x + Z(4), iy, w - Z(8), ih, Z(6), sel ? ACC : T->chip_hi, sel ? ACC : T->chip_hi);
        COLORREF c = sel ? WHITE : it->enabled ? T->text : T->muted;
        text(x + Z(14), iy, w - Z(28), ih, it->label, sel ? f_bold : f_body, c, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        add_hit(x + Z(4), iy, w - Z(8), ih, H_DROPITEM, U.drop, i, it->enabled);
    }
    /* swallow clicks on the list's padding */
    add_hit(x, y, w, Z(4), H_DROPITEM, U.drop, -1, 0);
}
