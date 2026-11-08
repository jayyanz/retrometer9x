/* opengl32.dll proxy: draws the counter just before wglSwapBuffers.
 * Works with the system OpenGL (ICD) and with MiniGL drivers (rename the
 * game's own opengl32.dll to glreal.dll). Uses only GL 1.1 immediate mode. */
#include "../common/fpscore.h"

#define GL_VIEWPORT         0x0BA2
#define GL_MATRIX_MODE      0x0BA0
#define GL_MODELVIEW        0x1700
#define GL_PROJECTION       0x1701
#define GL_TEXTURE          0x1702
#define GL_QUADS            0x0007
#define GL_ALL_ATTRIB_BITS  0x000FFFFF
#define GL_TEXTURE_1D       0x0DE0
#define GL_TEXTURE_2D       0x0DE1
#define GL_DEPTH_TEST       0x0B71
#define GL_BLEND            0x0BE2
#define GL_ALPHA_TEST       0x0BC0
#define GL_CULL_FACE        0x0B44
#define GL_FOG              0x0B60
#define GL_LIGHTING         0x0B50
#define GL_SCISSOR_TEST     0x0C11
#define GL_STENCIL_TEST     0x0B90
#define GL_ACTIVE_TEXTURE_ARB 0x84E0
#define GL_TEXTURE0_ARB     0x84C0

typedef void (WINAPI *PFV_E)(unsigned);
typedef void (WINAPI *PFV_V)(void);
typedef unsigned char (WINAPI *PFB_E)(unsigned);
typedef void (WINAPI *PFV_EIP)(unsigned, int *);
typedef void (WINAPI *PFV_4I)(int, int, int, int);
typedef void (WINAPI *PFV_6D)(double, double, double, double, double, double);
typedef void (WINAPI *PFV_3UB)(unsigned char, unsigned char, unsigned char);
typedef void (WINAPI *PFV_2I)(int, int);
typedef BOOL (WINAPI *PFSWAP)(HDC);
typedef BOOL (WINAPI *PFSWAPL)(HDC, UINT);
typedef PROC (WINAPI *PFGPA)(LPCSTR);

static struct {
    PFV_E   PushAttrib, MatrixMode, Disable, Enable, Begin;
    PFV_V   PopAttrib, PushMatrix, PopMatrix, LoadIdentity, End;
    PFB_E   IsEnabled;
    PFV_EIP GetIntegerv;
    PFV_4I  Viewport;
    PFV_6D  Ortho;
    PFV_3UB Color3ub;
    PFV_2I  Vertex2i;
    PFGPA   wglGetProcAddress;
} gl;

static PFSWAP  real_SwapBuffers;
static PFSWAPL real_SwapLayerBuffers;

static const unsigned s_caps[] = {
    GL_TEXTURE_1D, GL_TEXTURE_2D, GL_DEPTH_TEST, GL_BLEND, GL_ALPHA_TEST, GL_CULL_FACE,
    GL_FOG, GL_LIGHTING, GL_SCISSOR_TEST, GL_STENCIL_TEST,
};
#define NCAPS (sizeof s_caps / sizeof s_caps[0])

static void draw_overlay(HDC hdc)
{
    FpsRect r[FPS_MAX_RECTS];
    int n, i, w = 0, h = 0, vp[4] = {0, 0, 0, 0}, mm = GL_MODELVIEW;
    unsigned char was[NCAPS];
    int pushed = 0, active = 0;
    HWND hwnd;
    RECT rc;
    PFV_E ActiveTexture = NULL;

    if (!gl.Begin || !gl.End || !gl.Vertex2i || !gl.Color3ub || !gl.MatrixMode ||
        !gl.LoadIdentity || !gl.Ortho || !gl.Disable)
        return;

    if (gl.GetIntegerv) gl.GetIntegerv(GL_VIEWPORT, vp);
    hwnd = WindowFromDC(hdc);
    if (hwnd && GetClientRect(hwnd, &rc)) { w = rc.right; h = rc.bottom; }
    if (w <= 0 || h <= 0) { w = vp[2]; h = vp[3]; }
    if (w <= 0 || h <= 0) { w = 640; h = 480; }

    if (gl.PushAttrib && gl.PopAttrib) {
        gl.PushAttrib(GL_ALL_ATTRIB_BITS);
        pushed = 1;
    } else {
        if (gl.GetIntegerv) gl.GetIntegerv(GL_MATRIX_MODE, &mm);
        for (i = 0; i < (int)NCAPS; i++) was[i] = gl.IsEnabled ? gl.IsEnabled(s_caps[i]) : 0;
    }

    /* Multitexture games can leave texturing on in units 1-3; switch those off
     * too (restored by PopAttrib). */
    if (pushed && gl.wglGetProcAddress && gl.GetIntegerv) {
        ActiveTexture = (PFV_E)gl.wglGetProcAddress("glActiveTextureARB");
        if (ActiveTexture) {
            gl.GetIntegerv(GL_ACTIVE_TEXTURE_ARB, &active);
            for (i = 3; i >= 1; i--) { ActiveTexture(GL_TEXTURE0_ARB + i); gl.Disable(GL_TEXTURE_2D); }
            ActiveTexture(GL_TEXTURE0_ARB);
        }
    }

    for (i = 0; i < (int)NCAPS; i++) gl.Disable(s_caps[i]);
    if (gl.Viewport) gl.Viewport(0, 0, w, h);

    gl.MatrixMode(GL_PROJECTION);
    if (gl.PushMatrix) gl.PushMatrix();
    gl.LoadIdentity();
    gl.Ortho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    gl.MatrixMode(GL_MODELVIEW);
    if (gl.PushMatrix) gl.PushMatrix();
    gl.LoadIdentity();

    n = fps_rects(r, FPS_MAX_RECTS, w, h, 0, 0);
    gl.Begin(GL_QUADS);
    for (i = 0; i < n; i++) {
        gl.Color3ub((unsigned char)(r[i].rgb >> 16), (unsigned char)(r[i].rgb >> 8), (unsigned char)r[i].rgb);
        gl.Vertex2i(r[i].x, r[i].y);
        gl.Vertex2i(r[i].x + r[i].w, r[i].y);
        gl.Vertex2i(r[i].x + r[i].w, r[i].y + r[i].h);
        gl.Vertex2i(r[i].x, r[i].y + r[i].h);
    }
    gl.End();

    if (gl.PopMatrix) gl.PopMatrix();       /* modelview */
    gl.MatrixMode(GL_PROJECTION);
    if (gl.PopMatrix) gl.PopMatrix();

    if (pushed) {
        if (ActiveTexture) ActiveTexture((unsigned)active);
        gl.PopAttrib();                     /* restores enables, viewport, matrix mode, colour */
    } else {
        gl.MatrixMode((unsigned)mm);
        if (gl.Viewport && vp[2] > 0) gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
        if (gl.Enable)
            for (i = 0; i < (int)NCAPS; i++) if (was[i]) gl.Enable(s_caps[i]);
        gl.Color3ub(255, 255, 255);
    }
}

static BOOL WINAPI hook_wglSwapBuffers(HDC hdc)
{
    if (fps_frame()) draw_overlay(hdc);
    fps_limit();
    return real_SwapBuffers ? real_SwapBuffers(hdc) : FALSE;
}

static BOOL WINAPI hook_wglSwapLayerBuffers(HDC hdc, UINT planes)
{
    if (planes & 1 /* WGL_SWAP_MAIN_PLANE */) {
        if (fps_frame()) draw_overlay(hdc);
        fps_limit();
    }
    return real_SwapLayerBuffers ? real_SwapLayerBuffers(hdc, planes) : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE real;
        DisableThreadLibraryCalls(inst);
        fps_init(inst, "opengl32");
        real = fps_load_real("opengl32.dll", "glreal.dll");
        if (!real) {
            fps_log("could not load the real OpenGL driver; failing quietly");
            return FALSE;
        }
        fps_resolve_exports(real);
#define R(x, t) gl.x = (t)fps_real_export("gl" #x)
        R(PushAttrib, PFV_E); R(PopAttrib, PFV_V); R(MatrixMode, PFV_E); R(PushMatrix, PFV_V);
        R(PopMatrix, PFV_V); R(LoadIdentity, PFV_V); R(Ortho, PFV_6D); R(Disable, PFV_E);
        R(Enable, PFV_E); R(IsEnabled, PFB_E); R(GetIntegerv, PFV_EIP); R(Viewport, PFV_4I);
        R(Begin, PFV_E); R(End, PFV_V); R(Color3ub, PFV_3UB); R(Vertex2i, PFV_2I);
#undef R
        gl.wglGetProcAddress = (PFGPA)fps_real_export("wglGetProcAddress");
        real_SwapBuffers = (PFSWAP)fps_hook_export("wglSwapBuffers", (void *)hook_wglSwapBuffers);
        real_SwapLayerBuffers = (PFSWAPL)fps_hook_export("wglSwapLayerBuffers", (void *)hook_wglSwapLayerBuffers);
    }
    return TRUE;
}
