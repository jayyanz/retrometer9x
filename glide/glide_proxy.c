/* glide2x.dll / glide3x.dll proxy (built twice, -DGLIDE3 for the Glide 3 one).
 * Draws the counter into the back buffer through a linear frame buffer lock
 * just before grBufferSwap. Works on any Voodoo board or Glide wrapper that
 * supports LFB writes. */
#include "../common/fpscore.h"

typedef int FxBool;
typedef struct {
    int           size;
    void         *lfbPtr;
    unsigned long strideInBytes;
    int           writeMode;
    int           origin;
} GrLfbInfo_t;

#define GR_LFB_WRITE_ONLY       0x01
#define GR_BUFFER_BACKBUFFER    1
#define GR_LFBWRITEMODE_565     0x0
#define GR_LFBWRITEMODE_888     0x4
#define GR_LFBWRITEMODE_8888    0x5
#define GR_ORIGIN_UPPER_LEFT    0x0
#define GR_LFBWRITEMODE_ANY     0xFF

typedef void   (__stdcall *PFSWAP)(unsigned long interval);
typedef FxBool (__stdcall *PFLOCK)(int type, int buffer, int writeMode, int origin, FxBool pixelPipeline, GrLfbInfo_t *info);
typedef FxBool (__stdcall *PFUNLOCK)(int type, int buffer);
typedef unsigned long (__stdcall *PFWINOPEN)(unsigned long hwnd, int res, int refresh, int cfmt, int origin, int ncol, int naux);
typedef unsigned long (__stdcall *PFSCREENDIM)(void);

static PFSWAP   real_grBufferSwap;
static PFLOCK   real_grLfbLock;
static PFUNLOCK real_grLfbUnlock;
static PFWINOPEN real_grSstWinOpen;
static PFSCREENDIM real_grSstScreenWidth, real_grSstScreenHeight;
static int      s_warned;
static int      s_w = 640, s_h = 480;

/* GR_RESOLUTION_* -> pixels */
static const unsigned short s_res[][2] = {
    {320,200},{320,240},{400,256},{512,384},{640,200},{640,350},{640,400},{640,480},
    {800,600},{960,720},{856,480},{512,256},{1024,768},{1280,1024},{1600,1200},{400,300},
    {1152,864},{1280,960},{1600,1024},{1792,1344},{1856,1392},{1920,1440},{2048,1536},{2048,2048},
};

static unsigned long __stdcall hook_grSstWinOpen(unsigned long hwnd, int res, int refresh, int cfmt,
                                                 int origin, int ncol, int naux)
{
    unsigned long r = real_grSstWinOpen(hwnd, res, refresh, cfmt, origin, ncol, naux);
    if (res >= 0 && res < (int)(sizeof s_res / sizeof s_res[0])) { s_w = s_res[res][0]; s_h = s_res[res][1]; }
    else if (hwnd) {   /* GR_RESOLUTION_NONE: windowed, use the window size */
        RECT rc;
        if (GetClientRect((HWND)hwnd, &rc) && rc.right > 0) { s_w = rc.right; s_h = rc.bottom; }
    }
    fps_log("grSstWinOpen res=%d -> %dx%d", res, s_w, s_h);
    return r;
}

static void draw_overlay(void)
{
    FpsRect r[FPS_MAX_RECTS];
    GrLfbInfo_t info;
    int n, i, x, y, maxw;

    if (!real_grLfbLock || !real_grLfbUnlock) return;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (!real_grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565,
                        GR_ORIGIN_UPPER_LEFT, 0, &info) || !info.lfbPtr) {
        if (!s_warned++) fps_log("grLfbLock failed");
        return;
    }

    if (real_grSstScreenWidth && real_grSstScreenHeight) {   /* Glide 2 can tell us directly */
        int w = (int)real_grSstScreenWidth(), h = (int)real_grSstScreenHeight();
        if (w > 0 && h > 0) { s_w = w; s_h = h; }
    }
    n = fps_rects(r, FPS_MAX_RECTS, s_w, s_h, 0, 0);
    if (info.writeMode == GR_LFBWRITEMODE_8888 || info.writeMode == GR_LFBWRITEMODE_888) {
        maxw = (int)(info.strideInBytes / 4);
        for (i = 0; i < n; i++)
            for (y = r[i].y; y < r[i].y + r[i].h; y++) {
                unsigned long *row = (unsigned long *)((char *)info.lfbPtr + y * info.strideInBytes);
                for (x = r[i].x; x < r[i].x + r[i].w && x < maxw; x++) row[x] = 0xFF000000UL | r[i].rgb;
            }
    } else {
        maxw = (int)(info.strideInBytes / 2);
        for (i = 0; i < n; i++) {
            DWORD c = r[i].rgb;
            unsigned short px = (unsigned short)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            for (y = r[i].y; y < r[i].y + r[i].h; y++) {
                unsigned short *row = (unsigned short *)((char *)info.lfbPtr + y * info.strideInBytes);
                for (x = r[i].x; x < r[i].x + r[i].w && x < maxw; x++) row[x] = px;
            }
        }
    }
    real_grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
}

static void __stdcall hook_grBufferSwap(unsigned long interval)
{
    if (fps_frame()) draw_overlay();
    fps_limit();
    if (real_grBufferSwap) real_grBufferSwap(interval);
}

#ifdef GLIDE3
#  define TAG "glide3x"
#  define SYSNAME "glide3x.dll"
#  define LOCALNAME "g3real.dll"
#else
#  define TAG "glide2x"
#  define SYSNAME "glide2x.dll"
#  define LOCALNAME "g2real.dll"
#endif

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE real;
        DisableThreadLibraryCalls(inst);
        fps_init(inst, TAG);
        real = fps_load_real(SYSNAME, LOCALNAME);
        if (!real) {
            /* Fail quietly: games often probe for Glide to detect a Voodoo card. */
            fps_log("could not load the real " SYSNAME "; failing quietly");
            return FALSE;
        }
        fps_resolve_exports(real);
        real_grLfbLock   = (PFLOCK)fps_real_export("_grLfbLock@24");
        real_grLfbUnlock = (PFUNLOCK)fps_real_export("_grLfbUnlock@8");
#ifndef GLIDE3
        real_grSstScreenWidth  = (PFSCREENDIM)fps_real_export("_grSstScreenWidth@0");
        real_grSstScreenHeight = (PFSCREENDIM)fps_real_export("_grSstScreenHeight@0");
#endif
        if (fps_real_export("_grSstWinOpen@28"))
            real_grSstWinOpen = (PFWINOPEN)fps_hook_export("_grSstWinOpen@28", (void *)hook_grSstWinOpen);
        real_grBufferSwap = (PFSWAP)fps_hook_export("_grBufferSwap@4", (void *)hook_grBufferSwap);
        if (!real_grBufferSwap) fps_log("real DLL has no grBufferSwap");
    }
    return TRUE;
}
