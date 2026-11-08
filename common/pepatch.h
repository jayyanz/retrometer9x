#ifndef PEPATCH_H
#define PEPATCH_H
#include <windows.h>

enum { PE_NOT_PE = -1, PE_NO_DDRAW = 0, PE_DDRAW = 1, PE_FPSDD = 2 };

/* What the exe imports: PE_DDRAW (unpatched), PE_FPSDD (patched), PE_NO_DDRAW, PE_NOT_PE. */
int pe_ddraw_state(const char *path);

/* to_fps=1: DDRAW.DLL -> FPSDD.DLL (backup saved once as <exe>.rmbak);
 * to_fps=0: back again. Returns 1 on success, else 0 with a message in err (>=256 chars). */
int pe_ddraw_patch(const char *path, int to_fps, char *err);

/* Bit i set if the exe imports names[i] (case-insensitive). */
unsigned pe_import_mask(const char *path, const char *const *names, int n);

#endif
