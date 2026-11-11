/* ---------------------------------------------------------------------------
 * Behaviour: clicks, keys, the gamepad listener and the window procedure
 *
 * Every change is saved to port.ini right away (changed()).
 * ------------------------------------------------------------------------- */
#include "settings.h"
#include <shlobj.h>

static void redraw(void) { InvalidateRect(g_wnd, NULL, FALSE); }

static void changed(void) {
    save_settings();
    redraw();
}

static void fit_window(void) {
    RECT r = { 0, 0, Z(DESIGN_W), Z(DESIGN_H) };
    AdjustWindowRect(&r, (DWORD)GetWindowLongA(g_wnd, GWL_STYLE), FALSE);
    SetWindowPos(g_wnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

static int *volume_of(int id) { return id == O_MUSIC ? &S.music_vol : &S.fx_vol; }

static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

static void set_slider(int id, int mx) {
    for (int i = 0; i < U.nhits; i++) {
        const hit *h = &U.hits[i];
        if (h->kind != H_SLIDER || h->a != id) continue;
        int x0 = h->r.left + Z(8), w = h->r.right - h->r.left - Z(16);   /* see slider() */
        int v = clamp100(MulDiv(mx - x0, 100, w));
        int *dst = volume_of(id);
        if (*dst != v) {
            *dst = v;
            if (id == O_MUSIC) save_settings();   /* live while the game runs */
            redraw();
        }
        return;
    }
}

/* ---- binding keys and pad buttons ----------------------------------------- */

static void stop_listen(void) {
    if (U.listen_a >= 0) KillTimer(g_wnd, TIMER_LISTEN_PAD);
    U.listen_a = -1;
}

/* Puts value (a VK, or pad input + 1) into the slot being listened to. The
 * same value is taken off any other slot, so each key/button does one thing. */
static void assign(int value, const char *name) {
    int a = U.listen_a, s = U.listen_s;
    int (*table)[PORT_KEYS_PER_ACTION] = U.tab ? S.pad : S.keys;
    char msg[200] = "";
    stop_listen();
    for (int b = 0; b < PORT_ACTION_COUNT; b++)
        for (int t = 0; t < PORT_KEYS_PER_ACTION; t++)
            if ((b != a || t != s) && table[b][t] == value) {
                table[b][t] = 0;
                wsprintfA(msg, "%s moved from %s to %s.", name, PORT_ACTIONS[b].label, PORT_ACTIONS[a].label);
            }
    table[a][s] = value;
    for (int b = 0; b < PORT_ACTION_COUNT; b++) compact_slots(table[b]);
    if (!msg[0]) wsprintfA(msg, "%s → %s", PORT_ACTIONS[a].label, name);
    if (!U.tab) {                     /* every action should have a key */
        for (int b = 0; b < PORT_ACTION_COUNT; b++)
            if (!table[b][0]) { wsprintfA(msg + lstrlenA(msg), "  %s has no key now.", PORT_ACTIONS[b].label); break; }
    }
    status(msg, 0);
    changed();
}

static void assign_key(int vk) {
    char name[32];
    port_key_name(vk, name);
    assign(vk, name);
}

static void clear_slot(void) {
    int (*table)[PORT_KEYS_PER_ACTION] = U.tab ? S.pad : S.keys;
    table[U.listen_a][U.listen_s] = 0;
    compact_slots(table[U.listen_a]);
    stop_listen();
    changed();
}

static void start_listen(int a, int s) {
    stop_listen();
    U.listen_a = a;
    U.listen_s = s;
    if (U.tab) {
        U.pad_base = pad_read_dev(NULL, S.pad_dev);
        SetTimer(g_wnd, TIMER_LISTEN_PAD, 15, NULL);
        status(U.pad_src > 0 ? "Press a button or move a stick.  Esc cancels, Delete clears."
                             : "No controller found. Connect one, then press a button.", U.pad_src <= 0);
    }
}

/* A key pressed while a keyboard slot is listening. */
static void key_for_slot(int vk) {
    if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS || vk == VK_SNAPSHOT) status("That key can't be used.", 1);
    else if (vk == VK_LSHIFT || vk == VK_RSHIFT) assign_key(VK_SHIFT);
    else if (vk == VK_LCONTROL || vk == VK_RCONTROL) assign_key(VK_CONTROL);
    else if (vk == VK_LMENU || vk == VK_RMENU) assign_key(VK_MENU);
    else assign_key(vk);
}

/* Polled while a pad slot is listening: the first newly pressed input wins. */
static void poll_pad_for_slot(void) {
    pad_bits b = pad_read_dev(NULL, S.pad_dev);
    pad_bits fresh = b & ~U.pad_base;
    U.pad_base &= b;                  /* released buttons can be pressed again */
    if (!fresh) return;
    int i = 0;
    while (!(fresh & PAD_BIT(i))) i++;
    assign(i + 1, PAD_NAMES[i][1]);
}

/* ---- inline volume editor (double-click a percentage) --------------------- */

static HWND g_edit;
static int g_edit_id;
static WNDPROC g_edit_proc;
static HBRUSH g_edit_brush;

static void close_edit(int apply) {
    if (!g_edit) return;
    HWND e = g_edit;
    g_edit = NULL;
    if (apply) {
        char b[16];
        GetWindowTextA(e, b, sizeof b);
        int v = 0, any = 0;
        for (char *q = b; *q; q++) if (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); any = 1; if (v > 1000) break; }
        if (any) {
            *volume_of(g_edit_id) = v > 100 ? 100 : v;
            char m[80];
            wsprintfA(m, "%s volume set to %d%%.", g_edit_id == O_MUSIC ? "BGM" : "SFX", *volume_of(g_edit_id));
            status(m, 0);
            save_settings();
        }
    }
    DestroyWindow(e);
    SetFocus(g_wnd);
    redraw();
}

static LRESULT CALLBACK edit_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB)) {
        close_edit(wp != VK_ESCAPE);
        return 0;
    }
    if (m == WM_CHAR && (wp == '\r' || wp == 27 || wp == '\t')) return 0;   /* no beep */
    if (m == WM_KILLFOCUS) {
        LRESULT r = CallWindowProcA(g_edit_proc, w, m, wp, lp);
        if (g_edit == w) PostMessageA(g_wnd, WM_APP, 0, 0);    /* commit after focus settles */
        return r;
    }
    return CallWindowProcA(g_edit_proc, w, m, wp, lp);
}

static void open_edit(int id, RECT r) {
    close_edit(1);
    char b[8];
    wsprintfA(b, "%d", *volume_of(id));
    g_edit_id = id;
    g_edit = CreateWindowExA(0, "EDIT", b, WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
                             r.left, r.top + Z(2), r.right - r.left, r.bottom - r.top - Z(4), g_wnd, NULL,
                             GetModuleHandleA(NULL), NULL);
    if (!g_edit) return;
    SendMessageA(g_edit, WM_SETFONT, (WPARAM)f_bold, TRUE);
    SendMessageA(g_edit, EM_LIMITTEXT, 3, 0);
    g_edit_proc = (WNDPROC)SetWindowLongPtrA(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    SendMessageA(g_edit, EM_SETSEL, 0, -1);
    SetFocus(g_edit);
    status("Type a value from 0 to 100, then press Return (Esc cancels).", 0);
}

/* ---- actions ---------------------------------------------------------------- */

static void pick_item(int index) {
    int id = U.drop;
    U.drop = 0;
    if (index < 0 || index >= U.nitems) { redraw(); return; }
    const drop_item *it = &U.items[index];
    if (id == D_SIZE) {
        if (it->value == 0) {
            S.full = 1;
            status("Fullscreen, integer scaled (exact pixels). Cmd+Q / Alt+F4 quits. Applies on the next start.", 0);
        } else if (it->value == -1) {
            S.full = 2;
            status("Fullscreen, stretched to fill the screen at the original 8:7 shape. Applies on the next start.", 0);
        } else {
            S.full = 0;
            S.scale = it->value;
            status(S.scale > 3 ? "Sizes above 3× are scaled by the port with crisp pixels. Applies on the next start."
                               : "Window size applies the next time the game starts.", 0);
        }
    } else if (id == D_DEVICE) {
        static const char gone[] = " (not connected)";
        S.pad_dev = it->value;
        if (S.pad_dev == PAD_ANY) {
            S.pad_dev_name[0] = 0;
        } else {
            lstrcpynA(S.pad_dev_name, it->label, sizeof S.pad_dev_name);
            int n = lstrlenA(S.pad_dev_name), g = lstrlenA(gone);
            if (n >= g && !lstrcmpA(S.pad_dev_name + n - g, gone)) S.pad_dev_name[n - g] = 0;
        }
        char m[160];
        wsprintfA(m, "The game listens to: %s", S.pad_dev == PAD_ANY ? "any controller" : S.pad_dev_name);
        status(m, 0);
    }
    changed();
}

static void add_release(void) {
    BROWSEINFOA bi = { 0 };
    char path[MAX_PATH];
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = "Choose a Rockman 7 FC folder (contains rm7fc.exe or mm7fc.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (!id) return;
    int ok = SHGetPathFromIDListA(id, path);
    CoTaskMemFree(id);
    if (!ok) return;
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    char which[8] = "";
    const char *e = import_release(path, which);
    if (e) { status(e, 1); return; }
    S.lang_en = port_ieq(which, "en");
    if (!sheet_loaded()) load_sheet();
    status(S.lang_en ? "English release added and selected." : "Japanese release added and selected.", 0);
    changed();
}

static void play(void) {
    save_settings();
    if (!any_release()) { status("No game files found. Use \"Add release…\" first.", 1); return; }
    if (g_wine) { PostQuitMessage(EXIT_PLAY); return; }
    const char *e = launch_game(S.lang_en);
    if (e) status(e, 1); else DestroyWindow(g_wnd);
}

static void click_segment(int group, int i) {
    switch (group) {
    case G_LANG:
        S.lang_en = i;
        status(i ? "English text selected." : "Japanese text selected.", 0);
        break;
    case G_TRACK:
        S.track_en = i;
        status(i ? "BGM, SFX and sound engine from the English release (newer)."
                 : "BGM, SFX and sound engine from the Japanese final2 release.", 0);
        break;
    case G_RENDER: S.gdi = i; break;
    case G_THEME:  S.dark = i == 0; break;
    case G_ZOOM:   S.zoom = i; make_fonts(); save_settings(); fit_window(); return;
    case G_TAB:    U.tab = i; stop_listen(); redraw(); return;
    }
    changed();
}

static void click_toggle(int id) {
    switch (id) {
    case O_MUSIC:     S.music = !S.music; break;
    case O_FX:        S.fx = !S.fx; status("SFX changes apply the next time the game starts.", 0); break;
    case O_VDESK:     S.vdesk = !S.vdesk; break;
    case O_GAMESCOPE: S.gamescope = !S.gamescope; break;
    case O_PAD:       S.pad_on = !S.pad_on; status(S.pad_on ? "Gamepad on." : "Gamepad off.", 0); break;
    }
    changed();
}

static void click_button(int id) {
    switch (id) {
    case B_DEFAULTS:
        if (!U.confirm_reset) {
            U.confirm_reset = 1;
            status("Click \"Click again to confirm\" to reset window, language, sound, keys and gamepad.", 0);
        } else {
            U.confirm_reset = 0;
            restore_defaults();
            status("All game settings restored to defaults (appearance kept).", 0);
            changed();
        }
        redraw();
        break;
    case B_CLOSE: DestroyWindow(g_wnd); break;
    case B_ADD:   add_release(); break;
    case B_PLAY:  play(); break;
    }
}

static void click(int i) {
    const hit *h = &U.hits[i];
    if (g_edit) close_edit(1);
    if (h->kind == H_DROPITEM) { if (h->b >= 0) pick_item(h->b); return; }
    if (h->kind == H_DROP) {
        if (U.drop == h->a) U.drop = 0;
        else open_dropdown(h->a);
        stop_listen();
        redraw();
        return;
    }
    if (U.drop) { U.drop = 0; redraw(); }        /* any other click closes a list */
    if (h->kind == H_PCT) return;                /* double-click edits */
    if (!h->enabled) {
        if (h->kind == H_SEG && (h->a == G_LANG || h->a == G_TRACK))
            status("That release isn't installed. Use \"Add release…\" to add its folder.", 1);
        return;
    }
    if (U.listen_a >= 0 && h->kind != H_KEY) { stop_listen(); redraw(); }
    if (U.confirm_reset && !(h->kind == H_BUTTON && h->a == B_DEFAULTS)) U.confirm_reset = 0;
    switch (h->kind) {
    case H_SEG:    click_segment(h->a, h->b); break;
    case H_TOGGLE: click_toggle(h->a); break;
    case H_SLIDER:
        U.drag = h->a;
        SetCapture(g_wnd);
        break;
    case H_KEY:
        if (U.listen_a == h->a && U.listen_s == h->b) stop_listen();
        else start_listen(h->a, h->b);
        SetFocus(g_wnd);
        redraw();
        break;
    case H_RESETKEYS:
        if (U.tab) { default_pad(); status("Gamepad buttons reset to defaults.", 0); }
        else { default_keys(); status("Keys reset to the game's defaults.", 0); }
        changed();
        break;
    case H_SWATCH: {
        char m[64];
        S.accent = h->a;
        wsprintfA(m, "Accent: %s", ACCENT_NAMES[h->a]);
        status(m, 0);
        changed();
        break;
    }
    case H_BUTTON: click_button(h->a); break;
    }
}

/* ---- window procedure ------------------------------------------------------ */

static void on_paint(HWND w) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    RECT rc;
    GetClientRect(w, &rc);
    /* draw off-screen, then copy, so nothing flickers */
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    paint(mem, rc.right, rc.bottom);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(w, &ps);
}

static void on_key(HWND w, int vk) {
    if (U.listen_a >= 0) {
        if (vk == VK_ESCAPE) { stop_listen(); status("", 0); }
        else if (vk == VK_DELETE) clear_slot();
        else if (!U.tab) key_for_slot(vk);       /* pad slots only take pad input */
        redraw();
        return;
    }
    if (vk == VK_ESCAPE && U.drop) { U.drop = 0; redraw(); return; }
    if (vk == VK_ESCAPE) DestroyWindow(w);
}

LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
    switch (m) {
    case WM_PAINT:
        on_paint(w);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATEAPP:
        if (wp) {                                /* releases may have been added meanwhile */
            releases_refresh();
            if (!sheet_loaded()) load_sheet();
            redraw();
        }
        break;
    case WM_MOUSEMOVE: {
        if (U.drag >= 0) { set_slider(U.drag, mx); return 0; }
        int h = hit_at(mx, my);
        if (h != U.hover) {
            U.hover = h;
            redraw();
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, w, 0 };
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (U.hover != -1) { U.hover = -1; redraw(); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            int on = U.hover >= 0 && U.hits[U.hover].enabled;
            SetCursor(LoadCursor(NULL, on ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDBLCLK: {
        int i = hit_at(mx, my);
        if (i >= 0 && U.hits[i].kind == H_PCT) { open_edit(U.hits[i].a, U.hits[i].r); return 0; }
    }
    /* fall through - anywhere else a double-click is a normal click */
    case WM_LBUTTONDOWN: {
        int i = hit_at(mx, my);
        if (i >= 0) {
            click(i);
            if (U.drag >= 0) set_slider(U.drag, mx);
        } else {                                 /* click on empty space */
            if (U.listen_a >= 0) stop_listen();
            if (g_edit) close_edit(1);
            U.drop = 0;
            U.confirm_reset = 0;
            redraw();
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (U.drag >= 0) {
            int id = U.drag;
            U.drag = -1;
            ReleaseCapture();
            save_settings();
            status(id == O_FX ? "SFX volume applies the next time the game starts."
                              : "BGM volume applies right away, even while playing.", 0);
            redraw();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT p = { mx, my };
        ScreenToClient(w, &p);
        int i = hit_at(p.x, p.y);
        if (i >= 0 && U.hits[i].kind == H_SLIDER) {
            int *v = volume_of(U.hits[i].a);
            *v = clamp100(*v + ((short)HIWORD(wp) > 0 ? 5 : -5));
            changed();
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (U.listen_a >= 0 || wp == VK_ESCAPE) { on_key(w, (int)wp); return 0; }
        break;
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
        /* keep Alt/F10 from opening the (absent) menu while binding keys */
        if (U.listen_a >= 0 || wp == VK_MENU || wp == VK_F10) return 0;
        break;
    case WM_KILLFOCUS:
        if (U.listen_a >= 0 && !U.tab) { stop_listen(); redraw(); }
        break;
    case WM_TIMER:
        if (wp == TIMER_LISTEN_PAD && U.listen_a >= 0 && U.tab) {
            poll_pad_for_slot();
        } else if (wp == TIMER_PAD_CHECK) {
            int src = U.pad_src;
            if (U.tab || U.pad_src < 0) pad_read_dev(&src, S.pad_dev);
            if (src != U.pad_src) { U.pad_src = src; redraw(); }
        }
        return 0;
    case WM_APP:                                 /* the volume editor lost focus */
        if (g_edit && GetFocus() != g_edit) close_edit(1);
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, T->text);
        SetBkColor(dc, T->chip_hi);
        if (g_edit_brush) DeleteObject(g_edit_brush);
        g_edit_brush = CreateSolidBrush(T->chip_hi);
        return (LRESULT)g_edit_brush;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}
