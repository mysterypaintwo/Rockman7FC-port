/* ---------------------------------------------------------------------------
 * Layout: what the window shows and where
 *
 *   header     icon, title, "Restore defaults"
 *   left       Display, Sound, Wine (macOS/Linux only)
 *   right      Controls (keyboard / gamepad tabs), Appearance
 *   footer     status line, Add release, Close, Play
 *
 * All sizes are at 100% zoom and scaled with Z().
 * ------------------------------------------------------------------------- */
#include "settings.h"

#define MARGIN   28
#define COL_W    352     /* width of each column */
#define CARD_PAD 18
#define CARD_GAP 14
#define LABEL_W  140     /* left part of a row, before its control */

static void header(int cw) {
    int hx = Z(MARGIN), hy = Z(24), tx = hx + Z(64);
    if (g_icon_big) DrawIconEx(g_dc, hx, hy, g_icon_big, Z(48), Z(48), 0, NULL, DI_NORMAL);
    if (sheet_loaded())
        pixel_text(tx, hy, "ROCKMAN 7 FC", Z(3), T->text, S.dark ? RGB(0, 0, 0) : mix(T->text, T->bg, 70));
    else
        text(tx, hy - Z(4), Z(400), Z(32), "Rockman 7 FC", f_big, T->text, DT_LEFT | DT_SINGLELINE);
    text(tx, hy + Z(30), Z(400), Z(20), "Port settings  ·  changes save automatically", f_body, T->muted,
         DT_LEFT | DT_SINGLELINE);
    fill(Z(MARGIN), Z(92), cw - Z(2 * MARGIN), Z(3), ACC);
    button(cw - Z(MARGIN) - Z(170), Z(30), Z(170), Z(36), B_DEFAULTS,
           U.confirm_reset ? "Click again to confirm" : "Restore defaults", 0);
}

/* Returns the card's height. */
static int display_card(int x, int y) {
    int h = Z(146), cx = x + Z(CARD_PAD), cw = Z(COL_W) - 2 * Z(CARD_PAD);
    card(x, y, Z(COL_W), h, "Display");

    char lbl[64], sub[64];
    if (S.full == 2) { lstrcpyA(lbl, "Fullscreen (Stretched)"); lstrcpyA(sub, "fills the screen, 8:7 kept"); }
    else if (S.full) { lstrcpyA(lbl, "Fullscreen (Integer)"); lstrcpyA(sub, "pixel-perfect, black bars"); }
    else {
        wsprintfA(lbl, "%d×   %d × %d", S.scale, GAME_W * S.scale, GAME_H * S.scale);
        wsprintfA(sub, "up to %d× fits your screen", U.max_scale);
    }
    row_label(cx, y + Z(40), Z(44), "Window size", sub);
    dropdown(cx + Z(LABEL_W), y + Z(46), cw - Z(LABEL_W), Z(34), D_SIZE, lbl);

    static const char *const langs[] = { "Japanese", "English" };
    const release *ja = release_for(0), *en = release_for(1), *cur = release_for(S.lang_en);
    int have[2] = { ja->installed, en->installed };
    char lsub[64];
    if (cur->ver[0] && cur->year) wsprintfA(lsub, "%s (%d)", cur->ver, cur->year);
    else lstrcpyA(lsub, have[0] && have[1] ? "text and menus" : "add a release for more");
    row_label(cx, y + Z(90), Z(44), "Language", lsub);
    segmented(cx + Z(LABEL_W), y + Z(96), cw - Z(LABEL_W), Z(34), G_LANG, langs, have, 2, S.lang_en);
    return h;
}

/* A toggle, its name, a volume slider and the percentage (double-click to
 * type a value). */
static void volume_row(int cx, int cw, int y, int id, const char *name, int on, int vol) {
    char pct[16];
    toggle(cx, y + Z(10), id, on);
    text(cx + Z(54), y, Z(90), Z(44), name, f_body, T->text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    slider(cx + Z(126), y + Z(8), cw - Z(126) - Z(66), id, vol, on);
    wsprintfA(pct, "%d%%", vol);
    text(cx + cw - Z(54), y, Z(54), Z(44), pct, f_bold, on ? T->text : T->muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    add_hit(cx + cw - Z(54), y + Z(8), Z(54), Z(28), H_PCT, id, 0, 1);
}

static int sound_card(int x, int y) {
    int h = Z(198), cx = x + Z(CARD_PAD), cw = Z(COL_W) - 2 * Z(CARD_PAD), ry = y + Z(42);
    card(x, y, Z(COL_W), h, "Sound");

    static const char *const tracks[] = { "Original", "Updated" };
    const release *ja = release_for(0), *en = release_for(1);
    int have[2] = { ja->installed, en->installed };
    int yr = S.track_en ? en->year : ja->year;
    char tsub[64];
    if (yr) wsprintfA(tsub, "%s release, %d", S.track_en ? "English" : "Japanese", yr);
    else lstrcpyA(tsub, S.track_en ? "English release" : "Japanese release");
    row_label(cx, ry, Z(44), "Soundtrack", tsub);
    segmented(cx + Z(LABEL_W), ry + Z(6), cw - Z(LABEL_W), Z(32), G_TRACK, tracks, have, 2, S.track_en);
    ry += Z(48);

    volume_row(cx, cw, ry, O_MUSIC, "BGM", S.music, S.music_vol);
    ry += Z(48);
    volume_row(cx, cw, ry, O_FX, "SFX", S.fx, S.fx_vol);
    return h;
}

static void wine_card(int x, int y) {
    int h = g_linux ? Z(186) : Z(140), cx = x + Z(CARD_PAD), cw = Z(COL_W) - 2 * Z(CARD_PAD);
    int ry = y + Z(40);
    card(x, y, Z(COL_W), h, g_linux ? "Linux · Wine" : "macOS · Wine");
    row_label(cx, ry, Z(44), "Desktop window", "fixes window glitches");
    toggle(cx + cw - Z(42), ry + Z(10), O_VDESK, S.vdesk);
    ry += Z(48);
    static const char *const renderers[] = { "Default", "GDI" };
    row_label(cx, ry, Z(44), "Renderer", "GDI fixes black screens");
    segmented(cx + Z(LABEL_W), ry + Z(6), cw - Z(LABEL_W), Z(32), G_RENDER, renderers, NULL, 2, S.gdi);
    if (g_linux) {
        ry += Z(48);
        row_label(cx, ry, Z(44), "Gamescope", "integer-scaled fullscreen");
        toggle(cx + cw - Z(42), ry + Z(10), O_GAMESCOPE, S.gamescope);
    }
}

static int controls_card(int x, int y) {
    int h = Z(84) + PORT_ACTION_COUNT * Z(32) + Z(40);
    int kx = x + Z(CARD_PAD), kw = Z(COL_W) - 2 * Z(CARD_PAD), ry = y + Z(46);
    int chip_w = Z(92), gap = Z(8);
    card(x, y, Z(COL_W), h, "Controls");

    static const char *const tabs[] = { "Keyboard", "Gamepad" };
    segmented(x + Z(COL_W) - Z(CARD_PAD) - Z(190), y + Z(8), Z(190), Z(30), G_TAB, tabs, NULL, 2, U.tab);
    text(kx + kw - 2 * chip_w - gap, ry, chip_w, Z(16), U.tab ? "BUTTON" : "KEY", f_label, T->muted, DT_CENTER | DT_SINGLELINE);
    text(kx + kw - chip_w, ry, chip_w, Z(16), "ALTERNATE", f_label, T->muted, DT_CENTER | DT_SINGLELINE);
    ry += Z(22);
    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        text(kx, ry, kw - 2 * chip_w - gap, Z(30), PORT_ACTIONS[a].label, f_body,
             U.tab && !S.pad_on ? T->muted : T->text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        key_chip(kx + kw - 2 * chip_w - gap, ry + Z(2), chip_w, Z(26), a, 0);
        key_chip(kx + kw - chip_w, ry + Z(2), chip_w, Z(26), a, 1);
        ry += Z(32);
    }
    ry += Z(8);

    if (U.tab) {
        toggle(kx, ry + Z(2), O_PAD, S.pad_on);
        char lbl[128];
        if (S.pad_dev == PAD_ANY)
            lstrcpyA(lbl, U.pad_src > 0 ? "Any controller (found)" : "Any controller (none yet)");
        else
            lstrcpynA(lbl, S.pad_dev_name[0] ? S.pad_dev_name : "Selected controller", sizeof lbl);
        dropdown(kx + Z(52), ry, kw - Z(52) - Z(76), Z(28), D_DEVICE, lbl);
    } else {
        int listening = U.listen_a >= 0;
        text(kx, ry, kw - Z(130), Z(28), listening ? "Esc cancels  ·  Delete clears" : "Click a slot to change it",
             f_small, listening ? ACC : T->muted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    text(kx + kw - Z(66), ry, Z(66), Z(28), "Defaults", f_small, is_hover(H_RESETKEYS, 0, 0) ? ACC : T->muted,
         DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    add_hit(kx + kw - Z(66), ry, Z(66), Z(28), H_RESETKEYS, 0, 0, 1);
    return h;
}

static void appearance_card(int x, int y) {
    int ax = x + Z(CARD_PAD), aw = Z(COL_W) - 2 * Z(CARD_PAD), ry = y + Z(38);
    card(x, y, Z(COL_W), Z(118), "Appearance");
    static const char *const themes[] = { "Dark", "Light" };
    segmented(ax, ry + Z(4), Z(120), Z(30), G_THEME, themes, NULL, 2, S.dark ? 0 : 1);
    int sx = ax + Z(136);
    for (int i = 0; i < ACCENT_COUNT; i++) {
        int ccx = sx + Z(12) + i * Z(30), ccy = ry + Z(19);
        if (i == S.accent) ring(ccx, ccy, Z(12), T->text, (float)Z(2));
        circle(ccx, ccy, Z(9), ACCENTS[i]);
        add_hit(ccx - Z(13), ccy - Z(13), Z(26), Z(26), H_SWATCH, i, 0, 1);
    }
    ry += Z(40);
    text(ax, ry, Z(100), Z(32), "Zoom", f_body, T->text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    static const char *const zooms[ZOOM_COUNT] = { "100%", "125%", "150%", "200%" };
    segmented(ax + Z(80), ry, aw - Z(80), Z(30), G_ZOOM, zooms, NULL, ZOOM_COUNT, S.zoom);
}

static void footer(int cw, int ch) {
    int fy = ch - Z(72);
    fill(0, fy, cw, 1, T->edge);
    text(Z(MARGIN), fy + 1, cw - Z(MARGIN) - Z(420), Z(71), U.status, f_small, U.status_err ? ERROR_RED : T->muted,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    int bx = cw - Z(MARGIN);
    bx -= Z(130);         button(bx, fy + Z(16), Z(130), Z(40), B_PLAY, "Play", 1);
    bx -= Z(100) + Z(10); button(bx, fy + Z(16), Z(100), Z(40), B_CLOSE, "Close", 0);
    bx -= Z(150) + Z(10); button(bx, fy + Z(16), Z(150), Z(40), B_ADD, "Add release…", 0);
}

void paint(HDC dc, int cw, int ch) {
    draw_begin(dc);
    U.nhits = 0;
    fill(0, 0, cw, ch, T->bg);

    header(cw);

    int lx = Z(MARGIN), rx = cw - Z(MARGIN) - Z(COL_W), top = Z(112);
    int y = top;
    y += display_card(lx, y) + Z(CARD_GAP);
    y += sound_card(lx, y) + Z(CARD_GAP);
    if (g_wine) wine_card(lx, y);

    y = top;
    y += controls_card(rx, y) + Z(CARD_GAP);
    appearance_card(rx, y);

    footer(cw, ch);
    dropdown_list(ch);
    draw_end();
}
