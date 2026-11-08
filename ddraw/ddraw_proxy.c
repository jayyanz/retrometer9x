/* fpsdd.dll: DirectDraw / Direct3D 3-7 counter.
 *
 * Windows 9x cannot load a second ddraw.dll under the same name, and ddraw.dll
 * is tied to ddraw16.dll and the display driver by name, so this one can't be
 * a drop-in "ddraw.dll". Instead rmpatch.exe points the game's import of
 * DDRAW.DLL at FPSDD.DLL (same length), and this DLL forwards everything to
 * the real system ddraw.dll.
 *
 * Fullscreen games: counter is drawn on the back buffer just before Flip.
 * Windowed games (no Flip seen): drawn on the source surface of a Blt to the
 * primary surface. Drawing uses the surface's GDI DC, so it works in any
 * pixel format including 8-bit palettised modes. */
#define CINTERFACE
#include "../common/fpscore.h"
#include <ddraw.h>
#include <stddef.h>

#define SLOT(vtbl_type, member) ((int)(offsetof(vtbl_type, member) / sizeof(void *)))

/* Slots are identical in every IDirectDraw / IDirectDrawSurface version (new
 * methods were only appended), checked at compile time below. */
#define DD_QI        SLOT(IDirectDrawVtbl, QueryInterface)
#define DD_CREATESURF SLOT(IDirectDrawVtbl, CreateSurface)
#define S_QI         SLOT(IDirectDrawSurfaceVtbl, QueryInterface)
#define S_RELEASE    SLOT(IDirectDrawSurfaceVtbl, Release)
#define S_BLT        SLOT(IDirectDrawSurfaceVtbl, Blt)
#define S_FLIP       SLOT(IDirectDrawSurfaceVtbl, Flip)
#define S_GETATTACHED SLOT(IDirectDrawSurfaceVtbl, GetAttachedSurface)
#define S_GETCAPS    SLOT(IDirectDrawSurfaceVtbl, GetCaps)
#define S_GETDC      SLOT(IDirectDrawSurfaceVtbl, GetDC)
#define S_RELEASEDC  SLOT(IDirectDrawSurfaceVtbl, ReleaseDC)

typedef char chk1[(SLOT(IDirectDraw7Vtbl, CreateSurface) == DD_CREATESURF &&
                   SLOT(IDirectDraw4Vtbl, CreateSurface) == DD_CREATESURF &&
                   SLOT(IDirectDraw2Vtbl, CreateSurface) == DD_CREATESURF) ? 1 : -1];
typedef char chk2[(SLOT(IDirectDrawSurface7Vtbl, Flip) == S_FLIP &&
                   SLOT(IDirectDrawSurface7Vtbl, Blt) == S_BLT &&
                   SLOT(IDirectDrawSurface7Vtbl, GetDC) == S_GETDC &&
                   SLOT(IDirectDrawSurface7Vtbl, ReleaseDC) == S_RELEASEDC &&
                   SLOT(IDirectDrawSurface7Vtbl, GetCaps) == S_GETCAPS &&
                   SLOT(IDirectDrawSurface7Vtbl, GetAttachedSurface) == S_GETATTACHED &&
                   SLOT(IDirectDrawSurface3Vtbl, Flip) == S_FLIP) ? 1 : -1];

/* Generic signatures that are the same for every interface version. The caps
 * argument is DDSCAPS (4 bytes) or DDSCAPS2 (16 bytes); we always pass 16. */
typedef HRESULT (WINAPI *PFQI)(void *, REFIID, void **);
typedef ULONG   (WINAPI *PFREL)(void *);
typedef HRESULT (WINAPI *PFCREATESURF)(void *, void *desc, void **out, IUnknown *outer);
typedef HRESULT (WINAPI *PFFLIP)(void *, void *target, DWORD flags);
typedef HRESULT (WINAPI *PFBLT)(void *, RECT *dst, void *src, RECT *srcrc, DWORD flags, void *fx);
typedef HRESULT (WINAPI *PFGETATT)(void *, DWORD *caps, void **out);
typedef HRESULT (WINAPI *PFGETCAPS)(void *, DWORD *caps);
typedef HRESULT (WINAPI *PFGETDC)(void *, HDC *);
typedef HRESULT (WINAPI *PFRELDC)(void *, HDC);
typedef HRESULT (WINAPI *PFDDCREATE)(GUID *, void **, IUnknown *);
typedef HRESULT (WINAPI *PFDDCREATEEX)(GUID *, void **, REFIID, IUnknown *);

static PFDDCREATE   real_DirectDrawCreate;
static PFDDCREATEEX real_DirectDrawCreateEx;
static DWORD        s_lastflip;

static void *vfn(void *obj, int idx) { return (*(void ***)obj)[idx]; }

/* GUID compare without a CRT */
static int iid_eq(const void *a, const void *b)
{
    const unsigned char *p = a, *q = b; int i;
    for (i = 0; i < 16; i++) if (p[i] != q[i]) return 0;
    return 1;
}

static const GUID g_ddiids[] = {
    {0x6C14DB80,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}}, /* IDirectDraw  */
    {0xB3A6F3E0,0x2B43,0x11CF,{0xA2,0xDE,0x00,0xAA,0x00,0xB9,0x33,0x56}}, /* IDirectDraw2 */
    {0x9C59509A,0x39BD,0x11D1,{0x8C,0x4A,0x00,0xC0,0x4F,0xD9,0x30,0xC5}}, /* IDirectDraw4 */
    {0x15E65EC0,0x3B9C,0x11D2,{0xB9,0x2F,0x00,0x60,0x97,0x97,0xEA,0x5B}}, /* IDirectDraw7 */
};
static const GUID g_surfiids[] = {
    {0x6C14DB81,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}}, /* IDirectDrawSurface  */
    {0x57805885,0x6EEC,0x11CF,{0x94,0x41,0xA8,0x23,0x03,0xC1,0x0E,0x27}}, /* IDirectDrawSurface2 */
    {0xDA044E00,0x69B2,0x11D0,{0xA1,0xD5,0x00,0xAA,0x00,0xB8,0xDF,0xBB}}, /* IDirectDrawSurface3 */
    {0x0B2B8630,0xAD35,0x11D0,{0x8E,0xA6,0x00,0x60,0x97,0x97,0xEA,0x5B}}, /* IDirectDrawSurface4 */
    {0x06675A80,0x3B9B,0x11D2,{0xB9,0x2F,0x00,0x60,0x97,0x97,0xEA,0x5B}}, /* IDirectDrawSurface7 */
};

static int in_list(REFIID riid, const GUID *list, int n)
{
    int i;
    for (i = 0; i < n; i++) if (iid_eq(riid, &list[i])) return 1;
    return 0;
}

static void hook_surface(void *s);
static void hook_ddraw(void *dd);

/* ---------- drawing ---------- */
/* sw/sh: size of the visible area; 0 = whole surface */
static void draw_on(void *surf, int ox, int oy, int sw, int sh)
{
    FpsRect r[FPS_MAX_RECTS];
    HDC dc;
    RECT box;
    int n, i;
    if (FAILED(((PFGETDC)vfn(surf, S_GETDC))(surf, &dc))) return;
    if (sw <= 0 || sh <= 0) {
        if (GetClipBox(dc, &box) != ERROR && box.right > 0) { sw = box.right; sh = box.bottom; }
        else { sw = 640; sh = 480; }
    }
    n = fps_rects(r, FPS_MAX_RECTS, sw, sh, ox, oy);
    for (i = 0; i < n; i++) {
        DWORD c = r[i].rgb;
        HBRUSH b = CreateSolidBrush(RGB((c >> 16) & 255, (c >> 8) & 255, c & 255));
        RECT rc;
        rc.left = r[i].x; rc.top = r[i].y; rc.right = r[i].x + r[i].w; rc.bottom = r[i].y + r[i].h;
        if (b) { FillRect(dc, &rc, b); DeleteObject(b); }
    }
    ((PFRELDC)vfn(surf, S_RELEASEDC))(surf, dc);
}

/* ---------- surface hooks ---------- */
static HRESULT WINAPI hk_Flip(void *self, void *target, DWORD flags)
{
    PFFLIP orig = (PFFLIP)fps_vtbl_orig(self, S_FLIP);
    s_lastflip = GetTickCount();
    if (fps_frame()) {
        void *back = NULL;
        if (target) {
            draw_on(target, 0, 0, 0, 0);
        } else {
            DWORD caps[4] = { DDSCAPS_BACKBUFFER, 0, 0, 0 };
            if (SUCCEEDED(((PFGETATT)vfn(self, S_GETATTACHED))(self, caps, &back)) && back) {
                draw_on(back, 0, 0, 0, 0);
                ((PFREL)vfn(back, S_RELEASE))(back);
            }
        }
    }
    fps_limit();    /* only on real page flips; windowed Blt games are never capped */
    return orig(self, target, flags);
}

static HRESULT WINAPI hk_Blt(void *self, RECT *dst, void *src, RECT *srcrc, DWORD flags, void *fx)
{
    PFBLT orig = (PFBLT)fps_vtbl_orig(self, S_BLT);
    if (src && GetTickCount() - s_lastflip > 2000) {
        DWORD caps[4] = { 0, 0, 0, 0 };
        if (SUCCEEDED(((PFGETCAPS)vfn(self, S_GETCAPS))(self, caps)) && (caps[0] & DDSCAPS_PRIMARYSURFACE)) {
            if (fps_frame()) {
                if (srcrc) draw_on(src, srcrc->left, srcrc->top, srcrc->right - srcrc->left, srcrc->bottom - srcrc->top);
                else draw_on(src, 0, 0, 0, 0);
            }
        }
    }
    return orig(self, dst, src, srcrc, flags, fx);
}

static HRESULT WINAPI hk_SurfQI(void *self, REFIID riid, void **out)
{
    PFQI orig = (PFQI)fps_vtbl_orig(self, S_QI);
    HRESULT hr = orig(self, riid, out);
    if (SUCCEEDED(hr) && out && *out && in_list(riid, g_surfiids, 5)) hook_surface(*out);
    return hr;
}

static void hook_surface(void *s)
{
    void **vt = *(void ***)s;
    fps_patch_vtbl(vt, S_QI, (void *)hk_SurfQI);
    fps_patch_vtbl(vt, S_FLIP, (void *)hk_Flip);
    fps_patch_vtbl(vt, S_BLT, (void *)hk_Blt);
}

/* ---------- DirectDraw object hooks ---------- */
static HRESULT WINAPI hk_CreateSurface(void *self, void *desc, void **out, IUnknown *outer)
{
    PFCREATESURF orig = (PFCREATESURF)fps_vtbl_orig(self, DD_CREATESURF);
    HRESULT hr = orig(self, desc, out, outer);
    if (SUCCEEDED(hr) && out && *out) hook_surface(*out);
    return hr;
}

static HRESULT WINAPI hk_DDQI(void *self, REFIID riid, void **out)
{
    PFQI orig = (PFQI)fps_vtbl_orig(self, DD_QI);
    HRESULT hr = orig(self, riid, out);
    if (SUCCEEDED(hr) && out && *out && in_list(riid, g_ddiids, 4)) hook_ddraw(*out);
    return hr;
}

static void hook_ddraw(void *dd)
{
    void **vt = *(void ***)dd;
    fps_patch_vtbl(vt, DD_QI, (void *)hk_DDQI);
    fps_patch_vtbl(vt, DD_CREATESURF, (void *)hk_CreateSurface);
}

static HRESULT WINAPI hk_DirectDrawCreate(GUID *g, void **out, IUnknown *outer)
{
    HRESULT hr = real_DirectDrawCreate(g, out, outer);
    if (SUCCEEDED(hr) && out && *out) hook_ddraw(*out);
    return hr;
}

static HRESULT WINAPI hk_DirectDrawCreateEx(GUID *g, void **out, REFIID riid, IUnknown *outer)
{
    HRESULT hr = real_DirectDrawCreateEx(g, out, riid, outer);
    if (SUCCEEDED(hr) && out && *out) hook_ddraw(*out);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE real;
        DisableThreadLibraryCalls(inst);
        fps_init(inst, "ddraw");
        real = fps_load_real("ddraw.dll", NULL);
        if (!real) {
            fps_log("could not load ddraw.dll");
            return FALSE;
        }
        fps_resolve_exports(real);
        /* Only hook what the runtime has (DirectDrawCreateEx needs DirectX 7). */
        if (fps_real_export("DirectDrawCreate"))
            real_DirectDrawCreate = (PFDDCREATE)fps_hook_export("DirectDrawCreate", (void *)hk_DirectDrawCreate);
        if (fps_real_export("DirectDrawCreateEx"))
            real_DirectDrawCreateEx = (PFDDCREATEEX)fps_hook_export("DirectDrawCreateEx", (void *)hk_DirectDrawCreateEx);
        else
            fps_log("no DirectDrawCreateEx (pre-DX7 runtime)");
    }
    return TRUE;
}
