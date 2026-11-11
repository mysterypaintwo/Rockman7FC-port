/* ---------------------------------------------------------------------------
 * The settings and port.ini
 * ------------------------------------------------------------------------- */
#include "settings.h"

settings S;
const int ZOOMS[ZOOM_COUNT] = { 100, 125, 150, 200 };

void compact_slots(int slots[PORT_KEYS_PER_ACTION]) {
    if (!slots[0] && slots[1]) {
        slots[0] = slots[1];
        slots[1] = 0;
    }
}

void default_keys(void) {
    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        S.keys[a][0] = port_key_parse(PORT_ACTIONS[a].def);
        for (int s = 1; s < PORT_KEYS_PER_ACTION; s++) S.keys[a][s] = 0;
    }
}

/* "A, B" -> pad slots (pad input + 1, 0 = none). */
static void parse_pad_slots(const char *val, int slots[PORT_KEYS_PER_ACTION]) {
    int in[PORT_KEYS_PER_ACTION];
    int n = pad_list(val, in, PORT_KEYS_PER_ACTION);
    for (int s = 0; s < PORT_KEYS_PER_ACTION; s++) slots[s] = s < n ? in[s] + 1 : 0;
}

void default_pad(void) {
    for (int a = 0; a < PORT_ACTION_COUNT; a++) parse_pad_slots(PAD_DEFAULTS[a], S.pad[a]);
}

void restore_defaults(void) {
    S.scale = 3;
    S.full = 0;
    S.pad_dev = PAD_ANY;
    S.pad_dev_name[0] = 0;
    S.lang_en = release_for(1)->installed;
    S.track_en = S.lang_en;
    S.music = S.fx = 1;
    S.music_vol = S.fx_vol = 100;
    default_keys();
    default_pad();
    S.pad_on = 1;
    S.vdesk = S.gdi = S.gamescope = 0;
}

/* First run: match the system's display scaling. */
static int default_zoom(void) {
    HDC sdc = GetDC(NULL);
    int dpi = sdc ? GetDeviceCaps(sdc, LOGPIXELSX) : 96;
    if (sdc) ReleaseDC(NULL, sdc);
    return dpi >= 192 ? 200 : dpi >= 144 ? 150 : dpi >= 120 ? 125 : 100;
}

void load_settings(void) {
    char v[128];
    int have_ja = release_for(0)->installed, have_en = release_for(1)->installed;

    S.scale = ini_int("Display", "Scale", 3, 1, 16);
    S.full = ini_int("Display", "Fullscreen", 0, 0, 2);
    /* language and soundtrack fall back to whichever release is installed */
    GetPrivateProfileStringA("Display", "Language", have_en ? "en" : "ja", v, sizeof v, g_ini);
    S.lang_en = port_ieq(v, "en");
    if (S.lang_en && !have_en && have_ja) S.lang_en = 0;
    if (!S.lang_en && !have_ja && have_en) S.lang_en = 1;
    GetPrivateProfileStringA("Sound", "Soundtrack", have_en ? "en" : "ja", v, sizeof v, g_ini);
    S.track_en = port_ieq(v, "en");
    if (S.track_en && !have_en) S.track_en = 0;
    if (!S.track_en && !have_ja && have_en) S.track_en = 1;

    S.music = ini_int("Sound", "Music", 1, 0, 1);
    S.music_vol = ini_int("Sound", "MusicVolume", 100, 0, 100);
    S.fx = ini_int("Sound", "Effects", 1, 0, 1);
    S.fx_vol = ini_int("Sound", "EffectsVolume", 100, 0, 100);

    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        GetPrivateProfileStringA("Keys", PORT_ACTIONS[a].ini, PORT_ACTIONS[a].def, v, sizeof v, g_ini);
        int k[PORT_KEYS_PER_ACTION] = { 0 };
        port_key_list(v, k, PORT_KEYS_PER_ACTION);
        for (int s = 0; s < PORT_KEYS_PER_ACTION; s++) S.keys[a][s] = k[s];
    }

    S.pad_on = ini_int("Pad", "Enabled", 1, 0, 1);
    GetPrivateProfileStringA("Pad", "Device", "any", v, sizeof v, g_ini);
    S.pad_dev = pad_device_parse(v);
    GetPrivateProfileStringA("Pad", "DeviceName", "", S.pad_dev_name, sizeof S.pad_dev_name, g_ini);
    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        GetPrivateProfileStringA("Pad", PORT_ACTIONS[a].ini, PAD_DEFAULTS[a], v, sizeof v, g_ini);
        parse_pad_slots(v, S.pad[a]);
    }

    S.vdesk = ini_int("Wine", "VirtualDesktop", 0, 0, 1);
    GetPrivateProfileStringA("Wine", "Renderer", "default", v, sizeof v, g_ini);
    S.gdi = port_ieq(v, "gdi");
    S.gamescope = ini_int("Wine", "Gamescope", 0, 0, 1);

    GetPrivateProfileStringA("Editor", "Theme", "dark", v, sizeof v, g_ini);
    S.dark = !port_ieq(v, "light");
    S.accent = ini_int("Editor", "Accent", 0, 0, ACCENT_COUNT - 1);
    int z = ini_int("Editor", "Zoom", default_zoom(), 50, 400);
    S.zoom = 0;
    for (int i = 0; i < ZOOM_COUNT; i++) if (ZOOMS[i] == z) S.zoom = i;
}

static void ini_put_int(const char *sec, const char *key, int v) {
    char b[16];
    wsprintfA(b, "%d", v);
    WritePrivateProfileStringA(sec, key, b, g_ini);
}

static void append_item(char *list, const char *item) {
    if (list[0]) lstrcatA(list, ", ");
    lstrcatA(list, item);
}

void save_settings(void) {
    char v[128];
    ini_put_int("Display", "Scale", S.scale);
    ini_put_int("Display", "Fullscreen", S.full);
    WritePrivateProfileStringA("Display", "Language", S.lang_en ? "en" : "ja", g_ini);
    WritePrivateProfileStringA("Sound", "Soundtrack", S.track_en ? "en" : "ja", g_ini);
    ini_put_int("Sound", "Music", S.music);
    ini_put_int("Sound", "MusicVolume", S.music_vol);
    ini_put_int("Sound", "Effects", S.fx);
    ini_put_int("Sound", "EffectsVolume", S.fx_vol);
    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        v[0] = 0;
        for (int s = 0; s < PORT_KEYS_PER_ACTION; s++) {
            if (!S.keys[a][s]) continue;
            char k[16];
            port_key_name(S.keys[a][s], k);
            append_item(v, k);
        }
        WritePrivateProfileStringA("Keys", PORT_ACTIONS[a].ini, v, g_ini);
    }
    ini_put_int("Pad", "Enabled", S.pad_on);
    pad_device_name(S.pad_dev, v);
    WritePrivateProfileStringA("Pad", "Device", v, g_ini);
    WritePrivateProfileStringA("Pad", "DeviceName", S.pad_dev == PAD_ANY ? "" : S.pad_dev_name, g_ini);
    for (int a = 0; a < PORT_ACTION_COUNT; a++) {
        v[0] = 0;
        for (int s = 0; s < PORT_KEYS_PER_ACTION; s++)
            if (S.pad[a][s]) append_item(v, PAD_NAMES[S.pad[a][s] - 1][0]);
        WritePrivateProfileStringA("Pad", PORT_ACTIONS[a].ini, v, g_ini);
    }
    ini_put_int("Wine", "VirtualDesktop", S.vdesk);
    WritePrivateProfileStringA("Wine", "Renderer", S.gdi ? "gdi" : "default", g_ini);
    ini_put_int("Wine", "Gamescope", S.gamescope);
    WritePrivateProfileStringA("Editor", "Theme", S.dark ? "dark" : "light", g_ini);
    ini_put_int("Editor", "Accent", S.accent);
    ini_put_int("Editor", "Zoom", ZOOMS[S.zoom]);
    WritePrivateProfileStringA(NULL, NULL, NULL, g_ini);   /* flush */
}
