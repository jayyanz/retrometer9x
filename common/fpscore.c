#include "fpscore.h"
#include <stdarg.h>

extern const int g_exp_count;
extern const char *const g_exp_names[];
extern void *g_exp_ptrs[];
extern void *const g_exp_fallback[];

/* Marker that retrometer.exe looks for to recognise these DLLs. */
const char fps_marker[] = "RETROMETER9X-PROXY-1";
#include "version.h"

/* ---------- settings ---------- */
static char  s_dir[MAX_PATH];        /* game dir (where this DLL lives), with trailing '\\' */
static char  s_ini[MAX_PATH];
static char  s_tag[32];
static FpsSettings S;
static FILETIME s_initime;
static DWORD s_lastcheck;

/* ---------- counter state ---------- */
static DWORD s_t0;
static int   s_frames;
static int   s_fps, s_ms10;
static int   s_keydown;
static int   s_visible;

/* ---------- frame cap state ---------- */
static int   s_cap;              /* current cap in fps, 0 = off */
static int   s_capkeydown;
static int   s_cycle_pos;        /* index into caplist in cycle mode, -1 = off */
static char  s_banner[24];
static DWORD s_banner_until;
static LARGE_INTEGER s_qpf, s_next;
static int   s_hires;            /* 1 = timeBeginPeriod(1) done */

void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }

static void ini_time(FILETIME *ft)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(s_ini, &fd);
    ft->dwLowDateTime = ft->dwHighDateTime = 0;
    if (h != INVALID_HANDLE_VALUE) { *ft = fd.ftLastWriteTime; FindClose(h); }
}

void fps_log(const char *fmt, ...)
{
    char buf[512], path[MAX_PATH];
    va_list ap;
    HANDLE h;
    DWORD n;
    int len;
    if (!S.log) return;
    lstrcpyA(buf, "[");
    lstrcatA(buf, s_tag);
    lstrcatA(buf, "] ");
    len = lstrlenA(buf);
    va_start(ap, fmt);
    len += wvsprintfA(buf + len, fmt, ap);
    va_end(ap);
    buf[len++] = '\r'; buf[len++] = '\n';
    lstrcpyA(path, s_dir);
    lstrcatA(path, "retrometer.log");
    h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, len, &n, NULL);
    CloseHandle(h);
}

void fps_init(HINSTANCE self, const char *tag)
{
    int i, last = 0;
    GetModuleFileNameA(self, s_dir, MAX_PATH);
    for (i = 0; s_dir[i]; i++) if (s_dir[i] == '\\' || s_dir[i] == '/') last = i + 1;
    s_dir[last] = 0;
    lstrcpyA(s_ini, s_dir);
    lstrcatA(s_ini, "retrometer.ini");
    lstrcpynA(s_tag, tag, sizeof s_tag);
    fps_settings_load(s_ini, &S);
    ini_time(&s_initime);
    s_visible = S.visible;
    s_cap = S.capon ? S.capfps : 0;
    s_cycle_pos = -1;
    if (!QueryPerformanceFrequency(&s_qpf)) s_qpf.QuadPart = 0;
    s_t0 = s_lastcheck = GetTickCount();
    fps_log("%s %s (%s) loaded from %s (hotkey vk=%d mods=%d)", RM_NAME, RM_VERSION, fps_marker, s_dir, S.vk, S.mods);
}

HMODULE fps_load_real(const char *sysname, const char *localname)
{
    char path[MAX_PATH], sys[MAX_PATH];
    HMODULE h;

    GetPrivateProfileStringA(s_tag, "Real", "", path, sizeof path, s_ini);
    if (path[0]) {
        h = LoadLibraryA(path);
        fps_log("Real=%s -> %08lx", path, (DWORD)h);
        return h;
    }

    GetSystemDirectoryA(sys, MAX_PATH);
    lstrcatA(sys, "\\");
    lstrcatA(sys, sysname);

    /* A game-supplied driver renamed with an 'a' (glide2x.dll -> glide2xa.dll)
     * takes priority over everything except Real=. */
    {
        char alt[MAX_PATH];
        int i, dot = -1;
        lstrcpyA(alt, s_dir);
        i = lstrlenA(alt);
        lstrcatA(alt, sysname);
        for (; alt[i]; i++) if (alt[i] == '.') dot = i;
        if (dot > 0) {
            alt[dot] = 0;
            lstrcatA(alt, "a.dll");
            if (GetFileAttributesA(alt) != 0xFFFFFFFF) {
                h = LoadLibraryA(alt);
                fps_log("game's own driver %s -> %08lx", alt, (DWORD)h);
                if (h) return h;
            }
        }
    }

    if (!localname) {
        /* fpsdd.dll: the game imports us under a different name, so a ddraw.dll
         * the game ships (a wrapper) can be loaded straight from its folder. */
        lstrcpyA(path, s_dir);
        lstrcatA(path, sysname);
        if (GetFileAttributesA(path) != 0xFFFFFFFF) {
            h = LoadLibraryA(path);
            fps_log("game's own driver %s -> %08lx", path, (DWORD)h);
            if (h) return h;
        }
        h = LoadLibraryA(sys);
        fps_log("chain %s -> %08lx", sys, (DWORD)h);
        return h;
    }

    lstrcpyA(path, s_dir);
    lstrcatA(path, localname);
    if (GetFileAttributesA(path) == 0xFFFFFFFF) {
        BOOL ok = CopyFileA(sys, path, TRUE);
        fps_log("copied %s -> %s: %s", sys, path, ok ? "ok" : "FAILED");
    }
    h = LoadLibraryA(path);
    fps_log("chain %s -> %08lx", path, (DWORD)h);
    return h;
}

int fps_resolve_exports(HMODULE real)
{
    int i, found = 0;
    for (i = 0; i < g_exp_count; i++) {
        void *p = real ? (void *)GetProcAddress(real, g_exp_names[i]) : NULL;
        if (p) found++;
        g_exp_ptrs[i] = p ? p : g_exp_fallback[i];
    }
    fps_log("resolved %d of %d exports", found, g_exp_count);
    return found;
}

static int find_export(const char *name)
{
    int i;
    for (i = 0; i < g_exp_count; i++)
        if (lstrcmpA(g_exp_names[i], name) == 0) return i;
    return -1;
}

void *fps_real_export(const char *name)
{
    int i = find_export(name);
    if (i < 0 || g_exp_ptrs[i] == g_exp_fallback[i]) return NULL;
    return g_exp_ptrs[i];
}

void *fps_hook_export(const char *name, void *hook)
{
    int i = find_export(name);
    void *orig;
    if (i < 0) return NULL;
    orig = g_exp_ptrs[i];
    g_exp_ptrs[i] = hook;
    return orig == g_exp_fallback[i] ? NULL : orig;
}

/* ---------- counter + hotkey ---------- */
static int key_held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

int fps_frame(void)
{
    DWORD now = GetTickCount(), dt;
    int down;

    /* Pick up changes saved by retrometer.exe while the game is running. */
    if (now - s_lastcheck >= 1000) {
        FILETIME ft;
        s_lastcheck = now;
        ini_time(&ft);
        if (CompareFileTime(&ft, &s_initime) != 0) {
            s_initime = ft;
            {
                int was_on = s_cap != 0, old_start = S.capon;
                fps_settings_load(s_ini, &S);
                /* follow a changed cap value; follow "start on" only if it was flipped */
                if (S.capon != old_start) s_cap = S.capon ? S.capfps : 0;
                else if (was_on && S.capmode == FPS_CAP_TOGGLE) s_cap = S.capfps;
                s_cycle_pos = -1;
            }
            fps_log("settings reloaded");
        }
    }

    down = key_held(S.vk) &&
           (!(S.mods & FPS_MOD_CTRL) || key_held(VK_CONTROL)) &&
           (!(S.mods & FPS_MOD_ALT) || key_held(VK_MENU)) &&
           (!(S.mods & FPS_MOD_SHIFT) || key_held(VK_SHIFT));
    if (down && !s_keydown) s_visible = !s_visible;
    s_keydown = down;

    if (S.capvk) {
        int cdown = key_held(S.capvk) &&
                    (!(S.capmods & FPS_MOD_CTRL) || key_held(VK_CONTROL)) &&
                    (!(S.capmods & FPS_MOD_ALT) || key_held(VK_MENU)) &&
                    (!(S.capmods & FPS_MOD_SHIFT) || key_held(VK_SHIFT));
        /* the same key can't be both toggles: overlay toggle wins */
        if (cdown && !s_capkeydown && !(S.capvk == S.vk && S.capmods == S.mods)) {
            if (S.capmode == FPS_CAP_CYCLE) {
                /* Off -> list[0] -> list[1] ... -> Off */
                if (s_cycle_pos < 0 && s_cap) {     /* first press while on: find where we are */
                    int i;
                    for (i = 0; i < S.capcount && S.caplist[i] != s_cap; i++) ;
                    s_cycle_pos = i < S.capcount ? i : -1;
                }
                s_cycle_pos++;
                if (s_cycle_pos >= S.capcount) s_cycle_pos = -1;
                s_cap = s_cycle_pos < 0 ? 0 : S.caplist[s_cycle_pos];
            } else {
                s_cap = s_cap ? 0 : S.capfps;
            }
            if (s_cap) wsprintfA(s_banner, "CAP %d", s_cap); else lstrcpyA(s_banner, "CAP OFF");
            s_banner_until = now + 2000;
            s_next.QuadPart = 0;
            fps_log("cap -> %d", s_cap);
        }
        s_capkeydown = cdown;
    }

    s_frames++;
    dt = now - s_t0;
    if (dt >= (DWORD)S.interval) {   /* GetTickCount is ~55 ms coarse on 9x, so average over a window */
        s_fps = (int)((s_frames * 1000 + dt / 2) / dt);
        s_ms10 = (int)((dt * 10 + s_frames / 2) / s_frames);
        s_frames = 0;
        s_t0 = now;
    }
    return s_visible || (int)(s_banner_until - now) > 0;
}

int fps_rects(FpsRect *out, int max, int sw, int sh, int ox, int oy)
{
    char text[64];
    int banner = (int)(s_banner_until - GetTickCount()) > 0;
    if (sw <= 0) sw = 640;
    if (sh <= 0) sh = 480;
    text[0] = 0;
    if (s_visible) fps_format(&S, s_fps, s_ms10, text);
    if (banner) {
        if (text[0]) lstrcatA(text, "  ");
        lstrcatA(text, s_banner);
    }
    return fps_layout(&S, text, sw, sh, ox, oy, out, max);
}

/* ---------- frame cap ---------- */
typedef UINT (WINAPI *PFTIMEPERIOD)(UINT);

static void hires_sleep_on(void)
{
    /* Make Sleep(1) last ~1 ms instead of a 55 ms tick. winmm is loaded on
     * demand so games that never cap don't pay for it. */
    HMODULE w;
    PFTIMEPERIOD begin;
    if (s_hires) return;
    s_hires = 1;
    w = LoadLibraryA("winmm.dll");
    begin = w ? (PFTIMEPERIOD)GetProcAddress(w, "timeBeginPeriod") : NULL;
    if (begin) begin(1);
}

void fps_limit(void)
{
    LARGE_INTEGER now;
    LONGLONG period;
    if (!s_cap || !s_qpf.QuadPart) { s_next.QuadPart = 0; return; }
    hires_sleep_on();
    period = s_qpf.QuadPart / s_cap;
    QueryPerformanceCounter(&now);
    if (!s_next.QuadPart || now.QuadPart - s_next.QuadPart > period * 4) {
        /* just switched on, or fell far behind (loading screen): restart the clock */
        s_next.QuadPart = now.QuadPart + period;
        return;
    }
    while (now.QuadPart < s_next.QuadPart) {
        LONGLONG left_ms = (s_next.QuadPart - now.QuadPart) * 1000 / s_qpf.QuadPart;
        if (left_ms > 2) Sleep((DWORD)(left_ms - 2));     /* sleep most of it ... */
        else Sleep(0);                                    /* ... then yield-spin the rest */
        QueryPerformanceCounter(&now);
    }
    /* schedule from the target, not from now, so the rate doesn't drift */
    s_next.QuadPart += period;
    if (now.QuadPart - s_next.QuadPart > period) s_next.QuadPart = now.QuadPart + period;
}

/* ---------- vtable patching ---------- */
typedef struct { void **vtbl; int idx; void *orig; } VtPatch;
static VtPatch s_patches[64];
static int s_npatches;

int fps_patch_vtbl(void **vtbl, int idx, void *hook)
{
    DWORD old;
    int i;
    if (!vtbl) return 0;
    for (i = 0; i < s_npatches; i++)
        if (s_patches[i].vtbl == vtbl && s_patches[i].idx == idx) return 1;
    if (vtbl[idx] == hook) return 1;
    /* On Win9x everything above 2 GB is the shared arena; writing there would
     * affect every process, so refuse. */
    if ((DWORD)vtbl >= 0x80000000UL) {
        fps_log("vtable %08lx is in shared memory, not patching", (DWORD)vtbl);
        return 0;
    }
    if (s_npatches >= (int)(sizeof s_patches / sizeof s_patches[0])) return 0;
    if (!VirtualProtect(&vtbl[idx], sizeof(void *), PAGE_READWRITE, &old)) {
        fps_log("VirtualProtect failed on vtable %08lx", (DWORD)vtbl);
        return 0;
    }
    s_patches[s_npatches].vtbl = vtbl;
    s_patches[s_npatches].idx = idx;
    s_patches[s_npatches].orig = vtbl[idx];
    s_npatches++;
    vtbl[idx] = hook;
    VirtualProtect(&vtbl[idx], sizeof(void *), old, &old);
    fps_log("patched vtable %08lx slot %d", (DWORD)vtbl, idx);
    return 1;
}

void *fps_vtbl_orig(void *obj, int idx)
{
    void **vtbl = *(void ***)obj;
    int i;
    for (i = 0; i < s_npatches; i++)
        if (s_patches[i].vtbl == vtbl && s_patches[i].idx == idx) return s_patches[i].orig;
    return NULL;
}
