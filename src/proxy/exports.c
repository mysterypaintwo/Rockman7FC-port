/* ---------------------------------------------------------------------------
 * The pxtone exports
 *
 * The game imports pxtone.dll by name, so this DLL exports everything the
 * real one does (pxtone_proxy.def). Each export is a one-instruction jump
 * through g_px[], filled at start-up from the pxtone_real.dll of whichever
 * release supplies the soundtrack; static forwarders would tie the game to
 * one DLL. Jumping keeps the caller's arguments and calling convention
 * untouched.
 * ------------------------------------------------------------------------- */
#include "proxy.h"

/* Same order as px_stub_0.. in pxtone_proxy.def. */
static const char *const PX_NAMES[] = {
    "pxtone_GetDirectSound",
    "pxtone_GetLastError",
    "pxtone_GetQuality",
    "pxtone_Noise_Create",
    "pxtone_Noise_Release",
    "pxtone_Ready",
    "pxtone_Release",
    "pxtone_Reset",
    "pxtone_Tune_Fadeout",
    "pxtone_Tune_GetComment",
    "pxtone_Tune_GetInformation",
    "pxtone_Tune_GetName",
    "pxtone_Tune_GetPlayMeas",
    "pxtone_Tune_GetRepeatMeas",
    "pxtone_Tune_IsStreaming",
    "pxtone_Tune_Load",
    "pxtone_Tune_Read",
    "pxtone_Tune_Release",
    "pxtone_Tune_SetLoop",
    "pxtone_Tune_SetVolume",
    "pxtone_Tune_Start",
    "pxtone_Tune_Stop",
    "pxtone_Tune_Vomit",
};
#define PX_COUNT ((int)(sizeof PX_NAMES / sizeof PX_NAMES[0]))

void *g_px[PX_COUNT];        /* not static: the stubs below jump through it */

/* px_stub_N: jmp [g_px + N*4]. One per name above. */
#define PX_STUB(i) __asm__(".text\n.globl _px_stub_" #i "\n_px_stub_" #i ":\n\tjmp *_g_px+" #i "*4\n");
PX_STUB(0)  PX_STUB(1)  PX_STUB(2)  PX_STUB(3)  PX_STUB(4)  PX_STUB(5)
PX_STUB(6)  PX_STUB(7)  PX_STUB(8)  PX_STUB(9)  PX_STUB(10) PX_STUB(11)
PX_STUB(12) PX_STUB(13) PX_STUB(14) PX_STUB(15) PX_STUB(16) PX_STUB(17)
PX_STUB(18) PX_STUB(19) PX_STUB(20) PX_STUB(21) PX_STUB(22)

/* Stand-in for exports an older pxtone.dll lacks; the game never calls those
 * (they only exist in the newer DLL). */
static int __cdecl px_missing(void) { return 0; }

int px_resolve(HMODULE real_dll) {
    int missing = 0;
    for (int i = 0; i < PX_COUNT; i++) {
        void *f = real_dll ? (void *)GetProcAddress(real_dll, PX_NAMES[i]) : NULL;
        if (!f) { f = (void *)px_missing; missing++; }
        g_px[i] = f;
    }
    return missing;
}

void *px_export(const char *name) {
    for (int i = 0; i < PX_COUNT; i++)
        if (!lstrcmpA(PX_NAMES[i], name)) return g_px[i] == (void *)px_missing ? NULL : g_px[i];
    return NULL;
}
