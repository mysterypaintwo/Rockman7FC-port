/* ---------------------------------------------------------------------------
 * Input: key remapping and the gamepad
 *
 * The game reads its keys through GetKeyState / GetAsyncKeyState /
 * GetKeyboardState and keyboard messages. Those imports are redirected:
 *   - a "game key" (the key the engine listens for, PORT_ACTIONS[].game_vk)
 *     reads as down while any physical key bound to it, or a pad input bound
 *     to it, is down;
 *   - a physical key bound to an action is hidden from the game, so it only
 *     does what it was bound to;
 *   - keyboard messages are rewritten the same way, and pad presses are
 *     handed to the game as keyboard messages from a queue.
 * ------------------------------------------------------------------------- */
#include "proxy.h"
#include "port_pad.h"

#define MAX_PHYS 4                           /* physical keys per game key */

static unsigned char g_managed[256];         /* game key under our control */
static unsigned char g_phys_n[256];
static unsigned char g_phys[256][MAX_PHYS];  /* physical keys that press a game key */
static unsigned char g_to_game[256];         /* physical key -> game key (0 = none) */

/* gamepad */
static int g_pad_on;
static int g_pad_dev;                        /* PAD_ANY / PAD_XINPUT(n) / PAD_JOY(n) */
static pad_bits g_pad_mask[256];             /* game key -> pad inputs that press it */
static volatile LONG g_pad_down[256];        /* game key currently held on the pad */
static volatile DWORD g_game_tid;            /* thread that pumps the game's messages */

/* pad thread -> game thread events: vk, | PADQ_DOWN when pressed */
#define PADQ 64
#define PADQ_DOWN 0x100
static volatile LONG g_q_head, g_q_tail;
static WORD g_q[PADQ];

typedef SHORT (WINAPI *PFN_GETKEYSTATE)(int);
typedef BOOL  (WINAPI *PFN_GETKEYBOARDSTATE)(PBYTE);
typedef BOOL  (WINAPI *PFN_PEEKMESSAGEA)(LPMSG, HWND, UINT, UINT, UINT);
typedef BOOL  (WINAPI *PFN_GETMESSAGEA)(LPMSG, HWND, UINT, UINT);

static PFN_GETKEYSTATE      real_GetKeyState;
static PFN_GETKEYSTATE      real_GetAsyncKeyState;
static PFN_GETKEYBOARDSTATE real_GetKeyboardState;
static PFN_PEEKMESSAGEA     real_PeekMessageA;
static PFN_GETMESSAGEA      real_GetMessageA;

/* ---- reading the bindings ------------------------------------------------ */

static void load_keys(void) {
    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        const port_action *act = &PORT_ACTIONS[a];
        char val[128], names[64] = "";
        int keys[PORT_KEYS_PER_ACTION];
        GetPrivateProfileStringA("Keys", act->ini, act->def, val, sizeof val, g_ini);
        int n = port_key_list(val, keys, PORT_KEYS_PER_ACTION);
        int g = act->game_vk;
        g_managed[g] = 1;
        for (int i = 0; i < n; i++) {
            int p = keys[i];
            if (g_to_game[p] || g_phys_n[g] >= MAX_PHYS) continue;   /* first binding wins */
            g_phys[g][g_phys_n[g]++] = (unsigned char)p;
            g_to_game[p] = (unsigned char)g;
        }
        for (int i = 0; i < g_phys_n[g]; i++) {
            char k[16];
            port_key_name(g_phys[g][i], k);
            if (i) lstrcatA(names, ", ");
            lstrcatA(names, k);
        }
        plog("keys: %-12s %s", act->ini, g_phys_n[g] ? names : "(none)");
    }
}

/* Reads the [Pad] section; returns 1 if the gamepad should be active. */
static int load_pad(void) {
    g_pad_on = ini_int("Pad", "Enabled", 1, 0, 1);
    if (!g_pad_on) { plog("pad: disabled in port.ini"); return 0; }
    char dev[32];
    GetPrivateProfileStringA("Pad", "Device", "any", dev, sizeof dev, g_ini);
    g_pad_dev = pad_device_parse(dev);
    plog("pad: listening to %s", dev);
    int any = 0;
    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        char val[128];
        int in[PORT_KEYS_PER_ACTION];
        GetPrivateProfileStringA("Pad", PORT_ACTIONS[a].ini, PAD_DEFAULTS[a], val, sizeof val, g_ini);
        int n = pad_list(val, in, PORT_KEYS_PER_ACTION);
        for (int i = 0; i < n; i++) g_pad_mask[PORT_ACTIONS[a].game_vk] |= PAD_BIT(in[i]);
        if (n) any = 1;
        plog("pad:  %-12s %s", PORT_ACTIONS[a].ini, n ? val : "(none)");
    }
    g_pad_on = any;
    return any;
}

/* ---- key state hooks ----------------------------------------------------- */

static SHORT WINAPI hk_GetKeyState(int vk) {
    SHORT r = real_GetKeyState(vk);
    if (vk > 0 && vk < 256) {
        if (g_managed[vk]) {
            int down = g_pad_down[vk] != 0;
            for (int i = 0; i < g_phys_n[vk]; i++)
                if (real_GetKeyState(g_phys[vk][i]) & 0x8000) down = 1;
            return (SHORT)((r & 1) | (down ? 0x8000 : 0));    /* keep the toggle bit */
        }
        if (g_to_game[vk]) return (SHORT)(r & 1);   /* bound to an action: hide the raw press */
    }
    return r;
}

static SHORT WINAPI hk_GetAsyncKeyState(int vk) {
    SHORT r = real_GetAsyncKeyState(vk);
    if (vk > 0 && vk < 256) {
        if (g_managed[vk]) {
            SHORT out = 0;
            for (int i = 0; i < g_phys_n[vk]; i++) out |= real_GetAsyncKeyState(g_phys[vk][i]);
            if (g_pad_down[vk]) out |= (SHORT)0x8000;
            return (SHORT)(out & 0x8001);
        }
        if (g_to_game[vk]) return 0;
    }
    return r;
}

static BOOL WINAPI hk_GetKeyboardState(PBYTE state) {
    BOOL ok = real_GetKeyboardState(state);
    if (!ok || !state) return ok;
    BYTE orig[256];
    CopyMemory(orig, state, 256);
    for (int vk = 1; vk < 256; vk++) {
        if (g_managed[vk]) {
            BYTE down = g_pad_down[vk] ? 0x80 : 0;
            for (int i = 0; i < g_phys_n[vk]; i++)
                if (orig[g_phys[vk][i]] & 0x80) down = 0x80;
            state[vk] = (BYTE)((orig[vk] & 1) | down);
        } else if (g_to_game[vk]) {
            state[vk] = (BYTE)(orig[vk] & 1);
        }
    }
    return ok;
}

/* ---- keyboard messages --------------------------------------------------- */

/* lParam for a key message: repeat count 1, scan code, extended bit for the
 * arrow/navigation keys. */
static LPARAM key_lparam(int vk) {
    UINT scan = MapVirtualKeyA((UINT)vk, 0 /* MAPVK_VK_TO_VSC */) & 0xff;
    LPARAM lp = (LPARAM)scan << 16;
    if (vk >= VK_PRIOR && vk <= VK_DOWN) lp |= (LPARAM)1 << 24;
    return lp;
}

static void translate_msg(LPMSG m) {
    if (!m) return;
    if (m->message != WM_KEYDOWN && m->message != WM_KEYUP &&
        m->message != WM_SYSKEYDOWN && m->message != WM_SYSKEYUP) return;
    int vk = (int)(m->wParam & 0xff);
    int game = g_to_game[vk];
    if (game) {
        if (game == vk) return;
        m->wParam = (WPARAM)game;
        m->lParam = (m->lParam & ~(LPARAM)0x01ff0000) | key_lparam(game);   /* scan code + extended bit */
        /* Alt-combos arrive as WM_SYSKEY*; the game only handles WM_KEY*. */
        if (m->message == WM_SYSKEYDOWN) m->message = WM_KEYDOWN;
        else if (m->message == WM_SYSKEYUP) m->message = WM_KEYUP;
    } else if (g_managed[vk]) {
        m->message = WM_NULL;     /* this game key now lives on another physical key */
    }
}

/* Hands the game a queued gamepad press/release as a keyboard message,
 * addressed to whichever game window has the keyboard focus. Only when the
 * caller's filter would accept a key message. */
static BOOL inject_pad(LPMSG m, HWND h, UINT lo, UINT hi, UINT flags) {
    if (!g_pad_on || g_q_head == g_q_tail || !m) return FALSE;
    if (!(lo == 0 && hi == 0) && !(lo <= WM_KEYDOWN && hi >= WM_KEYUP)) return FALSE;
    if (h == (HWND)-1) return FALSE;                 /* thread messages only */
    HWND target = GetFocus();
    if (!target) target = GetActiveWindow();
    if (!target) return FALSE;
    if (h && h != target && !IsChild(h, target)) return FALSE;
    WORD ev = g_q[(DWORD)g_q_tail % PADQ];
    int vk = ev & 0xff, down = (ev & PADQ_DOWN) != 0;
    LPARAM lp = 1 | key_lparam(vk);
    if (!down) lp |= (LPARAM)0xC0000000;            /* previous state + transition */
    m->hwnd = target;
    m->message = down ? WM_KEYDOWN : WM_KEYUP;
    m->wParam = (WPARAM)vk;
    m->lParam = lp;
    m->time = GetTickCount();
    GetCursorPos(&m->pt);
    if (flags & PM_REMOVE) InterlockedIncrement(&g_q_tail);
    return TRUE;
}

static BOOL WINAPI hk_PeekMessageA(LPMSG m, HWND h, UINT lo, UINT hi, UINT flags) {
    if (!g_game_tid) g_game_tid = GetCurrentThreadId();
    if (inject_pad(m, h, lo, hi, flags)) return TRUE;
    BOOL r = real_PeekMessageA(m, h, lo, hi, flags);
    if (r) {
        window_check_message(m);    /* before remapping, so Cmd+Q is seen as Q */
        translate_msg(m);
    }
    return r;
}

static BOOL WINAPI hk_GetMessageA(LPMSG m, HWND h, UINT lo, UINT hi) {
    if (!g_game_tid) g_game_tid = GetCurrentThreadId();
    if (inject_pad(m, h, lo, hi, PM_REMOVE)) return TRUE;
    BOOL r = real_GetMessageA(m, h, lo, hi);
    if (r > 0) translate_msg(m);
    return r;
}

static void install_input_hooks(void) {
    HMODULE u = GetModuleHandleA("user32.dll");
    if (!u) { plog("keys: user32 not loaded, remapping disabled"); return; }
    real_GetKeyState      = (PFN_GETKEYSTATE)(void *)GetProcAddress(u, "GetKeyState");
    real_GetAsyncKeyState = (PFN_GETKEYSTATE)(void *)GetProcAddress(u, "GetAsyncKeyState");
    real_GetKeyboardState = (PFN_GETKEYBOARDSTATE)(void *)GetProcAddress(u, "GetKeyboardState");
    real_PeekMessageA     = (PFN_PEEKMESSAGEA)(void *)GetProcAddress(u, "PeekMessageA");
    real_GetMessageA      = (PFN_GETMESSAGEA)(void *)GetProcAddress(u, "GetMessageA");
    if (!real_GetKeyState || !real_GetAsyncKeyState || !real_GetKeyboardState || !real_PeekMessageA) {
        plog("keys: user32 exports missing, remapping disabled");
        return;
    }
    int a = patch_iat((void *)real_GetKeyState, (void *)hk_GetKeyState);
    int b = patch_iat((void *)real_GetAsyncKeyState, (void *)hk_GetAsyncKeyState);
    int c = patch_iat((void *)real_GetKeyboardState, (void *)hk_GetKeyboardState);
    int d = patch_iat((void *)real_PeekMessageA, (void *)hk_PeekMessageA);
    int e = real_GetMessageA ? patch_iat((void *)real_GetMessageA, (void *)hk_GetMessageA) : 0;
    plog("keys: remapping on (hooks: GetKeyState %d, GetAsyncKeyState %d, GetKeyboardState %d, "
         "PeekMessageA %d, GetMessageA %d)", a, b, c, d, e);
    if (!a && !d) plog("keys: WARNING no import slots matched; remapping will not work");
}

/* ---- gamepad thread ------------------------------------------------------ */

static DWORD WINAPI pad_thread(LPVOID unused) {
    (void)unused;
    typedef UINT (WINAPI *PFN_TBP)(UINT);
    HMODULE w = GetModuleHandleA("winmm.dll");
    PFN_TBP tbp = w ? (PFN_TBP)(void *)GetProcAddress(w, "timeBeginPeriod") : NULL;
    if (tbp) tbp(1);                             /* 1 ms Sleep resolution */
    int last_src = -1;
    int held[256] = { 0 };
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        int src = PAD_SRC_NONE;
        pad_bits b = pad_read_dev(&src, g_pad_dev);
        if (src != last_src) {
            plog("pad: %s", src == PAD_SRC_XINPUT ? "XInput controller connected" :
                            src == PAD_SRC_JOY ? "joystick connected" : "no controller connected");
            last_src = src;
        }
        /* only drive the game while it is the active app */
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) b = 0;
        int wake = 0;
        for (int vk = 1; vk < 256; vk++) {
            if (!g_pad_mask[vk]) continue;
            int d = (b & g_pad_mask[vk]) != 0;
            if (d == held[vk]) continue;
            held[vk] = d;
            InterlockedExchange(&g_pad_down[vk], d);
            if ((DWORD)(g_q_head - g_q_tail) < PADQ) {
                g_q[(DWORD)g_q_head % PADQ] = (WORD)(vk | (d ? PADQ_DOWN : 0));
                MemoryBarrier();
                InterlockedIncrement(&g_q_head);
                wake = 1;
            }
        }
        if (wake && g_game_tid) PostThreadMessageA(g_game_tid, WM_NULL, 0, 0);
        Sleep(src ? 4 : 400);    /* a disconnected pad is checked less often */
    }
    return 0;
}

/* ---- start-up ------------------------------------------------------------ */

void input_init(void) {
    load_keys();
    int pad = load_pad();
    install_input_hooks();           /* also needed for quit handling */
    if (pad && real_GetKeyState) {
        HANDLE t = CreateThread(NULL, 0, pad_thread, NULL, 0, NULL);
        if (t) CloseHandle(t); else plog("pad: could not start the gamepad thread");
    }
}
