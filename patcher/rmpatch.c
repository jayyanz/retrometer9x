/* rmpatch.exe - points a game's DDRAW.DLL import at FPSDD.DLL (or back again).
 * Drag the game's .exe onto rmpatch.exe, or run: rmpatch game.exe
 * (retrometer.exe can do the same from its window.) */
#include "../common/pepatch.h"
#include "../common/version.h"

static void msg(const char *text, UINT icon) { MessageBoxA(NULL, text, RM_NAME " patcher " RM_VERSION, MB_OK | icon); }

static char *get_arg(void)
{
    char *p = GetCommandLineA();
    static char buf[MAX_PATH];
    int n = 0;
    if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; }
    else while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    if (*p == '"') { p++; while (*p && *p != '"' && n < MAX_PATH - 1) buf[n++] = *p++; }
    else while (*p && n < MAX_PATH - 1) buf[n++] = *p++;
    while (n && buf[n - 1] == ' ') n--;
    buf[n] = 0;
    return buf;
}

static int run(void)
{
    char *path = get_arg(), err[300];
    int st;
    if (!path[0]) {
        msg("Drag the game's .exe onto rmpatch.exe.\n\n"
            "It makes the game load FPSDD.DLL (the DirectDraw FPS counter) instead of DDRAW.DLL. "
            "Run it again on the same exe to undo.", MB_ICONINFORMATION);
        return 0;
    }
    st = pe_ddraw_state(path);
    if (st == PE_NOT_PE) { msg("Not a 32-bit Windows program.", MB_ICONERROR); return 1; }
    if (!pe_ddraw_patch(path, st != PE_FPSDD, err)) { msg(err, MB_ICONWARNING); return 1; }
    if (st == PE_FPSDD) msg("Restored: the game uses DDRAW.DLL again.", MB_ICONINFORMATION);
    else msg("Patched: the game now loads FPSDD.DLL.\n\nMake sure fpsdd.dll is in the same folder as the exe.\n"
             "Run rmpatch on the exe again to undo (a backup is saved as .rmbak).", MB_ICONINFORMATION);
    return 0;
}

void WINAPI entry(void) { ExitProcess((UINT)run()); }
