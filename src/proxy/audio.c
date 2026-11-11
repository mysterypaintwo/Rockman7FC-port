/* ---------------------------------------------------------------------------
 * Sound: which release's audio is used, the SE volume and the BGM volume
 *
 * Both releases run the same engine build; they differ in text, music,
 * sound effects and pxtone.dll. [Sound] Soundtrack=ja|en picks the release
 * whose audio is used, independent of the language: the real pxtone DLL is
 * loaded from that release, and the game's opens of bgm\* and se\* are
 * redirected to its folder.
 *
 * BGM volume: the real export is  void __cdecl pxtone_Tune_SetVolume(float 0..1)
 * (both releases' DLLs load the argument with `flds` and return with a plain
 * `ret`). The first proxy declared it __stdcall(int); that unbalanced the
 * stack on every call (the crashes in backtrace*.txt) and passed 100 as a
 * denormal float, i.e. silence. Even called correctly it has no audible
 * effect in the game, so the volume is set on pxtone's DirectSound buffer
 * and SetVolume is only a fallback.
 * ------------------------------------------------------------------------- */
#include "proxy.h"
#include <mmsystem.h>
#include <dsound.h>
#include <math.h>

/* Case-insensitive "s starts with prefix". */
static int starts_with_i(const char *s, const char *prefix) {
    int n = lstrlenA(prefix);
    return lstrlenA(s) >= n &&
           CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, s, n, prefix, n) == CSTR_EQUAL;
}

/* ---- choosing the soundtrack --------------------------------------------- */

void pick_soundtrack(void) {
    char want[8], mine[4], root[MAX_PATH], cand[MAX_PATH], probe[MAX_PATH], bgm[MAX_PATH];
    lstrcpyA(mine, port_native_language(g_dir));
    lstrcpyA(g_snd, g_dir);
    GetPrivateProfileStringA("Sound", "Soundtrack", mine, want, sizeof want, g_ini);
    if (!port_ieq(want, "ja") && !port_ieq(want, "en")) return;
    if (port_ieq(want, mine)) return;
    /* the other release is the sibling folder: <root>\ja\ <-> <root>\en\ */
    if (!port_parent_dir(g_dir, root)) return;
    wsprintfA(cand, "%s%s\\", root, port_ieq(want, "en") ? "en" : "ja");
    port_join(probe, cand, "pxtone_real.dll");
    port_join(bgm, cand, "bgm");
    if (port_exists(probe) && port_is_dir(bgm)) lstrcpyA(g_snd, cand);
    else plog("soundtrack: '%s' requested but %s is not installed; using this release's", want, cand);
}

/* ---- redirecting bgm\ and se\ -------------------------------------------- */

typedef HANDLE (WINAPI *PFN_CREATEFILEA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *PFN_FINDFIRSTFILEA)(LPCSTR, LPWIN32_FIND_DATAA);
typedef void  *(WINAPI *PFN_MMIOOPENA)(LPSTR, void *, DWORD);
static PFN_CREATEFILEA real_CreateFileA;
static PFN_FINDFIRSTFILEA real_FindFirstFileA;
static PFN_MMIOOPENA real_mmioOpenA;

/* Rewrites "bgm\x" / "se\x" (relative or under the game folder) to the
 * soundtrack's folder. Returns 1 if out was filled. */
static int redirect(const char *in, char *out) {
    if (!in) return 0;
    const char *rest = in;
    if (starts_with_i(in, g_dir)) rest = in + lstrlenA(g_dir);
    while (rest[0] == '.' && (rest[1] == '\\' || rest[1] == '/')) rest += 2;
    if (!starts_with_i(rest, "bgm\\") && !starts_with_i(rest, "se\\")) return 0;
    if (lstrlenA(g_snd) + lstrlenA(rest) >= MAX_PATH) return 0;
    lstrcpyA(out, g_snd);
    lstrcatA(out, rest);
    return 1;
}

static HANDLE WINAPI hk_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                    DWORD disp, DWORD flags, HANDLE tmpl) {
    char alt[MAX_PATH];
    if (redirect(name, alt)) name = alt;
    return real_CreateFileA(name, acc, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI hk_FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA fd) {
    char alt[MAX_PATH];
    if (redirect(name, alt)) name = alt;
    return real_FindFirstFileA(name, fd);
}

static void *WINAPI hk_mmioOpenA(LPSTR name, void *info, DWORD flags) {
    char alt[MAX_PATH];
    if (redirect(name, alt)) name = alt;
    return real_mmioOpenA(name, info, flags);
}

void install_file_hooks(void) {
    if (!lstrcmpiA(g_snd, g_dir)) return;         /* this release's own audio */
    HMODULE k = GetModuleHandleA("kernel32.dll"), w = LoadLibraryA("winmm.dll");
    real_CreateFileA = k ? (PFN_CREATEFILEA)(void *)GetProcAddress(k, "CreateFileA") : NULL;
    real_FindFirstFileA = k ? (PFN_FINDFIRSTFILEA)(void *)GetProcAddress(k, "FindFirstFileA") : NULL;
    real_mmioOpenA = w ? (PFN_MMIOOPENA)(void *)GetProcAddress(w, "mmioOpenA") : NULL;
    int a = real_CreateFileA ? patch_iat((void *)real_CreateFileA, (void *)hk_CreateFileA) : 0;
    int b = real_FindFirstFileA ? patch_iat((void *)real_FindFirstFileA, (void *)hk_FindFirstFileA) : 0;
    int c = real_mmioOpenA ? patch_iat((void *)real_mmioOpenA, (void *)hk_mmioOpenA) : 0;
    plog("soundtrack: music and effects from %s (hooks %d/%d/%d)", g_snd, a, b, c);
}

/* ---- pxtone and its DirectSound buffer ------------------------------------
 * pxtone streams its mix into one DirectSound buffer. The proxy hooks
 * DirectSoundCreate in pxtone_real.dll's imports, adds DSBCAPS_CTRLVOLUME to
 * the buffers pxtone creates, and keeps them so their volume can be set. The
 * game's own DirectSound object (sound effects) is left alone. */

typedef HRESULT (WINAPI *PFN_DSCREATE)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);
typedef HRESULT (WINAPI *PFN_CSB)(IDirectSound *, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER *, LPUNKNOWN);
static PFN_DSCREATE real_DSCreate;
static PFN_CSB real_CSB;
static IDirectSound *volatile g_px_ds;           /* pxtone's DirectSound object */
#define MAX_BUF 8
static IDirectSoundBuffer *g_bufs[MAX_BUF];      /* pxtone's music buffers */
static volatile LONG g_nbuf, g_buf_new;

#define VTBL_CREATE_SOUND_BUFFER 3               /* IDirectSound::CreateSoundBuffer */

static HRESULT WINAPI hk_CreateSoundBuffer(IDirectSound *ds, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *out, LPUNKNOWN u) {
    if (ds != g_px_ds || !desc || (desc->dwFlags & DSBCAPS_PRIMARYBUFFER)) return real_CSB(ds, desc, out, u);
    DSBUFFERDESC d;
    ZeroMemory(&d, sizeof d);
    CopyMemory(&d, desc, desc->dwSize < sizeof d ? desc->dwSize : sizeof d);
    d.dwFlags |= DSBCAPS_CTRLVOLUME;
    HRESULT hr = real_CSB(ds, &d, out, u);
    if (FAILED(hr)) return real_CSB(ds, desc, out, u);
    if (out && *out) {
        LONG i = InterlockedIncrement(&g_nbuf) - 1;
        if (i < MAX_BUF) {
            (*out)->lpVtbl->AddRef(*out);      /* keep it valid for the volume thread */
            g_bufs[i] = *out;
            InterlockedExchange(&g_buf_new, 1);
        }
        plog("bgm: music buffer %ld captured (%lu bytes)", i + 1, d.dwBufferBytes);
    }
    return hr;
}

/* The vtable is shared by every IDirectSound object, so the hook checks
 * which object it was called on. */
static HRESULT WINAPI hk_DirectSoundCreate(LPCGUID g, LPDIRECTSOUND *pds, LPUNKNOWN u) {
    HRESULT hr = real_DSCreate(g, pds, u);
    if (SUCCEEDED(hr) && pds && *pds) {
        g_px_ds = *pds;
        void **slot = &(*(void ***)(*pds))[VTBL_CREATE_SOUND_BUFFER];
        DWORD old;
        if (*slot != (void *)hk_CreateSoundBuffer &&
            VirtualProtect(slot, sizeof *slot, PAGE_EXECUTE_READWRITE, &old)) {
            real_CSB = (PFN_CSB)*slot;
            *slot = (void *)hk_CreateSoundBuffer;
            VirtualProtect(slot, sizeof *slot, old, &old);
        }
    }
    return hr;
}

static void hook_pxtone_dsound(HMODULE m) {
    HMODULE ds = GetModuleHandleA("dsound.dll");
    if (!ds) ds = LoadLibraryA("dsound.dll");
    real_DSCreate = ds ? (PFN_DSCREATE)(void *)GetProcAddress(ds, "DirectSoundCreate") : NULL;
    int n = real_DSCreate ? patch_iat_in(m, (void *)real_DSCreate, (void *)hk_DirectSoundCreate) : 0;
    plog("bgm: DirectSound hook in pxtone %s", n ? "installed" : "NOT installed (using pxtone's own volume)");
}

void load_pxtone(void) {
    char path[MAX_PATH];
    port_join(path, g_snd, "pxtone_real.dll");
    HMODULE m = LoadLibraryA(path);
    if (!m && lstrcmpiA(g_snd, g_dir)) {
        plog("soundtrack: could not load %s; using this release's audio", path);
        lstrcpyA(g_snd, g_dir);
        port_join(path, g_snd, "pxtone_real.dll");
        m = LoadLibraryA(path);
    }
    int missing = px_resolve(m);
    if (!m) {
        plog("pxtone: FAILED to load %s (error %lu)", path, GetLastError());
        return;
    }
    hook_pxtone_dsound(m);
    plog("pxtone: %s (%d optional export(s) absent)", path, missing);
}

/* ---- sound-effect volume -------------------------------------------------
 * The game plays se\*.wav at full volume, so the files themselves are
 * rescaled, always from the pristine copies in port\se_original\. */

/* Scales the PCM data of one WAV in memory. Only 8-bit unsigned and 16-bit
 * signed PCM are touched; chunk sizes are clamped to the real file size. */
static int scale_wav(BYTE *d, DWORD n, int pct) {
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return 0;
    int bits = 0, pcm = 0;
    DWORD pos = 12;
    while (pos + 8 <= n) {
        DWORD sz = *(DWORD *)(d + pos + 4), body = pos + 8;
        if (sz > n - body) sz = n - body;
        if (!memcmp(d + pos, "fmt ", 4) && sz >= 16) {
            pcm = *(WORD *)(d + body) == 1;
            bits = *(WORD *)(d + body + 14);
        } else if (!memcmp(d + pos, "data", 4)) {
            if (!pcm || (bits != 8 && bits != 16)) return 0;
            if (bits == 16) {
                short *s = (short *)(d + body);
                for (DWORD i = 0; i < sz / 2; i++) {
                    int v = s[i] * pct / 100;
                    s[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                }
            } else {
                BYTE *s = d + body;
                for (DWORD i = 0; i < sz; i++) {
                    int v = ((int)s[i] - 128) * pct / 100 + 128;
                    s[i] = (BYTE)(v > 255 ? 255 : v < 0 ? 0 : v);
                }
            }
            return 1;
        }
        pos = body + sz + (sz & 1);
    }
    return 0;
}

/* Volume the files in se\ are at, from the marker file; -1 if unknown. */
static int read_marker(const char *marker) {
    DWORD n = 0;
    BYTE *m = port_read_file(marker, &n);
    int cur = -1;
    if (m && n > 0 && n < 16) {
        cur = 0;
        for (BYTE *p = m; *p >= '0' && *p <= '9'; p++) cur = cur * 10 + (*p - '0');
    }
    port_free(m);
    return cur;
}

static void save_pristine(const char *backup) {
    char dir[MAX_PATH], pat[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    wsprintfA(dir, "%sport", g_snd);
    CreateDirectoryA(dir, NULL);
    CreateDirectoryA(backup, NULL);
    wsprintfA(pat, "%sse\\*.wav", g_snd);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            wsprintfA(src, "%sse\\%s", g_snd, fd.cFileName);
            port_join(dst, backup, fd.cFileName);
            CopyFileA(src, dst, TRUE);
        } while (FindNextFileA(f, &fd));
        FindClose(f);
    }
    plog("se: saved pristine copies in port\\se_original");
}

void apply_se_volume(void) {
    char marker[MAX_PATH], backup[MAX_PATH], pat[MAX_PATH];
    int want = ini_int("Sound", "EffectsVolume", 100, 0, 100);
    /* The marker lives inside se\ so that reinstalling pristine files (which
     * removes it) forces a fresh rescale. */
    wsprintfA(marker, "%sse\\port_volume.txt", g_snd);
    wsprintfA(backup, "%sport\\se_original\\", g_snd);
    int cur = read_marker(marker);
    if (cur == want) return;
    if (cur < 0 && want == 100) return;          /* untouched files are already 100% */

    if (!port_is_dir(backup)) {
        if (cur >= 0 && cur != 100) {
            plog("se: files are at %d%% but port\\se_original is missing; not rescaling", cur);
            return;
        }
        save_pristine(backup);
    }

    port_join(pat, backup, "*.wav");
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pat, &fd);
    int done = 0, failed = 0;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            char src[MAX_PATH], dst[MAX_PATH];
            port_join(src, backup, fd.cFileName);
            wsprintfA(dst, "%sse\\%s", g_snd, fd.cFileName);
            DWORD sz = 0;
            BYTE *d = port_read_file(src, &sz);
            if (!d) { failed++; continue; }
            if (want != 100) scale_wav(d, sz, want);
            if (port_write_file(dst, d, sz)) done++; else failed++;
            port_free(d);
        } while (FindNextFileA(f, &fd));
        FindClose(f);
    }
    if (!failed) {
        char num[16];
        wsprintfA(num, "%d", want);
        port_write_file(marker, num, (DWORD)lstrlenA(num));
    }
    plog("se: volume %d%% (%d file(s)%s)", want, done, failed ? ", some FAILED" : "");
}

/* ---- music volume thread ------------------------------------------------- */

typedef void (__cdecl *PFN_SETVOL)(float);

static LONG volume_db(int pct) {
    if (pct <= 0) return DSBVOLUME_MIN;
    if (pct >= 100) return 0;
    LONG db = (LONG)(2000.0 * log10(pct / 100.0));    /* hundredths of a dB */
    return db < DSBVOLUME_MIN ? DSBVOLUME_MIN : db;
}

static int key_is(const char *k, int len, const char *name) {
    return len == lstrlenA(name) &&
           CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, k, len, name, len) == CSTR_EQUAL;
}

/* Reads [Sound] Music / MusicVolume straight from the file each time, so a
 * change made in the settings window is seen even if INI reads are cached.
 * Returns -1 while the file can't be read (e.g. while it is being saved). */
static int bgm_volume_file(void) {
    DWORD n = 0;
    char *s = (char *)port_read_file(g_ini, &n);
    if (!s) return -1;
    int insec = 0, music = 1, vol = 100;
    for (char *line = s; *line; ) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char save = *end;
        *end = 0;
        char *q = line;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '[') {
            insec = starts_with_i(q, "[Sound]");
        } else if (insec) {
            char *eq = q;
            while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                char *k = eq;
                while (k > q && (k[-1] == ' ' || k[-1] == '\t')) k--;
                int klen = (int)(k - q);
                const char *v = eq + 1;
                while (*v == ' ') v++;
                int val = 0, any = 0;
                for (; *v >= '0' && *v <= '9'; v++) { val = val * 10 + (*v - '0'); any = 1; if (val > 1000) break; }
                if (any && key_is(q, klen, "Music")) music = val != 0;
                if (any && key_is(q, klen, "MusicVolume")) vol = val > 100 ? 100 : val;
            }
        }
        *end = save;
        line = *end ? end + 1 : end;
    }
    port_free(s);
    return music ? vol : 0;
}

static DWORD WINAPI volume_thread(LPVOID unused) {
    (void)unused;
    PFN_SETVOL setvol = (PFN_SETVOL)px_export("pxtone_Tune_SetVolume");
    int last = -1, on_buffers = 0;
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        int vol = port_exists(g_ini) ? bgm_volume_file() : 100;
        if (vol < 0) vol = last < 0 ? 100 : last;      /* unreadable for now: keep the volume */
        int fresh = InterlockedExchange(&g_buf_new, 0);
        LONG nb = g_nbuf < MAX_BUF ? g_nbuf : MAX_BUF;
        if (vol != last || fresh) {
            if (nb > 0) {
                LONG db = volume_db(vol);
                for (LONG i = 0; i < nb; i++) {
                    if (!g_bufs[i]) continue;
                    HRESULT hr = g_bufs[i]->lpVtbl->SetVolume(g_bufs[i], db);
                    LONG got = 1;
                    g_bufs[i]->lpVtbl->GetVolume(g_bufs[i], &got);
                    plog("bgm: buffer %ld volume %ld/100 dB (hr=0x%08lx, now %ld)", i + 1, db, hr, got);
                }
                if (!on_buffers && setvol) setvol(1.0f);      /* don't attenuate twice */
                on_buffers = 1;
            } else if (setvol) {
                setvol((float)vol / 100.0f);
            }
            if (vol != last) plog("bgm: volume %d%% (%s)", vol, nb > 0 ? "DirectSound buffer" : "pxtone master");
            last = vol;
        }
        Sleep(250);
    }
    return 0;
}

void start_volume_thread(void) {
    HANDLE t = CreateThread(NULL, 0, volume_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else plog("bgm: could not start the volume thread");
}
