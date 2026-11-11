/* ---------------------------------------------------------------------------
 * pxtone-proxy.dll: what the proxy's files share
 *
 *   main.c     DllMain, logging, finding port.ini, config.ini, import patching
 *   exports.c  the pxtone exports, forwarded to the real pxtone DLL
 *   audio.c    soundtrack choice, sound-effect volume, music volume
 *   input.c    key remapping and gamepad input
 *   window.c   quit handling and display scaling
 * ------------------------------------------------------------------------- */
#ifndef PROXY_H
#define PROXY_H

#include "port_common.h"

/* ---- main.c -------------------------------------------------------------- */
extern char g_dir[MAX_PATH];      /* game directory, trailing backslash */
extern char g_ini[MAX_PATH];      /* full path of port.ini */
extern char g_snd[MAX_PATH];      /* release folder that supplies bgm\ se\ and pxtone */
extern volatile LONG g_stop;      /* set when the DLL unloads; ends the threads */
extern int g_trace;               /* [Debug] Trace=1 */

/* Appends a line to port.log (wsprintf format). */
void plog(const char *fmt, ...);

static inline int ini_int(const char *sec, const char *key, int def, int lo, int hi) {
    return port_ini_int(g_ini, sec, key, def, lo, hi);
}

/* Points every import slot of module `mod` (patch_iat: the game exe) that
 * holds `real` at `hook`. Returns how many slots were changed. */
int patch_iat_in(HMODULE mod, void *real, void *hook);
int patch_iat(void *real, void *hook);

/* ---- exports.c ----------------------------------------------------------- */
int   px_resolve(HMODULE real_dll);   /* fills the export table; returns how many are absent */
void *px_export(const char *name);    /* the real function, or NULL if absent */

/* ---- audio.c ------------------------------------------------------------- */
void pick_soundtrack(void);
void load_pxtone(void);
void apply_se_volume(void);
void install_file_hooks(void);
void start_volume_thread(void);

/* ---- input.c ------------------------------------------------------------- */
void input_init(void);

/* ---- window.c ------------------------------------------------------------ */
void install_display_hooks(void);
void window_check_message(const MSG *m);   /* every message the game peeks */

#endif
