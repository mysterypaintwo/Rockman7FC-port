/* ---------------------------------------------------------------------------
 * Rockman7FC.exe: settings window and launcher for the Rockman 7 FC port
 *
 * One custom-drawn window that edits port.ini: window size, language, music
 * and effects volume, every game key and pad button, Wine options and the
 * window's own look. Changes are saved immediately; music volume even
 * applies to a running game.
 *
 *   Rockman7FC.exe                 open the settings window
 *   Rockman7FC.exe --gamepad       ... on the Gamepad tab
 *   Rockman7FC.exe --play          start the game in the configured language
 *   Rockman7FC.exe --import <dir>  add a release (folder with rm7fc.exe or
 *                                  mm7fc.exe) as ja\ or en\ next to this exe
 *
 * Folder layout (all platforms):
 *   <root>\Rockman7FC.exe  pxtone-proxy.dll  port.ini
 *   <root>\ja\  Japanese release (rm7fc.exe)    <root>\en\  English (mm7fc.exe)
 * A single release with this exe dropped straight into its folder works too.
 *
 * Runs natively on Windows and under Wine on macOS/Linux. Under Wine the
 * Play button exits with code 10 (EXIT_PLAY) and the shell launcher starts
 * the game, so that Wine-only options (desktop window, gamescope) apply.
 * ------------------------------------------------------------------------- */

#include "settings.h"
#include <shellapi.h>
#include <objbase.h>

char g_root[MAX_PATH];
char g_ini[MAX_PATH];
int  g_wine;
int  g_linux;
HWND g_wnd;

#define DWM_DARK_TITLE_BAR 20    /* DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+) */

void log_root(const char *msg) {
    char path[MAX_PATH];
    port_join(path, g_root, "port.log");
    port_append_line(path, msg);
}

static void fail_box(const char *msg) {
    log_root(msg);
    MessageBoxA(NULL, msg, "Rockman 7 FC", MB_OK | MB_ICONERROR);
}

static void detect_wine(void) {
    if (!port_is_wine()) return;
    g_wine = 1;
    const char *host = port_wine_host();
    if (host && port_ieq(host, "Linux")) g_linux = 1;
}

/* The game's icon, unless this exe carries its own (it normally does). */
static void load_icons(HINSTANCE inst) {
    char exe[MAX_PATH];
    const release *r = release_for(0)->installed ? release_for(0) : release_for(1);
    if (r->installed) {
        port_join(exe, r->dir, port_game_exe(r->dir));
        ExtractIconExA(exe, 0, &g_icon_big, &g_icon_small, 1);
    }
    HICON own = LoadIconA(inst, MAKEINTRESOURCEA(1));
    if (own) g_icon_big = g_icon_small = own;
}

static void dark_title_bar(HWND w) {
    if (g_wine) return;
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT (WINAPI *PFN_DWMSET)(HWND, DWORD, LPCVOID, DWORD);
    PFN_DWMSET set = (PFN_DWMSET)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    BOOL on = S.dark;
    if (set) set(w, DWM_DARK_TITLE_BAR, &on, sizeof on);
}

static int run_window(HINSTANCE inst, int show) {
    load_settings();
    if (!port_exists(g_ini)) save_settings();
    load_sheet();
    measure_monitors();
    draw_startup();
    CoInitialize(NULL);              /* for the folder picker */
    load_icons(inst);

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.style = CS_DBLCLKS;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = g_icon_big;
    wc.lpszClassName = "Rockman7FCSettings";
    RegisterClassA(&wc);

    make_fonts();
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, Z(DESIGN_W), Z(DESIGN_H) };
    AdjustWindowRect(&r, style, FALSE);
    g_wnd = CreateWindowA(wc.lpszClassName, "Rockman 7 FC Settings", style, CW_USEDEFAULT, CW_USEDEFAULT,
                          r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!g_wnd) return 1;
    if (g_icon_small) SendMessageA(g_wnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon_small);
    dark_title_bar(g_wnd);

    if (!any_release())
        status("No game files yet. Click \"Add release…\" and choose the game folder.", 1);
    else if (!release_for(S.lang_en)->installed)
        status("The selected language's release isn't installed; using the other one.", 0);

    SetTimer(g_wnd, TIMER_PAD_CHECK, 700, NULL);
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    int code = msg.message == WM_QUIT ? (int)msg.wParam : 0;
    if (code == EXIT_PLAY && IsWindow(g_wnd)) DestroyWindow(g_wnd);
    return code;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;
    (void)cmd;
    port_module_dir(NULL, g_root);
    port_join(g_ini, g_root, PORT_INI);
    detect_wine();
    releases_refresh();

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2 && !lstrcmpiW(argv[1], L"--gamepad")) U.tab = 1;
    if (argc >= 2 && !lstrcmpiW(argv[1], L"--play")) {
        load_settings();
        save_settings();
        const char *e = launch_game(S.lang_en);
        if (e) { fail_box(e); return 1; }
        return 0;
    }
    if (argc >= 3 && !lstrcmpiW(argv[1], L"--import")) {
        char dir[MAX_PATH], which[8];
        WideCharToMultiByte(CP_ACP, 0, argv[2], -1, dir, MAX_PATH, NULL, NULL);
        const char *e = import_release(dir, which);
        if (e) {
            if (argc < 4) fail_box(e); else log_root(e);    /* a 3rd argument: no message box */
            return 1;
        }
        if (!port_exists(g_ini)) {
            load_settings();
            S.lang_en = port_ieq(which, "en");
            save_settings();
        }
        return 0;
    }
    return run_window(inst, show);
}
