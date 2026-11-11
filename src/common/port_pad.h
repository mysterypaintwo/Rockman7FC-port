/* ---------------------------------------------------------------------------
 * Gamepad input shared by the proxy and the settings window.
 *
 * Every button, trigger and stick direction is one "pad input" (PAD_*). A pad
 * is read through XInput (Xbox-style pads; Wine maps most controllers here)
 * and, if no XInput pad is connected, through the older winmm joystick API
 * (generic pads: Button1..16, POV hat as D-pad, X/Y as left stick, Z/R as
 * right stick). With Device=any, all connected pads are combined.
 * ------------------------------------------------------------------------- */
#ifndef PORT_PAD_H
#define PORT_PAD_H

#include "port_common.h"

typedef unsigned long long pad_bits;     /* one bit per pad input */
#define PAD_BIT(i) ((pad_bits)1 << (i))

enum {
    /* XInput buttons, in the order of their XINPUT_GAMEPAD_* bits */
    PAD_DUP, PAD_DDOWN, PAD_DLEFT, PAD_DRIGHT, PAD_START, PAD_BACK, PAD_LS, PAD_RS,
    PAD_LB, PAD_RB, PAD_A, PAD_B, PAD_X, PAD_Y,
    /* triggers and stick directions */
    PAD_LT, PAD_RT,
    PAD_LUP, PAD_LDOWN, PAD_LLEFT, PAD_LRIGHT, PAD_RUP, PAD_RDOWN, PAD_RLEFT, PAD_RRIGHT,
    /* generic joystick buttons: PAD_BUTTON1 .. PAD_BUTTON1 + 15 */
    PAD_BUTTON1,
    PAD_COUNT = PAD_BUTTON1 + 16
};

extern const char *const PAD_NAMES[PAD_COUNT][2];          /* ini name, label */
extern const char *const PAD_DEFAULTS[PORT_ACTION_COUNT];  /* same order as PORT_ACTIONS */

int pad_parse(const char *s);                    /* -1 if unknown */
int pad_list(const char *s, int *out, int max);  /* "A, B" -> pad inputs */

/* Which controller to listen to. Stored in port.ini as
 * Device=any|xinput1..4|joy1..16. */
#define PAD_ANY 0
#define PAD_XINPUT(i) (1 + (i))        /* XInput slot 0..3 */
#define PAD_JOY(i) (101 + (i))         /* joystick id 0..15 */

int  pad_device_parse(const char *s);
void pad_device_name(int dev, char *out);

/* What pad_read_dev found. */
enum { PAD_SRC_NONE, PAD_SRC_XINPUT, PAD_SRC_JOY };

/* Reads the selected pad(s); *source (may be NULL) gets a PAD_SRC_*. */
pad_bits pad_read_dev(int *source, int dev);

/* ---- winmm joystick access (also used to list devices) ------------------ */
typedef struct {
    DWORD dwSize, dwFlags, dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos;
    DWORD dwButtons, dwButtonNumber, dwPOV, dwReserved1, dwReserved2;
} pad_joyinfoex;                       /* JOYINFOEX without needing mmsystem.h */

UINT pad_joy_count(void);
int  pad_joy_read(UINT id, pad_joyinfoex *ji);   /* 1 if that joystick answered */

#endif
