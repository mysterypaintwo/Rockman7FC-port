/* ---------------------------------------------------------------------------
 * Releases: finding them, adding one, installing the proxy, launching
 * ------------------------------------------------------------------------- */
#include "settings.h"

static release g_rel[2];        /* [0] Japanese, [1] English */

/* "ver.final2" in the first lines of the release's readme. */
static void read_version(const char *dir, char *ver) {
    char path[MAX_PATH];
    DWORD n = 0;
    ver[0] = 0;
    port_join(path, dir, "readme.txt");
    char *buf = (char *)port_read_file(path, &n);
    if (!buf) return;
    if (n > 511) buf[511] = 0;
    for (char *q = buf; *q; q++) {
        if ((q[0] == 'v' || q[0] == 'V') && q[1] == 'e' && q[2] == 'r' && q[3] == '.') {
            int k = 0;
            while (q[k] && q[k] != '\r' && q[k] != '\n' && q[k] != ' ' && k < 30) { ver[k] = q[k]; k++; }
            ver[k] = 0;
            break;
        }
    }
    port_free(buf);
}

static int exe_year(const char *dir) {
    char path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    SYSTEMTIME st;
    port_join(path, dir, port_game_exe(dir));
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa) && FileTimeToSystemTime(&fa.ftLastWriteTime, &st))
        return st.wYear;
    return 0;
}

static void find_release(int en, release *r) {
    const char *exe = en ? "mm7fc.exe" : "rm7fc.exe";
    char path[MAX_PATH];
    ZeroMemory(r, sizeof *r);
    wsprintfA(r->dir, "%s%s\\", g_root, en ? "en" : "ja");
    port_join(path, r->dir, exe);
    if (!port_exists(path)) {
        port_join(path, g_root, exe);            /* this exe inside a game folder */
        if (!port_exists(path)) { r->dir[0] = 0; return; }
        lstrcpyA(r->dir, g_root);
    }
    r->installed = 1;
    read_version(r->dir, r->ver);
    r->year = exe_year(r->dir);
}

void releases_refresh(void) {
    find_release(0, &g_rel[0]);
    find_release(1, &g_rel[1]);
}

const release *release_for(int en) { return &g_rel[en ? 1 : 0]; }

int any_release(void) { return g_rel[0].installed || g_rel[1].installed; }

/* ---- the proxy ----------------------------------------------------------- */

static int files_equal(const char *a, const char *b) {
    DWORD na = 0, nb = 0;
    BYTE *x = port_read_file(a, &na), *y = port_read_file(b, &nb);
    int same = x && y && na == nb && memcmp(x, y, na) == 0;
    port_free(x);
    port_free(y);
    return same;
}

/* A pxtone.dll is one of our proxies if it names pxtone_real. */
static int is_proxy(const char *path) {
    DWORD n = 0;
    BYTE *buf = port_read_file(path, &n);
    int found = 0;
    for (DWORD i = 0; buf && !found && i + 11 <= n; i++)
        if (buf[i] == 'p' && !memcmp(buf + i, "pxtone_real", 11)) found = 1;
    port_free(buf);
    return found;
}

/* Installs/updates the proxy in a game folder: the game's own pxtone.dll
 * becomes pxtone_real.dll and the proxy takes its name. */
static const char *ensure_proxy(const char *gamedir) {
    char proxy[MAX_PATH], cur[MAX_PATH], real[MAX_PATH];
    port_join(proxy, g_root, "pxtone-proxy.dll");
    port_join(cur, gamedir, "pxtone.dll");
    port_join(real, gamedir, "pxtone_real.dll");
    if (!port_exists(real)) {
        if (!port_exists(cur)) return "pxtone.dll is missing from the game folder.";
        if (is_proxy(cur)) return "The game's original pxtone.dll is missing (only the port's proxy is there). Re-add the release.";
        if (!MoveFileA(cur, real)) return "Could not rename pxtone.dll to pxtone_real.dll.";
    }
    if (!port_exists(proxy)) return port_exists(cur) && is_proxy(cur) ? NULL : "pxtone-proxy.dll is missing next to Rockman7FC.exe.";
    if (!files_equal(proxy, cur) && !CopyFileA(proxy, cur, FALSE)) return "Could not install the audio/controls proxy (pxtone.dll).";
    return NULL;
}

/* ---- adding a release ----------------------------------------------------- */

/* Files that are not part of a release (or are the port's own per-folder
 * files) and are not copied. */
static int skip_name(const WCHAR *n) {
    return !lstrcmpW(n, L".") || !lstrcmpW(n, L"..") || !lstrcmpiW(n, L".DS_Store") ||
           !lstrcmpiW(n, L"Thumbs.db") || !lstrcmpiW(n, L"port.log") || !lstrcmpiW(n, L"port.ini") ||
           (lstrlenW(n) >= 9 && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, n, 9, L"backtrace", 9) == CSTR_EQUAL);
}

/* Copies a folder tree (both paths end in '\'). Uses the wide-character API:
 * some file names in the game (pc\*.txt) are Japanese and cannot be
 * expressed in the ANSI code page. */
static int copy_tree_w(const WCHAR *src, const WCHAR *dst) {
    WCHAR pat[MAX_PATH];
    CreateDirectoryW(dst, NULL);
    if (lstrlenW(src) + 2 >= MAX_PATH) return 0;
    lstrcpyW(pat, src);
    lstrcatW(pat, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    int ok = 1;
    do {
        if (skip_name(fd.cFileName)) continue;
        WCHAR s[MAX_PATH], d[MAX_PATH];
        if (lstrlenW(src) + lstrlenW(fd.cFileName) + 2 >= MAX_PATH ||
            lstrlenW(dst) + lstrlenW(fd.cFileName) + 2 >= MAX_PATH) { ok = 0; continue; }
        lstrcpyW(s, src); lstrcatW(s, fd.cFileName);
        lstrcpyW(d, dst); lstrcatW(d, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            lstrcatW(s, L"\\");
            lstrcatW(d, L"\\");
            ok &= copy_tree_w(s, d);
        } else {
            SetFileAttributesW(d, FILE_ATTRIBUTE_NORMAL);
            if (!CopyFileW(s, d, FALSE)) {
                char m[MAX_PATH * 3 + 40], name[MAX_PATH * 3];
                WideCharToMultiByte(CP_UTF8, 0, s, -1, name, sizeof name, NULL, NULL);
                wsprintfA(m, "copy failed (error %lu): %s", GetLastError(), name);
                log_root(m);
                ok = 0;
            }
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return ok;
}

static int copy_tree(const char *src, const char *dst) {
    WCHAR ws[MAX_PATH], wd[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, src, -1, ws, MAX_PATH) ||
        !MultiByteToWideChar(CP_ACP, 0, dst, -1, wd, MAX_PATH)) return 0;
    return copy_tree_w(ws, wd);
}

static const char *copy_release(const char *srcdir, char *which) {
    static char err[MAX_PATH + 100];
    char src[MAX_PATH], exe[MAX_PATH], dst[MAX_PATH];
    lstrcpynA(src, srcdir, MAX_PATH - 2);
    int n = lstrlenA(src);
    if (n && src[n - 1] != '\\' && src[n - 1] != '/') lstrcatA(src, "\\");
    const char *lang = NULL;
    port_join(exe, src, "rm7fc.exe");
    if (port_exists(exe)) lang = "ja";
    port_join(exe, src, "mm7fc.exe");
    if (port_exists(exe)) lang = "en";
    if (!lang) { wsprintfA(err, "No rm7fc.exe or mm7fc.exe in:\n%s", src); return err; }
    lstrcpyA(which, lang);
    wsprintfA(dst, "%s%s\\", g_root, lang);
    if (!lstrcmpiA(src, dst)) return ensure_proxy(dst);     /* already in place */
    if (!copy_tree(src, dst)) return "Copying the game files failed.";
    /* a copied proxy without its original would break the game */
    char real[MAX_PATH], cur[MAX_PATH];
    port_join(real, dst, "pxtone_real.dll");
    port_join(cur, dst, "pxtone.dll");
    if (port_exists(real) && !is_proxy(cur)) DeleteFileA(real);
    return ensure_proxy(dst);
}

const char *import_release(const char *srcdir, char *which) {
    const char *e = copy_release(srcdir, which);
    releases_refresh();
    return e;
}

/* ---- launching ------------------------------------------------------------ */

const char *launch_game(int en) {
    static char err[MAX_PATH + 80];
    char exe[MAX_PATH];
    const release *r = release_for(en)->installed ? release_for(en) : release_for(!en);
    if (!r->installed) return "No game files found. Use \"Add release\" to add the game folder.";
    const char *e = ensure_proxy(r->dir);
    if (e) return e;
    port_join(exe, r->dir, port_game_exe(r->dir));
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(exe, NULL, NULL, NULL, FALSE, 0, NULL, r->dir, &si, &pi)) {
        wsprintfA(err, "Could not start %s (error %lu).", exe, GetLastError());
        return err;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return NULL;
}
