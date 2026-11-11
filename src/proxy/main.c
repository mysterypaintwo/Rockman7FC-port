/* ---------------------------------------------------------------------------
 * pxtone proxy for Rockman 7 FC: applies port.ini inside the game process
 *
 * Installed as pxtone.dll next to the game executable (rm7fc.exe or
 * mm7fc.exe). The original DLL is renamed pxtone_real.dll and every export is
 * forwarded to it (exports.c). The game imports pxtone.dll, so this runs
 * inside the game before the game's own code starts, on Windows and under
 * Wine alike. Settings come from port.ini next to the game or one folder up;
 * if there is none, the proxy changes nothing.
 *
 * At start-up it applies port.ini:
 *   - config.ini      window scale, BGM/SE switches, engine-default keys
 *   - soundtrack      music, effects and pxtone from either release (audio.c)
 *   - SE volume       rescales se\*.wav from pristine port\se_original\
 *   - display         sizes above 3x and fullscreen (window.c)
 *   - key remapping   any key can press any action (input.c)
 *   - gamepad         pad buttons and sticks, fed through the same hooks
 * and while the game runs it keeps the BGM volume in sync with port.ini.
 *
 * Everything is logged to port.log next to the game.
 * ------------------------------------------------------------------------- */

#include "proxy.h"

char g_dir[MAX_PATH];
char g_ini[MAX_PATH];
char g_snd[MAX_PATH];
volatile LONG g_stop;
int g_trace;

#define LOG_MAX (256 * 1024)       /* port.log is started over beyond this */

/* ---- logging ------------------------------------------------------------- */

void plog(const char *fmt, ...) {
    char path[MAX_PATH], line[1025];       /* wvsprintf writes at most 1024 chars */
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    port_join(path, g_dir, "port.log");
    port_append_line(path, line);
}

static void trim_log(void) {
    char path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    port_join(path, g_dir, "port.log");
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa) &&
        (fa.nFileSizeHigh || fa.nFileSizeLow > LOG_MAX)) DeleteFileA(path);
}

/* ---- import patching ----------------------------------------------------- */

/* Matching slots by address works although Delphi executables keep no
 * import name table at run time. */
int patch_iat_in(HMODULE mod, void *real, void *hook) {
    if (!real || !mod) return 0;
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int count = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress);
         d->Name; d++) {
        for (DWORD *slot = (DWORD *)(base + d->FirstThunk); *slot; slot++) {
            if (*slot != (DWORD)(ULONG_PTR)real) continue;
            DWORD old;
            if (VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) {
                InterlockedExchange((LONG *)slot, (LONG)(ULONG_PTR)hook);
                VirtualProtect(slot, sizeof *slot, old, &old);
                count++;
            }
        }
    }
    return count;
}

int patch_iat(void *real, void *hook) { return patch_iat_in(GetModuleHandleA(NULL), real, hook); }

/* ---- config.ini ---------------------------------------------------------- */

/* Writes the file only when its content differs (keeps mtimes stable).
 * Returns 1 unchanged, 2 written, 0 failed. */
static int write_if_changed(const char *path, const char *text) {
    DWORD n = (DWORD)lstrlenA(text), sz = 0;
    BYTE *old = port_read_file(path, &sz);
    int same = old && sz == n && memcmp(old, text, n) == 0;
    port_free(old);
    if (same) return 1;
    return port_write_file(path, text, n) ? 2 : 0;
}

static void apply_config_ini(void) {
    char text[200], path[MAX_PATH];
    /* Sizes above 3x and fullscreen are drawn by the proxy from a 1x frame. */
    int want = ini_int("Display", "Scale", 3, 1, 16);
    int full = ini_int("Display", "Fullscreen", 0, 0, 2);
    int scale = (want > 3 || full) ? 1 : want;
    int bgm = ini_int("Sound", "Music", 1, 0, 1);
    int se = ini_int("Sound", "Effects", 1, 0, 1);
    /* Keys stay at the engine defaults: remapping happens in the proxy, so
     * the game always listens for X (attack) and Z (jump). */
    wsprintfA(text, "[Key]\r\nAttack=88\r\nJump=90\r\n[Window]\r\nSize=%d\r\n"
                    "[Sound]\r\nBGM=%d\r\nSE=%d\r\n", scale, bgm, se);
    port_join(path, g_dir, "config.ini");
    int r = write_if_changed(path, text);
    if (r == 2) plog("config.ini: scale %dx, BGM %d, SE %d", scale, bgm, se);
    else if (!r) plog("config.ini: could not write %s", path);
}

/* ---- entry --------------------------------------------------------------- */

/* port.ini sits next to the game, or one level up when both releases are
 * installed side by side (<root>\ja, <root>\en). */
static void find_ini(void) {
    char parent[MAX_PATH];
    port_join(g_ini, g_dir, PORT_INI);
    if (!port_exists(g_ini) && port_parent_dir(g_dir, parent)) port_join(g_ini, parent, PORT_INI);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        port_module_dir(inst, g_dir);
        find_ini();
        trim_log();
        plog("--- Rockman 7 FC port: proxy attached (settings: %s)", g_ini);

        int have_ini = port_exists(g_ini);
        lstrcpyA(g_snd, g_dir);
        if (have_ini) pick_soundtrack();
        load_pxtone();
        if (have_ini) {
            /* The executable's imports are resolved before the DLLs it
             * depends on are initialised, so the hooks go in before any game
             * code runs, and the files are ready before the game reads them. */
            g_trace = ini_int("Debug", "Trace", 0, 0, 1);
            apply_config_ini();
            apply_se_volume();
            install_file_hooks();
            install_display_hooks();
            input_init();
        } else {
            plog("no port.ini: running the game unmodified");
        }
        start_volume_thread();
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);
    }
    return TRUE;
}
