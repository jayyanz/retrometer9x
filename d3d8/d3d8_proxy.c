/* d3d8.dll proxy: hooks IDirect3DDevice8::Present and draws the counter with
 * a few untextured pre-transformed triangles. Game state is saved and
 * restored with a state block. */
#define COBJMACROS
#define CINTERFACE
#include "../common/fpscore.h"
#include <d3d8.h>
#include <stddef.h>

#define SLOT(vtbl_type, member) ((int)(offsetof(vtbl_type, member) / sizeof(void *)))

typedef IDirect3D8 *(WINAPI *PFCREATE)(UINT);
typedef HRESULT (WINAPI *PFCREATEDEV)(IDirect3D8 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **);
typedef HRESULT (WINAPI *PFPRESENT)(IDirect3DDevice8 *, CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *);
typedef HRESULT (WINAPI *PFRESET)(IDirect3DDevice8 *, D3DPRESENT_PARAMETERS *);

static PFCREATE real_Direct3DCreate8;
static IDirect3DDevice8 *s_dev;
static DWORD s_sb;               /* state block handle, 0 = none */

typedef struct { float x, y, z, rhw; DWORD c; } Vtx;
#define VTX_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static void drop_stateblock(IDirect3DDevice8 *d)
{
    if (s_sb && s_dev == d) IDirect3DDevice8_DeleteStateBlock(d, s_sb);
    s_sb = 0;
}

static void draw_overlay(IDirect3DDevice8 *d)
{
    FpsRect r[FPS_MAX_RECTS];
    Vtx v[FPS_MAX_RECTS * 6];
    IDirect3DSurface8 *bb = NULL, *oldrt = NULL, *oldz = NULL;
    D3DSURFACE_DESC desc;
    int n, i, k = 0, sw = 640, sh = 480;

    if (FAILED(IDirect3DDevice8_TestCooperativeLevel(d))) return;

    if (s_dev != d) { s_sb = 0; s_dev = d; }   /* new device: old handle belongs to the old one */
    if (!s_sb) {
        if (FAILED(IDirect3DDevice8_CreateStateBlock(d, D3DSBT_ALL, &s_sb))) { s_sb = 0; return; }
    } else {
        IDirect3DDevice8_CaptureStateBlock(d, s_sb);
    }

    IDirect3DDevice8_GetRenderTarget(d, &oldrt);
    IDirect3DDevice8_GetDepthStencilSurface(d, &oldz);   /* may legitimately fail (no Z) */
    if (SUCCEEDED(IDirect3DDevice8_GetBackBuffer(d, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
        if (bb != oldrt) IDirect3DDevice8_SetRenderTarget(d, bb, NULL);
        if (SUCCEEDED(IDirect3DSurface8_GetDesc(bb, &desc))) { sw = (int)desc.Width; sh = (int)desc.Height; }
    }

    if (SUCCEEDED(IDirect3DDevice8_BeginScene(d))) {
        IDirect3DDevice8_SetVertexShader(d, VTX_FVF);
        IDirect3DDevice8_SetPixelShader(d, 0);
        IDirect3DDevice8_SetTexture(d, 0, NULL);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        IDirect3DDevice8_SetTextureStageState(d, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ZENABLE, D3DZB_FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ZWRITEENABLE, FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHABLENDENABLE, FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_ALPHATESTENABLE, FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_LIGHTING, FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_CULLMODE, D3DCULL_NONE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_FOGENABLE, FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_STENCILENABLE, FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_FILLMODE, D3DFILL_SOLID);
        IDirect3DDevice8_SetRenderState(d, D3DRS_SPECULARENABLE, FALSE);
        IDirect3DDevice8_SetRenderState(d, D3DRS_COLORWRITEENABLE, 0xF);

        n = fps_rects(r, FPS_MAX_RECTS, sw, sh, 0, 0);
        for (i = 0; i < n; i++) {
            float x0 = (float)r[i].x - 0.5f, y0 = (float)r[i].y - 0.5f;
            float x1 = x0 + (float)r[i].w, y1 = y0 + (float)r[i].h;
            DWORD c = 0xFF000000UL | r[i].rgb;
            Vtx q[6] = {
                {x0, y0, 0, 1, c}, {x1, y0, 0, 1, c}, {x0, y1, 0, 1, c},
                {x1, y0, 0, 1, c}, {x1, y1, 0, 1, c}, {x0, y1, 0, 1, c},
            };
            memcpy(&v[k], q, sizeof q);
            k += 6;
        }
        if (k) IDirect3DDevice8_DrawPrimitiveUP(d, D3DPT_TRIANGLELIST, k / 3, v, sizeof(Vtx));
        IDirect3DDevice8_EndScene(d);
    }

    if (bb && bb != oldrt && oldrt) IDirect3DDevice8_SetRenderTarget(d, oldrt, oldz);
    IDirect3DDevice8_ApplyStateBlock(d, s_sb);     /* restores viewport and all render state */
    if (bb) IDirect3DSurface8_Release(bb);
    if (oldrt) IDirect3DSurface8_Release(oldrt);
    if (oldz) IDirect3DSurface8_Release(oldz);
}

static HRESULT WINAPI hook_Present(IDirect3DDevice8 *d, CONST RECT *src, CONST RECT *dst, HWND wnd, CONST RGNDATA *dirty)
{
    PFPRESENT orig = (PFPRESENT)fps_vtbl_orig(d, SLOT(IDirect3DDevice8Vtbl, Present));
    if (fps_frame()) draw_overlay(d);
    fps_limit();
    return orig(d, src, dst, wnd, dirty);
}

static HRESULT WINAPI hook_Reset(IDirect3DDevice8 *d, D3DPRESENT_PARAMETERS *pp)
{
    PFRESET orig = (PFRESET)fps_vtbl_orig(d, SLOT(IDirect3DDevice8Vtbl, Reset));
    drop_stateblock(d);
    return orig(d, pp);
}

static HRESULT WINAPI hook_CreateDevice(IDirect3D8 *self, UINT adapter, D3DDEVTYPE type, HWND wnd,
                                        DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    PFCREATEDEV orig = (PFCREATEDEV)fps_vtbl_orig(self, SLOT(IDirect3D8Vtbl, CreateDevice));
    HRESULT hr = orig(self, adapter, type, wnd, flags, pp, out);
    if (SUCCEEDED(hr) && out && *out) {
        void **vt = *(void ***)*out;
        fps_patch_vtbl(vt, SLOT(IDirect3DDevice8Vtbl, Present), (void *)hook_Present);
        fps_patch_vtbl(vt, SLOT(IDirect3DDevice8Vtbl, Reset), (void *)hook_Reset);
        s_dev = NULL; s_sb = 0;
        fps_log("device created %08lx", (DWORD)*out);
    }
    return hr;
}

static IDirect3D8 *WINAPI hook_Direct3DCreate8(UINT sdk)
{
    IDirect3D8 *d3d = real_Direct3DCreate8 ? real_Direct3DCreate8(sdk) : NULL;
    if (d3d) fps_patch_vtbl(*(void ***)d3d, SLOT(IDirect3D8Vtbl, CreateDevice), (void *)hook_CreateDevice);
    return d3d;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE real;
        DisableThreadLibraryCalls(inst);
        fps_init(inst, "d3d8");
        real = fps_load_real("d3d8.dll", "d3d8real.dll");
        if (!real) {
            fps_log("could not load the real d3d8.dll; failing quietly");
            return FALSE;
        }
        fps_resolve_exports(real);
        real_Direct3DCreate8 = (PFCREATE)fps_hook_export("Direct3DCreate8", (void *)hook_Direct3DCreate8);
    }
    return TRUE;
}
