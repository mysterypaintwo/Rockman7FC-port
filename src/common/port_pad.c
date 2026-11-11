/* ---------------------------------------------------------------------------
 * Gamepad names and reading; see port_pad.h.
 * ------------------------------------------------------------------------- */
#include "port_pad.h"

#define ARROW_UP    "\xE2\x86\x91"
#define ARROW_DOWN  "\xE2\x86\x93"
#define ARROW_LEFT  "\xE2\x86\x90"
#define ARROW_RIGHT "\xE2\x86\x92"

const char *const PAD_NAMES[][2] = {
    { "DPadUp", "D-pad " ARROW_UP }, { "DPadDown", "D-pad " ARROW_DOWN },
    { "DPadLeft", "D-pad " ARROW_LEFT }, { "DPadRight", "D-pad " ARROW_RIGHT },
    { "Start", "Start" }, { "Back", "Back" }, { "LS", "L-stick press" }, { "RS", "R-stick press" },
    { "LB", "LB" }, { "RB", "RB" }, { "A", "A" }, { "B", "B" }, { "X", "X" }, { "Y", "Y" },
    { "LT", "LT" }, { "RT", "RT" },
    { "LStickUp", "L-stick " ARROW_UP }, { "LStickDown", "L-stick " ARROW_DOWN },
    { "LStickLeft", "L-stick " ARROW_LEFT }, { "LStickRight", "L-stick " ARROW_RIGHT },
    { "RStickUp", "R-stick " ARROW_UP }, { "RStickDown", "R-stick " ARROW_DOWN },
    { "RStickLeft", "R-stick " ARROW_LEFT }, { "RStickRight", "R-stick " ARROW_RIGHT },
    { "Button1", "Button 1" }, { "Button2", "Button 2" }, { "Button3", "Button 3" },
    { "Button4", "Button 4" }, { "Button5", "Button 5" }, { "Button6", "Button 6" },
    { "Button7", "Button 7" }, { "Button8", "Button 8" }, { "Button9", "Button 9" },
    { "Button10", "Button 10" }, { "Button11", "Button 11" }, { "Button12", "Button 12" },
    { "Button13", "Button 13" }, { "Button14", "Button 14" }, { "Button15", "Button 15" },
    { "Button16", "Button 16" },
};

const char *const PAD_DEFAULTS[] = {
    "DPadUp, LStickUp", "DPadDown, LStickDown", "DPadLeft, LStickLeft", "DPadRight, LStickRight",
    "A", "X, B", "LB", "RB", "Start", "Back",
};

int pad_parse(const char *s) {
    for (int i = 0; i < PAD_COUNT; i++) if (port_ieq(s, PAD_NAMES[i][0])) return i;
    return -1;
}

int pad_list(const char *s, int *out, int max) { return port_list(s, pad_parse, -1, out, max); }

int pad_device_parse(const char *s) {
    if ((s[0] == 'x' || s[0] == 'X') && lstrlenA(s) == 7 && s[6] >= '1' && s[6] <= '4') return PAD_XINPUT(s[6] - '1');
    if ((s[0] == 'j' || s[0] == 'J') && (s[1] == 'o' || s[1] == 'O') && (s[2] == 'y' || s[2] == 'Y')) {
        int n = 0;
        for (const char *p = s + 3; *p >= '0' && *p <= '9' && n <= 16; p++) n = n * 10 + (*p - '0');
        if (n >= 1 && n <= 16) return PAD_JOY(n - 1);
    }
    return PAD_ANY;
}

void pad_device_name(int dev, char *out) {
    if (dev >= PAD_JOY(0)) wsprintfA(out, "joy%d", dev - PAD_JOY(0) + 1);
    else if (dev >= PAD_XINPUT(0)) wsprintfA(out, "xinput%d", dev - PAD_XINPUT(0) + 1);
    else lstrcpyA(out, "any");
}

/* ---- loading the APIs ---------------------------------------------------- */

typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } pad_xgamepad;
typedef struct { DWORD packet; pad_xgamepad pad; } pad_xstate;
typedef DWORD (WINAPI *PFN_XGETSTATE)(DWORD, pad_xstate *);
typedef UINT  (WINAPI *PFN_JOYGETPOSEX)(UINT, pad_joyinfoex *);
typedef UINT  (WINAPI *PFN_JOYGETNUMDEVS)(void);

static PFN_XGETSTATE     pad_xget;
static PFN_JOYGETPOSEX   pad_joyget;
static PFN_JOYGETNUMDEVS pad_joynum;

static void pad_init(void) {
    static int loaded;
    if (loaded) return;
    loaded = 1;
    static const char *const libs[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !pad_xget; i++) {
        HMODULE m = LoadLibraryA(libs[i]);
        if (m) pad_xget = (PFN_XGETSTATE)(void *)GetProcAddress(m, "XInputGetState");
    }
    HMODULE w = LoadLibraryA("winmm.dll");
    if (w) {
        pad_joyget = (PFN_JOYGETPOSEX)(void *)GetProcAddress(w, "joyGetPosEx");
        pad_joynum = (PFN_JOYGETNUMDEVS)(void *)GetProcAddress(w, "joyGetNumDevs");
    }
}

UINT pad_joy_count(void) {
    pad_init();
    if (!pad_joyget) return 0;
    UINT n = pad_joynum ? pad_joynum() : 16;
    return n > 16 ? 16 : n;
}

int pad_joy_read(UINT id, pad_joyinfoex *ji) {
    pad_init();
    if (!pad_joyget) return 0;
    ZeroMemory(ji, sizeof *ji);
    ji->dwSize = sizeof *ji;
    ji->dwFlags = 0xff;                  /* JOY_RETURNALL */
    return pad_joyget(id, ji) == 0;
}

/* ---- reading ------------------------------------------------------------- */

#define STICK_DEAD  16000                /* XInput stick: -32768..32767 */
#define TRIGGER_MIN 64                   /* XInput trigger: 0..255 */
#define JOY_LOW     16000                /* joystick axis: 0..65535, centre 32767 */
#define JOY_HIGH    49500

static pad_bits read_xinput(int dev, int *found) {
    pad_bits b = 0;
    static const WORD bits[PAD_Y + 1] = { 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80,
                                          0x100, 0x200, 0x1000, 0x2000, 0x4000, 0x8000 };
    for (DWORD i = 0; i < 4; i++) {
        if (dev != PAD_ANY && dev != PAD_XINPUT((int)i)) continue;
        pad_xstate st;
        ZeroMemory(&st, sizeof st);
        if (pad_xget(i, &st) != 0) continue;
        *found = 1;
        for (int k = 0; k <= PAD_Y; k++) if (st.pad.buttons & bits[k]) b |= PAD_BIT(k);
        if (st.pad.lt > TRIGGER_MIN) b |= PAD_BIT(PAD_LT);
        if (st.pad.rt > TRIGGER_MIN) b |= PAD_BIT(PAD_RT);
        if (st.pad.ly >  STICK_DEAD) b |= PAD_BIT(PAD_LUP);
        if (st.pad.ly < -STICK_DEAD) b |= PAD_BIT(PAD_LDOWN);
        if (st.pad.lx < -STICK_DEAD) b |= PAD_BIT(PAD_LLEFT);
        if (st.pad.lx >  STICK_DEAD) b |= PAD_BIT(PAD_LRIGHT);
        if (st.pad.ry >  STICK_DEAD) b |= PAD_BIT(PAD_RUP);
        if (st.pad.ry < -STICK_DEAD) b |= PAD_BIT(PAD_RDOWN);
        if (st.pad.rx < -STICK_DEAD) b |= PAD_BIT(PAD_RLEFT);
        if (st.pad.rx >  STICK_DEAD) b |= PAD_BIT(PAD_RRIGHT);
    }
    return b;
}

static pad_bits read_joysticks(int dev, int *found) {
    pad_bits b = 0;
    /* Pads without an axis can report 0 for it, so an axis only counts once
     * it has been seen centred. */
    static unsigned char centred[16][4];
    UINT n = pad_joy_count();
    for (UINT j = 0; j < n; j++) {
        if (dev != PAD_ANY && dev != PAD_JOY((int)j)) continue;
        pad_joyinfoex ji;
        if (!pad_joy_read(j, &ji)) continue;
        *found = 1;
        for (int k = 0; k < 16; k++) if (ji.dwButtons & (1u << k)) b |= PAD_BIT(PAD_BUTTON1 + k);
        if (ji.dwPOV != 0xffff) {
            DWORD p = ji.dwPOV;          /* hundredths of a degree, 0 = up */
            if (p >= 31500 || p <= 4500)  b |= PAD_BIT(PAD_DUP);
            if (p >= 4500 && p <= 13500)  b |= PAD_BIT(PAD_DRIGHT);
            if (p >= 13500 && p <= 22500) b |= PAD_BIT(PAD_DDOWN);
            if (p >= 22500 && p <= 31500) b |= PAD_BIT(PAD_DLEFT);
        }
        DWORD ax[4] = { ji.dwXpos, ji.dwYpos, ji.dwZpos, ji.dwRpos };
        for (int k = 0; k < 4; k++)
            if (ax[k] > 24000 && ax[k] < 41500) centred[j][k] = 1;
        if (centred[j][1] && ji.dwYpos < JOY_LOW)  b |= PAD_BIT(PAD_LUP);
        if (centred[j][1] && ji.dwYpos > JOY_HIGH) b |= PAD_BIT(PAD_LDOWN);
        if (centred[j][0] && ji.dwXpos < JOY_LOW)  b |= PAD_BIT(PAD_LLEFT);
        if (centred[j][0] && ji.dwXpos > JOY_HIGH) b |= PAD_BIT(PAD_LRIGHT);
        if (centred[j][3] && ji.dwRpos < JOY_LOW)  b |= PAD_BIT(PAD_RUP);
        if (centred[j][3] && ji.dwRpos > JOY_HIGH) b |= PAD_BIT(PAD_RDOWN);
        if (centred[j][2] && ji.dwZpos < JOY_LOW)  b |= PAD_BIT(PAD_RLEFT);
        if (centred[j][2] && ji.dwZpos > JOY_HIGH) b |= PAD_BIT(PAD_RRIGHT);
    }
    return b;
}

pad_bits pad_read_dev(int *source, int dev) {
    pad_bits b = 0;
    int src = PAD_SRC_NONE, found = 0;
    pad_init();
    if (pad_xget) {
        b |= read_xinput(dev, &found);
        if (found) src = PAD_SRC_XINPUT;
    }
    /* joysticks: when one is selected, or as the fallback with no XInput pad */
    if (dev >= PAD_JOY(0) || (dev == PAD_ANY && !src)) {
        found = 0;
        b |= read_joysticks(dev, &found);
        if (found) src = PAD_SRC_JOY;
    }
    if (source) *source = src;
    return b;
}
