/* Retrometer 9x - tiny FPS counter proxy DLLs for Windows 9x
 * Shared core: settings, timing, hotkey, digit font, real-DLL loading, vtable patching.
 */
#ifndef FPSCORE_H
#define FPSCORE_H

#include <windows.h>

#include "fpssettings.h"

/* Call from DllMain(PROCESS_ATTACH). tag = ini section for this proxy, e.g. "opengl32". */
void  fps_init(HINSTANCE self, const char *tag);

/* Load the DLL we chain to.
 *   1. [tag] Real=<path> in retrometer.ini, if set
 *   2. <game dir>\<sysname with 'a' added>, e.g. glide2xa.dll (game's own driver, renamed)
 *   3. <game dir>\<localname>, copied from <windows\system>\<sysname> first if missing
 *      (Win9x will not load two DLLs with the same module name, so the real one
 *       must live under a different name)
 *   localname == NULL: load <system dir>\<sysname> directly (used by fpsdd.dll). */
HMODULE fps_load_real(const char *sysname, const char *localname);

/* Fill g_exp_ptrs from the real DLL. Returns number of exports found. */
int   fps_resolve_exports(HMODULE real);

/* Replace one export with a hook. Returns the real function, or NULL if the
 * real DLL doesn't have it. */
void *fps_hook_export(const char *name, void *hook);

/* Look up the real function for an export (after resolve). NULL if missing. */
void *fps_real_export(const char *name);

/* Call once per presented frame. Updates the counter and polls the hotkey.
 * Returns nonzero if the overlay should be drawn this frame. */
int   fps_frame(void);

/* Frame rate cap: call right before handing the frame to the real
 * present/flip/swap. Waits as needed; does nothing when the cap is off. */
void  fps_limit(void);

/* Build the overlay as solid rectangles for a sw x sh pixel screen/surface,
 * shifted by (ox, oy). Returns the rectangle count. */
int   fps_rects(FpsRect *out, int max, int sw, int sh, int ox, int oy);

/* Patch one slot of a COM vtable in place. The original is remembered per
 * vtable so one hook can serve several interface versions. Returns 1 if the
 * slot is (now) hooked. */
int   fps_patch_vtbl(void **vtbl, int idx, void *hook);

/* Original function for a patched slot, looked up by the object's vtable. */
void *fps_vtbl_orig(void *obj, int idx);

void  fps_log(const char *fmt, ...);

/* Freestanding helpers (no C runtime on purpose, so the DLLs run on plain Win95/98). */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);

#endif
