/* ---------------------------------------------------------------------------
 * Shared helpers for the proxy and the settings window; see port_common.h.
 * ------------------------------------------------------------------------- */
#include "port_common.h"

/* ---- actions ------------------------------------------------------------- */

const port_action PORT_ACTIONS[] = {
    { "Up",         "Up",               VK_UP,    "Up"    },
    { "Down",       "Down",             VK_DOWN,  "Down"  },
    { "Left",       "Left",             VK_LEFT,  "Left"  },
    { "Right",      "Right",            VK_RIGHT, "Right" },
    { "Jump",       "Jump",             'Z',      "Z"     },
    { "Shoot",      "Shoot / Confirm",  'X',      "X"     },
    { "WeaponPrev", "Previous weapon",  'A',      "A"     },
    { "WeaponNext", "Next weapon",      'S',      "S"     },
    { "Menu",       "Weapon menu",      'Q',      "Q"     },
    { "Reset",      "Reset",            'R',      "R"     },
};

/* ---- key names ----------------------------------------------------------- */

/* Letters, digits and F1..F24 are named directly; everything else is here. */
typedef struct { int vk; const char *name; } port_key;

static const port_key PORT_KEYS[] = {
    { VK_UP, "Up" }, { VK_DOWN, "Down" }, { VK_LEFT, "Left" }, { VK_RIGHT, "Right" },
    { VK_SPACE, "Space" }, { VK_RETURN, "Return" }, { VK_TAB, "Tab" },
    { VK_BACK, "Backspace" }, { VK_SHIFT, "Shift" }, { VK_CONTROL, "Ctrl" },
    { VK_MENU, "Alt" }, { VK_INSERT, "Insert" }, { VK_DELETE, "Delete" },
    { VK_HOME, "Home" }, { VK_END, "End" }, { VK_PRIOR, "PageUp" }, { VK_NEXT, "PageDown" },
    { VK_NUMPAD0, "Num0" }, { VK_NUMPAD1, "Num1" }, { VK_NUMPAD2, "Num2" },
    { VK_NUMPAD3, "Num3" }, { VK_NUMPAD4, "Num4" }, { VK_NUMPAD5, "Num5" },
    { VK_NUMPAD6, "Num6" }, { VK_NUMPAD7, "Num7" }, { VK_NUMPAD8, "Num8" },
    { VK_NUMPAD9, "Num9" }, { VK_MULTIPLY, "Num*" }, { VK_ADD, "Num+" },
    { VK_SUBTRACT, "Num-" }, { VK_DECIMAL, "Num." }, { VK_DIVIDE, "Num/" },
    { VK_OEM_COMMA, "Comma" }, { VK_OEM_PERIOD, "Period" }, { VK_OEM_2, "Slash" },
    { VK_OEM_1, "Semicolon" }, { VK_OEM_7, "Quote" }, { VK_OEM_4, "LBracket" },
    { VK_OEM_6, "RBracket" }, { VK_OEM_5, "Backslash" }, { VK_OEM_MINUS, "Minus" },
    { VK_OEM_PLUS, "Equals" }, { VK_OEM_3, "Grave" },
};
#define PORT_KEY_COUNT ((int)(sizeof PORT_KEYS / sizeof PORT_KEYS[0]))

void port_key_name(int vk, char *out) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { out[0] = (char)vk; out[1] = 0; return; }
    if (vk >= VK_F1 && vk <= VK_F24) { wsprintfA(out, "F%d", vk - VK_F1 + 1); return; }
    for (int i = 0; i < PORT_KEY_COUNT; i++)
        if (PORT_KEYS[i].vk == vk) { lstrcpyA(out, PORT_KEYS[i].name); return; }
    wsprintfA(out, "VK%d", vk);
}

static int port_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int port_ieq(const char *a, const char *b) {
    while (*a && *b && port_lower((unsigned char)*a) == port_lower((unsigned char)*b)) { a++; b++; }
    return *a == 0 && *b == 0;
}

/* Digits after a prefix ("F12", "VK45"); -1 unless the rest is all digits. */
static int number_after(const char *p) {
    int n = 0;
    if (!*p) return -1;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        n = n * 10 + (*p - '0');
        if (n > 9999) return -1;
    }
    return n;
}

int port_key_parse(const char *s) {
    if (!s[0]) return 0;
    if (!s[1]) {
        int c = port_lower((unsigned char)s[0]);
        if (c >= 'a' && c <= 'z') return c - 32;
        if (c >= '0' && c <= '9') return c;
    }
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9') {
        int n = number_after(s + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if ((s[0] == 'V' || s[0] == 'v') && (s[1] == 'K' || s[1] == 'k')) {
        int n = number_after(s + 2);
        if (n > 0 && n < 256) return n;
    }
    for (int i = 0; i < PORT_KEY_COUNT; i++)
        if (port_ieq(s, PORT_KEYS[i].name)) return PORT_KEYS[i].vk;
    /* other spellings people may type by hand */
    if (port_ieq(s, "Enter")) return VK_RETURN;
    if (port_ieq(s, "Control")) return VK_CONTROL;
    if (port_ieq(s, "Option")) return VK_MENU;
    if (port_ieq(s, "Esc") || port_ieq(s, "Escape")) return VK_ESCAPE;
    return 0;
}

int port_list(const char *s, int (*parse)(const char *), int none, int *out, int max) {
    int n = 0;
    char tok[32];
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        int k = 0;
        while (*s && *s != ',' && k < (int)sizeof tok - 1) tok[k++] = *s++;
        while (k > 0 && (tok[k - 1] == ' ' || tok[k - 1] == '\t')) k--;
        tok[k] = 0;
        if (k) {
            int v = parse(tok);
            if (v != none) out[n++] = v;
        }
    }
    return n;
}

int port_key_list(const char *s, int *out, int max) { return port_list(s, port_key_parse, 0, out, max); }

/* ---- files and paths ----------------------------------------------------- */

void port_module_dir(HMODULE m, char *out) {
    DWORD len = GetModuleFileNameA(m, out, MAX_PATH);
    if (!len || len >= MAX_PATH) { out[0] = 0; return; }
    char *slash = NULL;
    for (char *p = out; *p; p++) if (*p == '\\' || *p == '/') slash = p;
    if (slash) slash[1] = 0; else out[0] = 0;
}

void port_join(char *out, const char *dir, const char *name) {
    lstrcpynA(out, dir, MAX_PATH);
    int n = lstrlenA(out);
    lstrcpynA(out + n, name, MAX_PATH - n);
}

int port_parent_dir(const char *dir, char *out) {
    lstrcpynA(out, dir, MAX_PATH);
    int n = lstrlenA(out);
    if (n < 2) return 0;
    out[n - 1] = 0;                              /* drop the trailing slash */
    char *slash = NULL;
    for (char *q = out; *q; q++) if (*q == '\\' || *q == '/') slash = q;
    if (!slash) return 0;
    slash[1] = 0;
    return 1;
}

int port_exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

int port_is_dir(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

BYTE *port_read_file(const char *path, DWORD *size) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL), got = 0;
    BYTE *buf = NULL;
    if (sz != INVALID_FILE_SIZE && sz < 64u * 1024 * 1024) {
        buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, sz + 1);
        if (buf && (!ReadFile(h, buf, sz, &got, NULL) || got != sz)) {
            HeapFree(GetProcessHeap(), 0, buf);
            buf = NULL;
        }
    }
    CloseHandle(h);
    if (buf) {
        buf[sz] = 0;
        *size = sz;
    }
    return buf;
}

void port_free(void *p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

int port_write_file(const char *path, const void *data, DWORD size) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    BOOL ok = WriteFile(h, data, size, &w, NULL) && w == size;
    CloseHandle(h);
    return ok;
}

/* One WriteFile per line, so lines from different threads don't interleave. */
void port_append_line(const char *path, const char *line) {
    char buf[1100];
    int n = lstrlenA(line);
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    CopyMemory(buf, line, n);
    buf[n++] = '\r';
    buf[n++] = '\n';
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    CloseHandle(h);
}

int port_ini_int(const char *ini, const char *sec, const char *key, int def, int lo, int hi) {
    int v = (int)GetPrivateProfileIntA(sec, key, def, ini);
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- releases ------------------------------------------------------------ */

const char *port_native_language(const char *gamedir) {
    char p[MAX_PATH];
    port_join(p, gamedir, "mm7fc.exe");
    return port_exists(p) ? "en" : "ja";
}

const char *port_game_exe(const char *gamedir) {
    char p[MAX_PATH];
    port_join(p, gamedir, "rm7fc.exe");
    if (port_exists(p)) return "rm7fc.exe";
    port_join(p, gamedir, "mm7fc.exe");
    if (port_exists(p)) return "mm7fc.exe";
    return "rm7fc.exe";
}

/* ---- Wine ---------------------------------------------------------------- */

int port_is_wine(void) {
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    return nt && GetProcAddress(nt, "wine_get_version");
}

const char *port_wine_host(void) {
    typedef void (CDECL *PFN_HOST)(const char **, const char **);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    PFN_HOST host = nt ? (PFN_HOST)(void *)GetProcAddress(nt, "wine_get_host_version") : NULL;
    const char *sys = NULL, *rel = NULL;
    if (host) host(&sys, &rel);
    return sys;
}
