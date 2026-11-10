/* retrometer.exe - FPS counter manager for the Retrometer 9x proxy DLLs.
 * Plain Win32 (USER/GDI/COMDLG/SHELL), no C runtime: runs on Windows 95/98/ME and later.
 *
 * Lives in its own folder with the DLLs. Keeps a list of games it has
 * installed to (games.ini), installs/removes overlays per game (renaming a
 * game's own driver DLL out of the way and back), and edits settings for
 * "All games" (allgames.ini) or per game. Each game's settings are always
 * written to retrometer.ini in the game folder, which is what the DLLs read. */
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include "resource.h"
#include "gamedb.h"
#include "../common/fpssettings.h"
#include "../common/pepatch.h"
#include "../common/version.h"

void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }

/* ---------- state ---------- */
static HINSTANCE g_inst;
static HWND      g_dlg;
static char      g_appdir[MAX_PATH];
static char      g_dbpath[MAX_PATH];     /* games.ini */
static char      g_globalini[MAX_PATH];  /* allgames.ini */
static GameDb    g_db;
static int       g_sel = -1;             /* -1 = All games, else game index */
static FpsSettings g_s;
static int       g_useGlobal;
static int       g_loading, g_dirty;
static HBRUSH    g_brText, g_brBg;
static COLORREF  g_custom[16];
static int       g_detecting;
static DWORD     g_detect_start;
static int       g_prev_w = 640, g_prev_h = 480;
static char      g_report[8192];

static const int g_settingIds[] = {
    IDC_KEY, IDC_DETECT, IDC_CTRL, IDC_ALT, IDC_SHIFT, IDC_VISIBLE, IDC_CORNER, IDC_X, IDC_Y, IDC_SCALE,
    IDC_COLOR, IDC_BG, IDC_BGCOLOR, IDC_LABEL, IDC_FRAMETIME, IDC_INTERVAL, IDC_LOG, IDC_DEFAULTS,
    IDC_CAPON, IDC_CAPFPS, IDC_CAPKEY, IDC_CAPDETECT, IDC_CAPCTRL, IDC_CAPALT, IDC_CAPSHIFT, IDC_CAPMODE, IDC_CAPLIST,
};
static int g_detect_target = IDC_KEY;   /* which hotkey "Detect" fills */

static int ask(const char *text) { return MessageBoxA(g_dlg, text, "Retrometer 9x", MB_YESNO | MB_ICONQUESTION) == IDYES; }
static void info(const char *text) { MessageBoxA(g_dlg, text, "Retrometer 9x", MB_OK | MB_ICONINFORMATION); }
static void add_slash(char *p) { int n = lstrlenA(p); if (n && p[n - 1] != '\\') { p[n] = '\\'; p[n + 1] = 0; } }
static const char *base_name(const char *p) { const char *b = p; for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1; return b; }
static void game_ini(const Game *g, char *out) { path_join(out, g->folder, "retrometer.ini"); }
static void save_db(void) { db_save(g_dbpath, &g_db); }

static void redraw_previews(void)
{
    InvalidateRect(GetDlgItem(g_dlg, IDC_PREVIEW), NULL, FALSE);
    InvalidateRect(GetDlgItem(g_dlg, IDC_ACTUAL), NULL, FALSE);
}

/* ---------- settings <-> controls ---------- */
static const int g_intervals[] = {250, 500, 1000, 2000};

static void set_brushes(void)
{
    if (g_brText) DeleteObject(g_brText);
    if (g_brBg) DeleteObject(g_brBg);
    g_brText = CreateSolidBrush(RGB((g_s.color >> 16) & 255, (g_s.color >> 8) & 255, g_s.color & 255));
    g_brBg = CreateSolidBrush(RGB((g_s.bgcolor >> 16) & 255, (g_s.bgcolor >> 8) & 255, g_s.bgcolor & 255));
    InvalidateRect(GetDlgItem(g_dlg, IDC_SWATCH), NULL, TRUE);
    InvalidateRect(GetDlgItem(g_dlg, IDC_BGSWATCH), NULL, TRUE);
}

static void select_key_in(int id, int vk)
{
    HWND cb = GetDlgItem(g_dlg, id);
    int i, n = (int)SendMessageA(cb, CB_GETCOUNT, 0, 0);
    char t[32];
    for (i = 0; i < n; i++)
        if ((int)SendMessageA(cb, CB_GETITEMDATA, i, 0) == vk) { SendMessageA(cb, CB_SETCURSEL, i, 0); return; }
    fps_key_to_text(vk, 0, t);
    i = (int)SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageA(cb, CB_SETITEMDATA, i, vk);
    SendMessageA(cb, CB_SETCURSEL, i, 0);
}
static void select_key(int vk) { select_key_in(IDC_KEY, vk); }

static void enable_settings(void)
{
    int i, on = (g_sel < 0) || !g_useGlobal;
    for (i = 0; i < (int)(sizeof g_settingIds / sizeof g_settingIds[0]); i++)
        EnableWindow(GetDlgItem(g_dlg, g_settingIds[i]), on);
    if (on) {
        EnableWindow(GetDlgItem(g_dlg, IDC_BGCOLOR), g_s.bg);
        EnableWindow(GetDlgItem(g_dlg, IDC_CAPLIST), g_s.capmode == FPS_CAP_CYCLE);
    }
}

static void settings_to_ui(void)
{
    int i;
    g_loading = 1;
    select_key(g_s.vk);
    CheckDlgButton(g_dlg, IDC_CTRL, (g_s.mods & FPS_MOD_CTRL) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, IDC_ALT, (g_s.mods & FPS_MOD_ALT) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, IDC_SHIFT, (g_s.mods & FPS_MOD_SHIFT) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, IDC_VISIBLE, g_s.visible ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageA(g_dlg, IDC_CORNER, CB_SETCURSEL, g_s.corner, 0);
    SetDlgItemInt(g_dlg, IDC_X, g_s.x, FALSE);
    SetDlgItemInt(g_dlg, IDC_Y, g_s.y, FALSE);
    SendDlgItemMessageA(g_dlg, IDC_SCALE, CB_SETCURSEL, g_s.scale - 1, 0);
    CheckDlgButton(g_dlg, IDC_BG, g_s.bg ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, IDC_LABEL, g_s.label ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, IDC_FRAMETIME, g_s.frametime ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, IDC_LOG, g_s.log ? BST_CHECKED : BST_UNCHECKED);
    for (i = 0; i < 4 && g_intervals[i] < g_s.interval; i++) ;
    if (i == 4) i = 3;
    SendDlgItemMessageA(g_dlg, IDC_INTERVAL, CB_SETCURSEL, i, 0);
    {
        char t[64];
        CheckDlgButton(g_dlg, IDC_CAPON, g_s.capon ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(g_dlg, IDC_CAPFPS, g_s.capfps, FALSE);
        select_key_in(IDC_CAPKEY, g_s.capvk);
        CheckDlgButton(g_dlg, IDC_CAPCTRL, (g_s.capmods & FPS_MOD_CTRL) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(g_dlg, IDC_CAPALT, (g_s.capmods & FPS_MOD_ALT) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(g_dlg, IDC_CAPSHIFT, (g_s.capmods & FPS_MOD_SHIFT) ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageA(g_dlg, IDC_CAPMODE, CB_SETCURSEL, g_s.capmode, 0);
        fps_caplist_to_text(&g_s, t);
        SetDlgItemTextA(g_dlg, IDC_CAPLIST, t);
    }
    set_brushes();
    enable_settings();
    g_loading = 0;
    redraw_previews();
}

static void ui_to_settings(void)
{
    BOOL ok;
    int v, sel;
    sel = (int)SendDlgItemMessageA(g_dlg, IDC_KEY, CB_GETCURSEL, 0, 0);
    if (sel >= 0) g_s.vk = (int)SendDlgItemMessageA(g_dlg, IDC_KEY, CB_GETITEMDATA, sel, 0);
    g_s.mods = (IsDlgButtonChecked(g_dlg, IDC_CTRL) ? FPS_MOD_CTRL : 0) |
               (IsDlgButtonChecked(g_dlg, IDC_ALT) ? FPS_MOD_ALT : 0) |
               (IsDlgButtonChecked(g_dlg, IDC_SHIFT) ? FPS_MOD_SHIFT : 0);
    g_s.visible = IsDlgButtonChecked(g_dlg, IDC_VISIBLE) == BST_CHECKED;
    sel = (int)SendDlgItemMessageA(g_dlg, IDC_CORNER, CB_GETCURSEL, 0, 0);
    if (sel >= 0) g_s.corner = sel;
    v = (int)GetDlgItemInt(g_dlg, IDC_X, &ok, FALSE); if (ok) g_s.x = v > 4000 ? 4000 : v;
    v = (int)GetDlgItemInt(g_dlg, IDC_Y, &ok, FALSE); if (ok) g_s.y = v > 4000 ? 4000 : v;
    sel = (int)SendDlgItemMessageA(g_dlg, IDC_SCALE, CB_GETCURSEL, 0, 0);
    if (sel >= 0) g_s.scale = sel + 1;
    g_s.bg = IsDlgButtonChecked(g_dlg, IDC_BG) == BST_CHECKED;
    g_s.label = IsDlgButtonChecked(g_dlg, IDC_LABEL) == BST_CHECKED;
    g_s.frametime = IsDlgButtonChecked(g_dlg, IDC_FRAMETIME) == BST_CHECKED;
    g_s.log = IsDlgButtonChecked(g_dlg, IDC_LOG) == BST_CHECKED;
    sel = (int)SendDlgItemMessageA(g_dlg, IDC_INTERVAL, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < 4) g_s.interval = g_intervals[sel];
    EnableWindow(GetDlgItem(g_dlg, IDC_BGCOLOR), g_s.bg);

    g_s.capon = IsDlgButtonChecked(g_dlg, IDC_CAPON) == BST_CHECKED;
    v = (int)GetDlgItemInt(g_dlg, IDC_CAPFPS, &ok, FALSE);
    if (ok) g_s.capfps = v < 10 ? 10 : v > 500 ? 500 : v;
    sel = (int)SendDlgItemMessageA(g_dlg, IDC_CAPKEY, CB_GETCURSEL, 0, 0);
    if (sel >= 0) g_s.capvk = (int)SendDlgItemMessageA(g_dlg, IDC_CAPKEY, CB_GETITEMDATA, sel, 0);
    g_s.capmods = (IsDlgButtonChecked(g_dlg, IDC_CAPCTRL) ? FPS_MOD_CTRL : 0) |
                  (IsDlgButtonChecked(g_dlg, IDC_CAPALT) ? FPS_MOD_ALT : 0) |
                  (IsDlgButtonChecked(g_dlg, IDC_CAPSHIFT) ? FPS_MOD_SHIFT : 0);
    sel = (int)SendDlgItemMessageA(g_dlg, IDC_CAPMODE, CB_GETCURSEL, 0, 0);
    if (sel >= 0) g_s.capmode = sel;
    {
        char t[64];
        GetDlgItemTextA(g_dlg, IDC_CAPLIST, t, sizeof t);
        fps_text_to_caplist(t, &g_s);
    }
    EnableWindow(GetDlgItem(g_dlg, IDC_CAPLIST), g_s.capmode == FPS_CAP_CYCLE);
}

static void set_status(const char *t) { SetDlgItemTextA(g_dlg, IDC_STATUS, t); }

static void set_dirty(int d)
{
    g_dirty = d;
    SetWindowTextA(g_dlg, d ? RM_NAME " " RM_VERSION " *" : RM_NAME " " RM_VERSION);
    if (d) set_status("Not saved yet. Click Save.");
}

static void changed(void)
{
    if (g_loading) return;
    ui_to_settings();
    set_dirty(1);
    redraw_previews();
}

/* ---------- saving ---------- */
static int write_game_ini(Game *g, const FpsSettings *s)
{
    char ini[MAX_PATH];
    if (!file_exists(g->folder)) return 0;
    game_ini(g, ini);
    if (!file_exists(ini)) list_add(g->created, "retrometer.ini");
    fps_settings_save(ini, s);
    return 1;
}

static void save_current(void)
{
    char t[200];
    ui_to_settings();
    if (g_s.capvk && g_s.capvk == g_s.vk && g_s.capmods == g_s.mods &&
        (g_sel < 0 || !g_useGlobal))
        info("The cap hotkey is the same as the show/hide key, so it won't do anything. Pick a different key.");
    if (g_sel < 0) {
        int i, n = 0;
        fps_settings_save(g_globalini, &g_s);
        for (i = 0; i < g_db.count; i++)
            if (g_db.g[i].useGlobal && write_game_ini(&g_db.g[i], &g_s)) n++;
        save_db();
        wsprintfA(t, "Saved. Applied to %d game%s that use the All games settings. Running games update within a second.",
                  n, n == 1 ? "" : "s");
    } else {
        Game *g = &g_db.g[g_sel];
        FpsSettings all;
        g->useGlobal = g_useGlobal;
        if (g_useGlobal) { fps_settings_load(g_globalini, &all); write_game_ini(g, &all); }
        else write_game_ini(g, &g_s);
        save_db();
        wsprintfA(t, "Saved to %sretrometer.ini. A running game updates within a second.", g->folder);
    }
    set_dirty(0);
    set_status(t);
}

static int confirm_discard(void)
{
    int r;
    if (!g_dirty) return 1;
    r = MessageBoxA(g_dlg, "Save your changes first?", "Retrometer 9x", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) save_current();
    set_dirty(0);
    return 1;
}

/* ---------- game list / details ---------- */
static void api_list(unsigned mask, char *out)
{
    int a;
    out[0] = 0;
    for (a = 0; a < NAPI; a++) if (mask & (1u << a)) {
        if (out[0]) lstrcatA(out, ", ");
        lstrcatA(out, g_apis[a].label);
    }
    if (!out[0]) lstrcpyA(out, "none");
}

static void refresh_list(void)
{
    HWND lb = GetDlgItem(g_dlg, IDC_GAMES);
    char t[300], apis[100], prob[400];
    int i, idx;
    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    idx = (int)SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)"<All games: default settings>");
    SendMessageA(lb, LB_SETITEMDATA, idx, (LPARAM)-1);
    for (i = 0; i < g_db.count; i++) {
        api_list(g_db.g[i].installed, apis);
        game_check(&g_db.g[i], prob);
        wsprintfA(t, "%s  (%s)%s", g_db.g[i].name, apis, prob[0] ? "  [!]" : "");
        idx = (int)SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)t);
        SendMessageA(lb, LB_SETITEMDATA, idx, i);
    }
    SendMessageA(lb, LB_SETCURSEL, g_sel + 1, 0);
}

static void show_details(void)
{
    char t[1400], apis[100], prob[400], line[400];
    int a, on = g_sel >= 0;
    EnableWindow(GetDlgItem(g_dlg, IDC_CHANGE), on);
    EnableWindow(GetDlgItem(g_dlg, IDC_REMOVE), on);
    EnableWindow(GetDlgItem(g_dlg, IDC_REPAIR), on);
    EnableWindow(GetDlgItem(g_dlg, IDC_OPEN), on);
    ShowWindow(GetDlgItem(g_dlg, IDC_USEGLOBAL), on ? SW_SHOW : SW_HIDE);
    if (!on) {
        int i, n = 0;
        for (i = 0; i < g_db.count; i++) if (g_db.g[i].useGlobal) n++;
        SetDlgItemTextA(g_dlg, IDC_DETGROUP, "All games");
        wsprintfA(t, "These settings apply to every game set to use the All games settings "
                     "(%d of %d game%s). Saving writes them into each of those games' folders.\r\n\r\n"
                     "Click Add game to put the counter into a new game.",
                  n, g_db.count, g_db.count == 1 ? "" : "s");
        SetDlgItemTextA(g_dlg, IDC_INFO, t);
        return;
    }
    {
        const Game *g = &g_db.g[g_sel];
        SetDlgItemTextA(g_dlg, IDC_DETGROUP, g->name);
        api_list(g->installed, apis);
        wsprintfA(t, "%s\r\nOverlays: %s", g->exe, apis);
        for (a = 0; a < NAPI; a++) if (g->renamed[a][0]) {
            wsprintfA(line, "\r\nGame's %s kept as %s", g_apis[a].own, g->renamed[a]);
            lstrcatA(t, line);
        }
        if (g->patched[0]) { lstrcatA(t, "\r\nPatched for DirectDraw: "); lstrcatA(t, g->patched); }
        game_check(g, prob);
        lstrcatA(t, prob[0] ? "\r\nProblem: " : "\r\nStatus: OK");
        lstrcatA(t, prob);
        if (prob[0]) lstrcatA(t, " (click Repair)");
        SetDlgItemTextA(g_dlg, IDC_INFO, t);
    }
}

static void load_target(int sel)
{
    g_sel = sel;
    if (sel < 0) {
        fps_settings_load(g_globalini, &g_s);
    } else {
        Game *g = &g_db.g[sel];
        char ini[MAX_PATH];
        g_useGlobal = g->useGlobal;
        CheckDlgButton(g_dlg, IDC_USEGLOBAL, g_useGlobal ? BST_CHECKED : BST_UNCHECKED);
        game_ini(g, ini);
        if (g_useGlobal || !file_exists(ini)) fps_settings_load(g_globalini, &g_s);
        else fps_settings_load(ini, &g_s);
    }
    settings_to_ui();
    show_details();
    set_dirty(0);
    set_status(sel < 0 ? "Editing the All games settings." :
               g_useGlobal ? "This game uses the All games settings. Untick the box above to give it its own." :
                             "Editing this game's own settings.");
}

/* ---------- add / change dialog ---------- */
typedef struct {
    Game   *g;
    Detect  d;
    int     change;            /* editing an existing game */
    int     want[NAPI];
    char    newname[NAPI][64];
} AddCtx;

static int rename_needed(const AddCtx *c, int a)
{
    return a != API_DDRAW && c->d.ownDll[a] && !(c->g->installed & (1u << a));
}

static void add_update_rows(HWND dlg, AddCtx *c)
{
    int a;
    for (a = 0; a < NAPI; a++) {
        int on = IsDlgButtonChecked(dlg, IDC_A_CHK0 + a) == BST_CHECKED;
        EnableWindow(GetDlgItem(dlg, IDC_A_NEW0 + a), on && rename_needed(c, a));
    }
}

/* Many Glide / OpenGL / Direct3D 8 games also import DDRAW.DLL just for mode
 * switching, menus or video. DirectDraw is only the right overlay when it is
 * the game's renderer, so don't suggest it when another API was found. */
static int other_api_found(const AddCtx *c)
{
    int a;
    for (a = 0; a < API_DDRAW; a++)
        if (c->d.level[a] != DET_NONE || c->d.oursPresent[a] || (c->g->installed & (1u << a))) return 1;
    return 0;
}

static void add_init(HWND dlg, AddCtx *c)
{
    char t[300];
    int a;
    SetWindowTextA(dlg, c->change ? "Change overlays" : "Add game");
    SetDlgItemTextA(dlg, IDOK, c->change ? "Apply" : "Install");
    SetDlgItemTextA(dlg, IDC_A_NAME, c->g->name);
    wsprintfA(t, "Exe: %s", c->g->exe);
    SetDlgItemTextA(dlg, IDC_A_EXE, t);
    for (a = 0; a < NAPI; a++) {
        int inst = (c->g->installed & (1u << a)) != 0, tick;
        const char *w = c->d.where[a];
        if (inst) lstrcpyA(t, "Installed");
        else if (c->d.oursPresent[a]) lstrcpyA(t, "Already has Retrometer 9x (hand install)");
        else if (c->d.level[a] == DET_IMPORT) wsprintfA(t, "Used by %s", w);
        else if (c->d.level[a] == DET_RUNTIME)
            wsprintfA(t, a == API_DDRAW ? "Loaded at run time: can't hook" : "Loaded at run time (%s)", w);
        else lstrcpyA(t, "Not detected");
        SetDlgItemTextA(dlg, IDC_A_FOUND0 + a, t);

        tick = inst || c->d.oursPresent[a] ||
               (a == API_DDRAW ? (c->d.ddrawFiles[0] != 0 && !other_api_found(c)) : (c->d.level[a] != DET_NONE));
        if (a == API_DDRAW && !inst && c->d.ddrawFiles[0] && other_api_found(c))
            wsprintfA(t, "Used by %s - see note", w), SetDlgItemTextA(dlg, IDC_A_FOUND0 + a, t);
        CheckDlgButton(dlg, IDC_A_CHK0 + a, tick ? BST_CHECKED : BST_UNCHECKED);
        if (a == API_DDRAW && !inst && !c->d.ddrawFiles[0] && !c->d.fpsddFiles[0])
            EnableWindow(GetDlgItem(dlg, IDC_A_CHK0 + a), FALSE);

        if (rename_needed(c, a)) {
            suggest_rename(c->g->folder, a, t);
            SetDlgItemTextA(dlg, IDC_A_NEW0 + a, t);
        } else if (inst && c->g->renamed[a][0]) {
            SetDlgItemTextA(dlg, IDC_A_NEW0 + a, c->g->renamed[a]);
        } else if (a == API_DDRAW && c->d.ownDll[a]) {
            SetDlgItemTextA(dlg, IDC_A_NEW0 + a, "(game's ddraw.dll kept)");
        }
        SendDlgItemMessageA(dlg, IDC_A_NEW0 + a, EM_SETREADONLY, !rename_needed(c, a), 0);
    }
    SetDlgItemTextA(dlg, IDC_A_NOTE,
        "Tick the overlay for the renderer you play with. DirectDraw is only for games that draw with "
        "DirectDraw or Direct3D 7 and older: many Glide/OpenGL games import it just for menus, so leave it "
        "off for those. A DLL the game ships is renamed (not deleted) and restored on removal; DirectDraw "
        "also patches the exe (backup kept).");
    add_update_rows(dlg, c);
}

static int valid_dll_name(const char *n)
{
    int len = lstrlenA(n), i;
    if (len < 5 || lstrcmpiA(n + len - 4, ".dll") != 0) return 0;
    for (i = 0; i < len; i++) if (n[i] == '\\' || n[i] == '/' || n[i] == ':' || n[i] == '*' || n[i] == '?') return 0;
    return 1;
}

static int add_ok(HWND dlg, AddCtx *c)
{
    char q[1600], line[300], p[MAX_PATH];
    int a, renames = 0, any = 0;
    GetDlgItemTextA(dlg, IDC_A_NAME, c->g->name, sizeof c->g->name);
    if (!c->g->name[0]) lstrcpyA(c->g->name, base_name(c->g->exe));
    lstrcpyA(q, "Please confirm:\r\n\r\n");
    for (a = 0; a < NAPI; a++) {
        int inst = (c->g->installed & (1u << a)) != 0;
        c->want[a] = IsDlgButtonChecked(dlg, IDC_A_CHK0 + a) == BST_CHECKED;
        c->newname[a][0] = 0;
        if (c->want[a] && !inst && rename_needed(c, a)) {
            GetDlgItemTextA(dlg, IDC_A_NEW0 + a, c->newname[a], 64);
            path_join(p, c->g->folder, c->newname[a]);
            if (!valid_dll_name(c->newname[a]) || istr_eq(c->newname[a], g_apis[a].own)) {
                MessageBoxA(dlg, "Enter a different file name ending in .dll for the game's own DLL.", "Retrometer 9x", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dlg, IDC_A_NEW0 + a));
                return 0;
            }
            if (file_exists(p)) {
                MessageBoxA(dlg, "A file with that name already exists in the game folder. Choose another name.", "Retrometer 9x", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dlg, IDC_A_NEW0 + a));
                return 0;
            }
            wsprintfA(line, "- Rename the game's %s to %s, then install the %s overlay\r\n", g_apis[a].own, c->newname[a], g_apis[a].label);
            lstrcatA(q, line);
            renames++;
        } else if (c->want[a] && !inst) {
            if (a == API_DDRAW) wsprintfA(line, "- Install the DirectDraw overlay and patch: %s\r\n", c->d.ddrawFiles[0] ? c->d.ddrawFiles : "(already patched)");
            else wsprintfA(line, "- Install the %s overlay\r\n", g_apis[a].label);
            lstrcatA(q, line);
            if (a == API_DDRAW) renames++;
        } else if (!c->want[a] && inst) {
            if (c->g->renamed[a][0])
                wsprintfA(line, "- Remove the %s overlay and rename %s back to %s\r\n", g_apis[a].label, c->g->renamed[a], g_apis[a].own);
            else
                wsprintfA(line, "- Remove the %s overlay\r\n", g_apis[a].label);
            lstrcatA(q, line);
            renames++;
        } else continue;
        any = 1;
    }
    if (!any) {
        if (!c->change) { MessageBoxA(dlg, "Tick at least one overlay to install.", "Retrometer 9x", MB_OK | MB_ICONINFORMATION); return 0; }
        return 1;   /* only the name changed */
    }
    if (c->want[API_DDRAW] && !(c->g->installed & (1u << API_DDRAW))) {
        int b, other = -1;
        for (b = 0; b < API_DDRAW; b++) if (c->want[b]) { other = b; break; }
        if (other >= 0) {
            char w[700];
            wsprintfA(w, "You've ticked both %s and DirectDraw.\r\n\r\n"
                         "Only add DirectDraw if you also play this game in its Direct3D or software mode. "
                         "If you play in %s, the game probably uses DirectDraw only for menus and videos, "
                         "and the DirectDraw overlay would add a second counter there and apply the frame "
                         "cap to menus.\r\n\r\nInstall DirectDraw anyway?",
                      g_apis[other].label, g_apis[other].label);
            if (MessageBoxA(dlg, w, "Retrometer 9x", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
        }
    }
    if (renames && MessageBoxA(dlg, q, "Retrometer 9x", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 0;
    return 1;
}

static BOOL CALLBACK AddProc(HWND dlg, UINT m, WPARAM wp, LPARAM lp)
{
    AddCtx *c = (AddCtx *)GetWindowLongA(dlg, DWL_USER);
    switch (m) {
    case WM_INITDIALOG:
        SetWindowLongA(dlg, DWL_USER, lp);
        add_init(dlg, (AddCtx *)lp);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) >= IDC_A_CHK0 && LOWORD(wp) < IDC_A_CHK0 + NAPI) add_update_rows(dlg, c);
        if (LOWORD(wp) == IDOK && add_ok(dlg, c)) EndDialog(dlg, IDOK);
        if (LOWORD(wp) == IDCANCEL) EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* Run the dialog and apply. Returns 1 if anything changed. */
static int add_or_change(Game *g, int change)
{
    static AddCtx c;
    int a;
    memset(&c, 0, sizeof c);
    c.g = g;
    c.change = change;
    detect_folder(g->folder, &c.d);
    if (DialogBoxParamA(g_inst, MAKEINTRESOURCEA(IDD_ADD), g_dlg, AddProc, (LPARAM)&c) != IDOK) return 0;
    g_report[0] = 0;
    for (a = 0; a < NAPI; a++) {
        int inst = (g->installed & (1u << a)) != 0;
        if (c.want[a] && !inst) game_install_api(g, a, c.newname[a], g_appdir, &c.d, g_report);
        else if (!c.want[a] && inst) game_remove_api(g, a, g_report);
    }
    return 1;
}

static int pick_exe(char *out)
{
    OPENFILENAMEA of;
    memset(&of, 0, sizeof of);
    of.lStructSize = OPENFILENAME_SIZE_VERSION_400A;   /* Win95/98 size */
    of.hwndOwner = g_dlg;
    of.lpstrFilter = "Programs (*.exe)\0*.exe\0";
    out[0] = 0;
    of.lpstrFile = out;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = "Choose the game's main .exe";
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    return GetOpenFileNameA(&of);
}

static void do_add(void)
{
    char exe[MAX_PATH];
    Game *g;
    int i, n;
    if (!confirm_discard()) return;
    if (!pick_exe(exe)) return;
    i = db_find_exe(&g_db, exe);
    if (i >= 0) {
        info("That game is already in the list. Use Change overlays to add or remove overlays.");
        load_target(i);
        refresh_list();
        return;
    }
    if (g_db.count >= MAX_GAMES) { info("The game list is full."); return; }
    g = &g_db.g[g_db.count];
    memset(g, 0, sizeof *g);
    lstrcpyA(g->exe, exe);
    lstrcpyA(g->folder, exe);
    *(char *)base_name(g->folder) = 0;
    add_slash(g->folder);
    lstrcpyA(g->name, base_name(exe));
    n = lstrlenA(g->name);
    if (n > 4 && lstrcmpiA(g->name + n - 4, ".exe") == 0) g->name[n - 4] = 0;
    g->useGlobal = 1;
    if (!add_or_change(g, 0)) return;
    if (!g->installed) {
        lstrcatA(g_report, "\r\nNothing was installed, so the game was not added to the list.");
        info(g_report);
        return;
    }
    {   /* new games start with the All games settings */
        FpsSettings all;
        fps_settings_load(g_globalini, &all);
        write_game_ini(g, &all);
    }
    g_db.count++;
    save_db();
    load_target(g_db.count - 1);
    refresh_list();
    info(g_report);
}

static void do_change(void)
{
    Game *g;
    if (g_sel < 0 || !confirm_discard()) return;
    g = &g_db.g[g_sel];
    if (!file_exists(g->folder)) { info("The game folder can't be found. Use Remove to take it off the list."); return; }
    if (!add_or_change(g, 1)) return;
    save_db();
    refresh_list();
    show_details();
    if (g_report[0]) info(g_report);
}

static void do_remove(void)
{
    char q[600];
    Game *g;
    int i;
    if (g_sel < 0 || !confirm_discard()) return;
    g = &g_db.g[g_sel];
    g_report[0] = 0;
    if (!file_exists(g->folder)) {
        wsprintfA(q, "The folder for %s can't be found (moved or deleted).\r\n\r\nJust take it off the list?", g->name);
        if (!ask(q)) return;
    } else {
        wsprintfA(q, "Remove the FPS counter from %s?\r\n\r\nThis deletes the files Retrometer 9x added and gives the game's own "
                     "DLLs back their original names.", g->name);
        if (!ask(q)) return;
        if (!game_remove_all(g, g_report)) {
            save_db();       /* keep what did get undone */
            lstrcatA(g_report, "\r\nSome things could not be undone (is the game running?). The game stays in the list; try again.");
            info(g_report);
            refresh_list();
            show_details();
            return;
        }
    }
    for (i = g_sel; i < g_db.count - 1; i++) g_db.g[i] = g_db.g[i + 1];
    g_db.count--;
    save_db();
    load_target(-1);
    refresh_list();
    if (g_report[0]) info(g_report);
}

static void do_repair(void)
{
    char prob[400];
    Game *g;
    if (g_sel < 0) return;
    g = &g_db.g[g_sel];
    game_check(g, prob);
    if (!prob[0]) { info("Everything is in place for this game."); return; }
    g_report[0] = 0;
    {
        int replace = 0;
        if (game_repair_conflict(g))
            replace = ask("The game has put back its own driver DLL (a patch or reinstall did this), and the "
                          "old copy Retrometer 9x renamed earlier is still there.\r\n\r\n"
                          "Replace the old renamed copy with the game's new one? (Usually yes: the new "
                          "one is what the game was updated with.)");
        game_repair(g, g_appdir, replace, g_report);
    }
    save_db();
    refresh_list();
    show_details();
    info(g_report[0] ? g_report : "Nothing could be repaired automatically.");
}

/* ---------- colours, key detection ---------- */
static void pick_color(DWORD *rgb)
{
    CHOOSECOLORA cc;
    memset(&cc, 0, sizeof cc);
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = g_dlg;
    cc.rgbResult = RGB((*rgb >> 16) & 255, (*rgb >> 8) & 255, *rgb & 255);
    cc.lpCustColors = g_custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColorA(&cc)) {
        *rgb = ((DWORD)GetRValue(cc.rgbResult) << 16) | ((DWORD)GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult);
        set_brushes();
        set_dirty(1);
        redraw_previews();
    }
}

static void detect_tick(void)
{
    int vk;
    if (GetTickCount() - g_detect_start > 6000) goto stop;
    for (vk = 0x08; vk <= 0xFE; vk++) {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || (vk >= 0xA0 && vk <= 0xA5) ||
            vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS || vk == VK_CAPITAL) continue;
        if (!(GetAsyncKeyState(vk) & 0x8000)) continue;
        if (vk == VK_ESCAPE) goto stop;
        if (vk == VK_RETURN || vk == VK_SPACE || vk == VK_TAB) continue;
        g_loading = 1;
        {
            int cap = g_detect_target == IDC_CAPKEY;
            select_key_in(g_detect_target, vk);
            CheckDlgButton(g_dlg, cap ? IDC_CAPCTRL : IDC_CTRL, (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(g_dlg, cap ? IDC_CAPALT : IDC_ALT, (GetAsyncKeyState(VK_MENU) & 0x8000) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(g_dlg, cap ? IDC_CAPSHIFT : IDC_SHIFT, (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? BST_CHECKED : BST_UNCHECKED);
        }
        g_loading = 0;
        changed();
        goto stop;
    }
    return;
stop:
    KillTimer(g_dlg, 1);
    g_detecting = 0;
    SetDlgItemTextA(g_dlg, IDC_DETECT, "&Detect key...");
    SetDlgItemTextA(g_dlg, IDC_CAPDETECT, "...");
}

/* ---------- preview control ---------- */
static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r; HBRUSH b = CreateSolidBrush(c);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void draw_scene(HDC dc, int w, int h)
{
    int i;
    for (i = 0; i < 8; i++) fill(dc, 0, i * h / 16, w, h / 16 + 1, RGB(40 + i * 12, 80 + i * 14, 160 + i * 10));
    fill(dc, 0, h / 2, w, h - h / 2, RGB(60, 110, 50));
    fill(dc, w / 2 - w / 10, h / 2 - h / 5, w / 5, h / 5, RGB(130, 120, 110));
    fill(dc, w / 2 - w / 30, h / 2 - h / 9, w / 15, h / 9, RGB(70, 50, 40));
}

static void draw_overlay(HDC dc, int sw, int sh, const FpsSettings *s)
{
    FpsRect r[FPS_MAX_RECTS];
    char text[48];
    int i, n;
    fps_format(s, 60, 167, text);
    n = fps_layout(s, text, sw, sh, 0, 0, r, FPS_MAX_RECTS);
    for (i = 0; i < n; i++) {
        DWORD c = r[i].rgb;
        fill(dc, r[i].x, r[i].y, r[i].w, r[i].h, RGB((c >> 16) & 255, (c >> 8) & 255, c & 255));
    }
}

static LRESULT CALLBACK PreviewProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(w, &ps), mem;
        HBITMAP bmp, old;
        int full = GetDlgCtrlID(w) == IDC_PREVIEW;
        int bw, bh;
        FpsSettings t = g_s;
        GetClientRect(w, &rc);
        bw = full ? g_prev_w : rc.right;
        bh = full ? g_prev_h : rc.bottom;
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, bw, bh);
        old = (HBITMAP)SelectObject(mem, bmp);
        if (full) draw_scene(mem, bw, bh);
        else { draw_scene(mem, bw * 4, bh * 4); t.corner = FPS_TOPLEFT; t.x = 4; t.y = 4; }
        draw_overlay(mem, bw, bh, &t);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, bw, bh, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(w, &ps);
        return 0;
    }
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcA(w, m, wp, lp);
}

/* ---------- main dialog ---------- */
static void init_dialog(HWND dlg)
{
    static const char *const corners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    static const char *const intervals[] = {"1/4 second", "1/2 second", "1 second", "2 seconds"};
    static const struct { const char *t; int w, h; } res[] = {
        {"320 x 240", 320, 240}, {"512 x 384", 512, 384}, {"640 x 480", 640, 480},
        {"800 x 600", 800, 600}, {"1024 x 768", 1024, 768}, {"1280 x 1024", 1280, 1024},
    };
    char t[64];
    int i, idx;

    g_dlg = dlg;
    for (i = 0; i < fps_key_name_count; i++) {
        idx = (int)SendDlgItemMessageA(dlg, IDC_KEY, CB_ADDSTRING, 0, (LPARAM)fps_key_names[i].name);
        SendDlgItemMessageA(dlg, IDC_KEY, CB_SETITEMDATA, idx, fps_key_names[i].vk);
    }
    for (i = 0; i < 4; i++) SendDlgItemMessageA(dlg, IDC_CORNER, CB_ADDSTRING, 0, (LPARAM)corners[i]);
    idx = (int)SendDlgItemMessageA(dlg, IDC_CAPKEY, CB_ADDSTRING, 0, (LPARAM)"None");
    SendDlgItemMessageA(dlg, IDC_CAPKEY, CB_SETITEMDATA, idx, 0);
    for (i = 0; i < fps_key_name_count; i++) {
        idx = (int)SendDlgItemMessageA(dlg, IDC_CAPKEY, CB_ADDSTRING, 0, (LPARAM)fps_key_names[i].name);
        SendDlgItemMessageA(dlg, IDC_CAPKEY, CB_SETITEMDATA, idx, fps_key_names[i].vk);
    }
    SendDlgItemMessageA(dlg, IDC_CAPMODE, CB_ADDSTRING, 0, (LPARAM)"Turn cap on/off");
    SendDlgItemMessageA(dlg, IDC_CAPMODE, CB_ADDSTRING, 0, (LPARAM)"Step: off, then each");
    SendDlgItemMessageA(dlg, IDC_CAPFPS, EM_LIMITTEXT, 3, 0);
    SendDlgItemMessageA(dlg, IDC_CAPLIST, EM_LIMITTEXT, 30, 0);
    for (i = 1; i <= 8; i++) {
        wsprintfA(t, "%dx  (%d pixels tall)", i, 5 * i);
        SendDlgItemMessageA(dlg, IDC_SCALE, CB_ADDSTRING, 0, (LPARAM)t);
    }
    for (i = 0; i < 4; i++) SendDlgItemMessageA(dlg, IDC_INTERVAL, CB_ADDSTRING, 0, (LPARAM)intervals[i]);
    for (i = 0; i < 6; i++) {
        idx = (int)SendDlgItemMessageA(dlg, IDC_PREVRES, CB_ADDSTRING, 0, (LPARAM)res[i].t);
        SendDlgItemMessageA(dlg, IDC_PREVRES, CB_SETITEMDATA, idx, MAKELONG(res[i].w, res[i].h));
    }
    SendDlgItemMessageA(dlg, IDC_PREVRES, CB_SETCURSEL, 2, 0);
    SendDlgItemMessageA(dlg, IDC_X, EM_LIMITTEXT, 4, 0);
    SendDlgItemMessageA(dlg, IDC_Y, EM_LIMITTEXT, 4, 0);
    for (i = 0; i < 16; i++) g_custom[i] = RGB(255, 255, 255);

    db_load(g_dbpath, &g_db);
    load_target(-1);
    refresh_list();
}

static BOOL CALLBACK DlgProc(HWND dlg, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG:
        SendMessageA(dlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconA(g_inst, MAKEINTRESOURCEA(IDI_APP)));
        init_dialog(dlg);
        return TRUE;

    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == GetDlgItem(dlg, IDC_SWATCH)) return (BOOL)(INT_PTR)g_brText;
        if ((HWND)lp == GetDlgItem(dlg, IDC_BGSWATCH)) return (BOOL)(INT_PTR)g_brBg;
        return FALSE;

    case WM_TIMER:
        if (wp == 1) detect_tick();
        return TRUE;

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case IDC_GAMES:
            if (code == LBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageA(dlg, IDC_GAMES, LB_GETCURSEL, 0, 0);
                int target = sel <= 0 ? -1 : (int)SendDlgItemMessageA(dlg, IDC_GAMES, LB_GETITEMDATA, sel, 0);
                if (target != g_sel) {
                    if (confirm_discard()) load_target(target);
                    SendDlgItemMessageA(dlg, IDC_GAMES, LB_SETCURSEL, g_sel + 1, 0);
                }
            }
            break;
        case IDC_ADD: do_add(); break;
        case IDC_CHANGE: do_change(); break;
        case IDC_REMOVE: do_remove(); break;
        case IDC_REPAIR: do_repair(); break;
        case IDC_OPEN:
            if (g_sel >= 0) ShellExecuteA(dlg, "open", g_db.g[g_sel].folder, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDC_USEGLOBAL:
            if (code == BN_CLICKED && g_sel >= 0) {
                g_useGlobal = IsDlgButtonChecked(dlg, IDC_USEGLOBAL) == BST_CHECKED;
                if (g_useGlobal) fps_settings_load(g_globalini, &g_s);   /* show what it'll get */
                settings_to_ui();
                set_dirty(1);
            }
            break;
        case IDC_DETECT:
        case IDC_CAPDETECT:
            if (!g_detecting) {
                g_detecting = 1;
                g_detect_target = id == IDC_CAPDETECT ? IDC_CAPKEY : IDC_KEY;
                g_detect_start = GetTickCount();
                SetDlgItemTextA(dlg, id, id == IDC_CAPDETECT ? "?" : "Press a key...");
                if (id == IDC_CAPDETECT) set_status("Press the key for the frame cap (Esc cancels).");
                SetTimer(dlg, 1, 30, NULL);
            }
            break;
        case IDC_COLOR: pick_color(&g_s.color); break;
        case IDC_BGCOLOR: pick_color(&g_s.bgcolor); break;
        case IDC_SAVE: save_current(); break;
        case IDC_DEFAULTS:
            fps_settings_defaults(&g_s);
            settings_to_ui();
            set_dirty(1);
            break;
        case IDC_PREVRES:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageA(dlg, IDC_PREVRES, CB_GETCURSEL, 0, 0);
                DWORD d = (DWORD)SendDlgItemMessageA(dlg, IDC_PREVRES, CB_GETITEMDATA, sel, 0);
                g_prev_w = LOWORD(d); g_prev_h = HIWORD(d);
                redraw_previews();
            }
            break;
        case IDC_CLOSE:
        case IDCANCEL:
            if (confirm_discard()) EndDialog(dlg, 0);
            break;
        case IDC_KEY: case IDC_CORNER: case IDC_SCALE: case IDC_INTERVAL: case IDC_CAPKEY: case IDC_CAPMODE:
            if (code == CBN_SELCHANGE) changed();
            break;
        case IDC_X: case IDC_Y: case IDC_CAPFPS: case IDC_CAPLIST:
            if (code == EN_CHANGE) changed();
            break;
        case IDC_CTRL: case IDC_ALT: case IDC_SHIFT: case IDC_VISIBLE: case IDC_BG:
        case IDC_LABEL: case IDC_FRAMETIME: case IDC_LOG:
        case IDC_CAPON: case IDC_CAPCTRL: case IDC_CAPALT: case IDC_CAPSHIFT:
            if (code == BN_CLICKED) changed();
            break;
        }
        return TRUE;
    }
    }
    return FALSE;
}

void WINAPI entry(void)
{
    WNDCLASSA wc;
    int i, last = 0;
    g_inst = GetModuleHandleA(NULL);
    GetModuleFileNameA(NULL, g_appdir, MAX_PATH);
    for (i = 0; g_appdir[i]; i++) if (g_appdir[i] == '\\' || g_appdir[i] == '/') last = i + 1;
    g_appdir[last] = 0;
    path_join(g_dbpath, g_appdir, "games.ini");
    path_join(g_globalini, g_appdir, "allgames.ini");

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "FpsPreview";
    RegisterClassA(&wc);

    DialogBoxParamA(g_inst, MAKEINTRESOURCEA(IDD_MAIN), NULL, DlgProc, 0);
    if (g_brText) DeleteObject(g_brText);
    if (g_brBg) DeleteObject(g_brBg);
    ExitProcess(0);
}
