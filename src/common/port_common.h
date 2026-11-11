/* ---------------------------------------------------------------------------
 * Shared by the pxtone proxy (src/proxy) and the settings window
 * (src/settings): the port.ini layout, key names, paths and small helpers.
 *
 * port.ini lives next to the game executable (or one folder up when both
 * releases are installed) and is the only settings file. Everything else
 * (the game's config.ini, the SE volume, key remapping, BGM volume) is
 * derived from it by the proxy when the game starts.
 * ------------------------------------------------------------------------- */
#ifndef PORT_COMMON_H
#define PORT_COMMON_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define PORT_INI "port.ini"

/* The game's picture at 1x (8:7). */
#define GAME_W 256
#define GAME_H 224

/* ---- actions: the game's hard-coded keys ---------------------------------
 * config.ini is always written with the engine defaults (Attack=X, Jump=Z),
 * so game_vk is the key the game listens for; the proxy maps the player's
 * chosen keys onto it. */
typedef struct {
    const char *ini;        /* key name in [Keys] and [Pad] */
    const char *label;      /* shown in the settings window */
    int game_vk;            /* key the game itself reads */
    const char *def;        /* default [Keys] value */
} port_action;

#define PORT_ACTION_COUNT 10
#define PORT_KEYS_PER_ACTION 2       /* a main slot and an alternate */
extern const port_action PORT_ACTIONS[PORT_ACTION_COUNT];

/* ---- keys ---------------------------------------------------------------- */
void port_key_name(int vk, char *out);              /* out: at least 16 bytes */
int  port_key_parse(const char *s);                 /* 0 if unknown */
int  port_key_list(const char *s, int *out, int max);

/* ---- strings ------------------------------------------------------------- */
int port_ieq(const char *a, const char *b);         /* ASCII case-insensitive */

/* Splits "A, B" at commas and parses each item; items that parse to `none`
 * are skipped. Returns how many were stored. */
int port_list(const char *s, int (*parse)(const char *), int none, int *out, int max);

/* ---- files and paths ----------------------------------------------------- */
void  port_module_dir(HMODULE m, char *out);        /* NULL = the exe; trailing '\' */
void  port_join(char *out, const char *dir, const char *name);
int   port_parent_dir(const char *dir, char *out);  /* "C:\a\b\" -> "C:\a\" */
int   port_exists(const char *path);
int   port_is_dir(const char *path);

/* Whole file, NUL-terminated (not counted in *size); free with port_free. */
BYTE *port_read_file(const char *path, DWORD *size);
void  port_free(void *p);
int   port_write_file(const char *path, const void *data, DWORD size);
void  port_append_line(const char *path, const char *line);

int port_ini_int(const char *ini, const char *sec, const char *key, int def, int lo, int hi);

/* ---- releases ------------------------------------------------------------ */
const char *port_native_language(const char *gamedir);  /* "en" for mm7fc.exe, else "ja" */
const char *port_game_exe(const char *gamedir);         /* rm7fc.exe or mm7fc.exe */

/* ---- Wine ---------------------------------------------------------------- */
int port_is_wine(void);
const char *port_wine_host(void);   /* host system ("Darwin", "Linux") or NULL */

#endif
