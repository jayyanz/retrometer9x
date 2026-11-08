#include "gamedb.h"
#include "../common/pepatch.h"

const ApiInfo g_apis[NAPI] = {
    {"OpenGL",     "opengl32.dll", "opengl32.dll", "opengl32a.dll", "opengl32", "glreal.dll",   "OPENGL32.DLL", "opengl32"},
    {"Glide 2",    "glide2x.dll",  "glide2x.dll",  "glide2xa.dll",  "glide2x",  "g2real.dll",   "GLIDE2X.DLL",  "glide2x"},
    {"Glide 3",    "glide3x.dll",  "glide3x.dll",  "glide3xa.dll",  "glide3x",  "g3real.dll",   "GLIDE3X.DLL",  "glide3x"},
    {"Direct3D 8", "d3d8.dll",     "d3d8.dll",     "d3d8a.dll",     "d3d8",     "d3d8real.dll", "D3D8.DLL",     "d3d8.dll"},
    {"DirectDraw", "fpsdd.dll",    "",             "",              "ddraw",    "",             "DDRAW.DLL",    "ddraw.dll"},
};

/* ---------- helpers ---------- */
void path_join(char *out, const char *dir, const char *file) { lstrcpyA(out, dir); lstrcatA(out, file); }
int file_exists(const char *p) { return GetFileAttributesA(p) != 0xFFFFFFFF; }
static int exists_in(const char *dir, const char *f) { char p[MAX_PATH]; path_join(p, dir, f); return file_exists(p); }
int istr_eq(const char *a, const char *b) { return lstrcmpiA(a, b) == 0; }
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int istarts(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) if (lower(*s) != lower(*prefix)) return 0;
    return 1;
}

int list_has(const char *list, const char *item)
{
    char tok[MAX_PATH];
    while (*list) {
        int n = 0;
        while (*list && *list != ',' && n < MAX_PATH - 1) tok[n++] = *list++;
        tok[n] = 0;
        if (*list == ',') list++;
        if (istr_eq(tok, item)) return 1;
    }
    return 0;
}

void list_add(char *list, const char *item)
{
    if (list_has(list, item) || lstrlenA(list) + lstrlenA(item) + 2 >= LIST_LEN) return;
    if (list[0]) lstrcatA(list, ",");
    lstrcatA(list, item);
}

void list_remove(char *list, const char *item)
{
    char out[LIST_LEN], tok[MAX_PATH];
    const char *p = list;
    out[0] = 0;
    while (*p) {
        int n = 0;
        while (*p && *p != ',' && n < MAX_PATH - 1) tok[n++] = *p++;
        tok[n] = 0;
        if (*p == ',') p++;
        if (tok[0] && !istr_eq(tok, item)) { if (out[0]) lstrcatA(out, ","); lstrcatA(out, tok); }
    }
    lstrcpyA(list, out);
}

/* Calls fn for every name in a list. */
typedef void (*ItemFn)(const char *item, void *ctx);
static void list_each(const char *list, ItemFn fn, void *ctx)
{
    char tok[MAX_PATH];
    while (*list) {
        int n = 0;
        while (*list && *list != ',' && n < MAX_PATH - 1) tok[n++] = *list++;
        tok[n] = 0;
        if (*list == ',') list++;
        if (tok[0]) fn(tok, ctx);
    }
}

int file_is_ours(const char *path)
{
    /* current marker, plus the one from pre-release builds (named fpsovl) */
    static const char *const marks[2] = {"RETROMETER9X-PROXY-", "FPSOVL-PROXY-"};
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char buf[4096];
    DWORD got, total = 0;
    int m[2] = {0, 0}, found = 0, k;
    if (h == INVALID_HANDLE_VALUE) return 0;
    while (!found && total < 512 * 1024 && ReadFile(h, buf, sizeof buf, &got, NULL) && got) {
        DWORD i;
        for (i = 0; i < got && !found; i++)
            for (k = 0; k < 2; k++) {
                const char *mk = marks[k];
                if (buf[i] == mk[m[k]]) { if (!mk[++m[k]]) { found = 1; break; } }
                else m[k] = (buf[i] == mk[0]) ? 1 : 0;
            }
        total += got;
    }
    CloseHandle(h);
    return found;
}
static int ours_in(const char *dir, const char *f) { char p[MAX_PATH]; path_join(p, dir, f); return file_is_ours(p); }

/* ---------- database ---------- */
static void sect(char *out, int i) { wsprintfA(out, "Game%d", i + 1); }

void db_load(const char *path, GameDb *db)
{
    char s[16], k[16];
    int i, a;
    db->count = (int)GetPrivateProfileIntA("Games", "Count", 0, path);
    if (db->count < 0) db->count = 0;
    if (db->count > MAX_GAMES) db->count = MAX_GAMES;
    for (i = 0; i < db->count; i++) {
        Game *g = &db->g[i];
        sect(s, i);
        GetPrivateProfileStringA(s, "Name", "", g->name, sizeof g->name, path);
        GetPrivateProfileStringA(s, "Exe", "", g->exe, sizeof g->exe, path);
        GetPrivateProfileStringA(s, "Folder", "", g->folder, sizeof g->folder, path);
        g->useGlobal = (int)GetPrivateProfileIntA(s, "UseGlobal", 1, path);
        g->installed = (unsigned)GetPrivateProfileIntA(s, "Installed", 0, path);
        for (a = 0; a < NAPI; a++) {
            wsprintfA(k, "Renamed%d", a);
            GetPrivateProfileStringA(s, k, "", g->renamed[a], sizeof g->renamed[a], path);
        }
        GetPrivateProfileStringA(s, "Patched", "", g->patched, LIST_LEN, path);
        GetPrivateProfileStringA(s, "Created", "", g->created, LIST_LEN, path);
    }
}

void db_save(const char *path, const GameDb *db)
{
    char s[16], k[16], v[16];
    int i, a;
    for (i = 0; i < MAX_GAMES; i++) { sect(s, i); WritePrivateProfileStringA(s, NULL, NULL, path); }
    WritePrivateProfileStringA("Games", NULL, NULL, path);
    wsprintfA(v, "%d", db->count);
    WritePrivateProfileStringA("Games", "Count", v, path);
    for (i = 0; i < db->count; i++) {
        const Game *g = &db->g[i];
        sect(s, i);
        WritePrivateProfileStringA(s, "Name", g->name, path);
        WritePrivateProfileStringA(s, "Exe", g->exe, path);
        WritePrivateProfileStringA(s, "Folder", g->folder, path);
        WritePrivateProfileStringA(s, "UseGlobal", g->useGlobal ? "1" : "0", path);
        wsprintfA(v, "%u", g->installed);
        WritePrivateProfileStringA(s, "Installed", v, path);
        for (a = 0; a < NAPI; a++) {
            if (!g->renamed[a][0]) continue;
            wsprintfA(k, "Renamed%d", a);
            WritePrivateProfileStringA(s, k, g->renamed[a], path);
        }
        if (g->patched[0]) WritePrivateProfileStringA(s, "Patched", g->patched, path);
        if (g->created[0]) WritePrivateProfileStringA(s, "Created", g->created, path);
    }
    WritePrivateProfileStringA(NULL, NULL, NULL, path);   /* flush the Win9x ini cache */
}

int db_find_exe(const GameDb *db, const char *exe)
{
    int i;
    for (i = 0; i < db->count; i++) if (istr_eq(db->g[i].exe, exe)) return i;
    return -1;
}

/* ---------- detection ---------- */

/* Search a file for the run-time probe strings (case-insensitive). Bit per API. */
static unsigned scan_strings(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    static unsigned char buf[16384];
    int pos[NAPI] = {0};
    unsigned found = 0, all = (1u << NAPI) - 1;
    DWORD got, total = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    while (found != all && total < 16 * 1024 * 1024 && ReadFile(h, buf, sizeof buf, &got, NULL) && got) {
        DWORD i;
        int a;
        for (i = 0; i < got; i++) {
            int c = lower(buf[i]);
            for (a = 0; a < NAPI; a++) {
                const char *p = g_apis[a].probe;
                if (c == p[pos[a]]) { if (!p[++pos[a]]) { found |= 1u << a; pos[a] = 0; } }
                else pos[a] = (c == p[0]) ? 1 : 0;
            }
        }
        total += got;
    }
    CloseHandle(h);
    return found;
}

static void add_where(char *list, const char *name)
{
    if (lstrlenA(list) + lstrlenA(name) + 3 >= 120) return;
    if (list[0]) lstrcatA(list, ", ");
    lstrcatA(list, name);
}

/* Driver DLLs and our own files aren't "the game": skip them when scanning. */
static int skip_file(const char *folder, const char *name)
{
    static const char *const prefixes[] = {"opengl32", "glide2x", "glide3x", "d3d8", "ddraw", "fpsdd", "glreal", "g2real", "g3real"};
    int i;
    for (i = 0; i < (int)(sizeof prefixes / sizeof prefixes[0]); i++) if (istarts(name, prefixes[i])) return 1;
    return ours_in(folder, name);
}

void detect_folder(const char *folder, Detect *d)
{
    static const char *const pats[] = {"*.exe", "*.dll"};
    const char *imports[NAPI + 1];
    char pat[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int a, p;

    memset(d, 0, sizeof *d);
    for (a = 0; a < NAPI; a++) imports[a] = g_apis[a].import;
    imports[NAPI] = "FPSDD.DLL";

    for (p = 0; p < 2; p++) {
        path_join(pat, folder, pats[p]);
        h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            unsigned m, s;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (skip_file(folder, fd.cFileName)) continue;
            path_join(path, folder, fd.cFileName);
            m = pe_import_mask(path, imports, NAPI + 1);
            s = scan_strings(path);
            for (a = 0; a < NAPI; a++) {
                int lvl = (m & (1u << a)) ? DET_IMPORT : (s & (1u << a)) ? DET_RUNTIME : DET_NONE;
                if (a == API_DDRAW && (m & (1u << NAPI))) lvl = DET_IMPORT;
                if (lvl == DET_NONE) continue;
                if (lvl > d->level[a]) { d->level[a] = lvl; d->where[a][0] = 0; }
                if (lvl == d->level[a]) add_where(d->where[a], fd.cFileName);
            }
            if (m & (1u << API_DDRAW)) list_add(d->ddrawFiles, fd.cFileName);
            if (m & (1u << NAPI)) list_add(d->fpsddFiles, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    for (a = 0; a < NAPI; a++) {
        if (exists_in(folder, g_apis[a].file) && ours_in(folder, g_apis[a].file)) d->oursPresent[a] = 1;
        if (g_apis[a].own[0] && exists_in(folder, g_apis[a].own) && !ours_in(folder, g_apis[a].own)) d->ownDll[a] = 1;
    }
    if (exists_in(folder, "ddraw.dll") && !ours_in(folder, "ddraw.dll")) d->ownDll[API_DDRAW] = 1;
}

void suggest_rename(const char *folder, int api, char *out)
{
    const ApiInfo *a = &g_apis[api];
    char base[32];
    int i, n;
    lstrcpyA(out, a->alt);
    if (!exists_in(folder, out)) return;
    lstrcpyA(base, a->own);
    n = lstrlenA(base) - 4;               /* strip ".dll" */
    base[n] = 0;
    for (i = 'b'; i <= 'z'; i++) {
        wsprintfA(out, "%s%c.dll", base, i);
        if (!exists_in(folder, out)) return;
    }
    wsprintfA(out, "%s_game.dll", base);
}

/* ---------- install / remove ---------- */
static void say(char *report, const char *fmt, const char *a, const char *b)
{
    char line[400];
    wsprintfA(line, fmt, a, b);
    lstrcatA(report, line);
    lstrcatA(report, "\r\n");
}

static void say3(char *report, const char *fmt, const char *a, const char *b, const char *c)
{
    char line[500];
    wsprintfA(line, fmt, a, b, c);
    lstrcatA(report, line);
    lstrcatA(report, "\r\n");
}

static void ini_path(const Game *g, char *out) { path_join(out, g->folder, "retrometer.ini"); }

static void track_ini(Game *g)
{
    char ini[MAX_PATH];
    ini_path(g, ini);
    if (!file_exists(ini)) list_add(g->created, "retrometer.ini");
}

typedef struct { Game *g; char *report; int ok; const char *err; } PatchCtx;

static void patch_one(const char *name, void *ctx)
{
    PatchCtx *pc = (PatchCtx *)ctx;
    char path[MAX_PATH], bak[MAX_PATH + 8], err[300];
    int had_bak;
    path_join(path, pc->g->folder, name);
    lstrcpyA(bak, name);
    lstrcatA(bak, ".rmbak");
    had_bak = exists_in(pc->g->folder, bak);
    if (pe_ddraw_patch(path, 1, err)) {
        list_add(pc->g->patched, name);
        if (!had_bak) list_add(pc->g->created, bak);
        say(pc->report, "DirectDraw: patched %s (backup %s).", name, bak);
    } else {
        pc->ok = 0;
        say(pc->report, "DirectDraw: could not patch %s: %s", name, err);
    }
}

static void adopt_patched(const char *name, void *ctx) { list_add(((Game *)ctx)->patched, name); }

int game_install_api(Game *g, int api, const char *newname, const char *appdir, const Detect *d, char *report)
{
    const ApiInfo *a = &g_apis[api];
    char src[MAX_PATH], dst[MAX_PATH], moved[MAX_PATH], ini[MAX_PATH];

    path_join(src, appdir, a->file);
    path_join(dst, g->folder, a->file);
    if (!file_exists(src)) { say(report, "%s: %s is missing from the Retrometer 9x folder.", a->label, a->file); return 0; }

    if (api == API_DDRAW) {
        PatchCtx pc;
        if (!d->ddrawFiles[0] && !d->fpsddFiles[0]) {
            say(report, "%s: nothing in this folder imports DDRAW.DLL, so it can't be hooked.%s", a->label, "");
            return 0;
        }
        if (!file_is_ours(dst) && !CopyFileA(src, dst, FALSE)) { say(report, "%s: could not copy %s.", a->label, a->file); return 0; }
        list_each(d->fpsddFiles, adopt_patched, g);
        pc.g = g; pc.report = report; pc.ok = 1;
        list_each(d->ddrawFiles, patch_one, &pc);
        if (d->ownDll[API_DDRAW]) say(report, "%s: the game's own ddraw.dll stays as it is; fpsdd.dll uses it.%s", a->label, "");
        g->installed |= 1u << api;
        return pc.ok;
    }

    if (file_exists(dst) && !file_is_ours(dst)) {
        /* the game ships its own driver: move it aside */
        if (!newname || !newname[0]) { say(report, "%s: no new name given for the game's %s.", a->label, a->file); return 0; }
        path_join(moved, g->folder, newname);
        if (file_exists(moved)) { say(report, "%s: %s already exists; choose another name.", a->label, newname); return 0; }
        if (!MoveFileA(dst, moved)) { say(report, "%s: could not rename %s (is the game running?).", a->label, a->file); return 0; }
        lstrcpyA(g->renamed[api], newname);
        say3(report, "%s: game's own %s renamed to %s.", a->label, a->file, newname);
        if (!istr_eq(newname, a->alt)) {
            /* not the name the proxy looks for by default: point it there */
            track_ini(g);
            ini_path(g, ini);
            WritePrivateProfileStringA(a->section, "Real", moved, ini);
            WritePrivateProfileStringA(NULL, NULL, NULL, ini);
        }
    } else if (file_is_ours(dst) && !g->renamed[api][0] && exists_in(g->folder, a->alt)) {
        lstrcpyA(g->renamed[api], a->alt);   /* adopting a hand install */
    }

    if (!CopyFileA(src, dst, FALSE)) { say(report, "%s: could not copy %s.", a->label, a->file); return 0; }
    if (a->local[0] && !exists_in(g->folder, a->local)) list_add(g->created, a->local);
    g->installed |= 1u << api;
    say(report, "%s: overlay installed (%s).", a->label, a->file);
    return 1;
}

static void unpatch_one(const char *name, void *ctx)
{
    PatchCtx *pc = (PatchCtx *)ctx;
    char path[MAX_PATH], bak[MAX_PATH + 8], err[300];
    path_join(path, pc->g->folder, name);
    if (pe_ddraw_state(path) == PE_FPSDD && !pe_ddraw_patch(path, 0, err)) {
        pc->ok = 0;
        say(pc->report, "DirectDraw: could not restore %s: %s", name, err);
        return;
    }
    lstrcpyA(bak, name);
    lstrcatA(bak, ".rmbak");
    if (list_has(pc->g->created, bak)) {
        char bp[MAX_PATH];
        path_join(bp, pc->g->folder, bak);
        DeleteFileA(bp);
        list_remove(pc->g->created, bak);
    }
}

int game_remove_api(Game *g, int api, char *report)
{
    const ApiInfo *a = &g_apis[api];
    char dst[MAX_PATH], moved[MAX_PATH], ini[MAX_PATH];

    path_join(dst, g->folder, a->file);
    if (api == API_DDRAW) {
        PatchCtx pc;
        pc.g = g; pc.report = report; pc.ok = 1;
        list_each(g->patched, unpatch_one, &pc);
        if (!pc.ok) { say(report, "%s: left installed because a file couldn't be restored.%s", a->label, ""); return 0; }
        g->patched[0] = 0;
        if (file_is_ours(dst) && !DeleteFileA(dst)) { say(report, "%s: could not delete %s.", a->label, a->file); return 0; }
        g->installed &= ~(1u << api);
        say(report, "%s: removed, game files restored.%s", a->label, "");
        return 1;
    }

    if (file_exists(dst) && file_is_ours(dst) && !DeleteFileA(dst)) {
        say(report, "%s: could not delete %s (is the game running?).", a->label, a->file);
        return 0;
    }
    if (g->renamed[api][0]) {
        path_join(moved, g->folder, g->renamed[api]);
        if (file_exists(moved) && !file_exists(dst) && MoveFileA(moved, dst))
            say(report, "%s: game's own %s restored.", a->label, a->file);
        else if (file_exists(moved))
            say(report, "%s: could not rename %s back; do it by hand.", a->label, g->renamed[api]);
        g->renamed[api][0] = 0;
    }
    ini_path(g, ini);
    if (file_exists(ini)) {
        WritePrivateProfileStringA(a->section, NULL, NULL, ini);
        WritePrivateProfileStringA(NULL, NULL, NULL, ini);
    }
    if (a->local[0] && list_has(g->created, a->local)) {
        char lp[MAX_PATH];
        path_join(lp, g->folder, a->local);
        DeleteFileA(lp);
        list_remove(g->created, a->local);
    }
    g->installed &= ~(1u << api);
    say(report, "%s: overlay removed.%s", a->label, "");
    return 1;
}

static void delete_created(const char *name, void *ctx)
{
    char p[MAX_PATH];
    path_join(p, ((Game *)ctx)->folder, name);
    DeleteFileA(p);
}

int game_remove_all(Game *g, char *report)
{
    int a, ok = 1;
    char p[MAX_PATH];
    for (a = 0; a < NAPI; a++)
        if (g->installed & (1u << a)) ok &= game_remove_api(g, a, report);
    if (!ok) return 0;
    list_each(g->created, delete_created, g);   /* retrometer.ini, driver copies, backups */
    g->created[0] = 0;
    path_join(p, g->folder, "retrometer.log");
    DeleteFileA(p);
    return 1;
}

typedef struct { const Game *g; char *out; int problems; } CheckCtx;

static void check_patched(const char *name, void *ctx)
{
    CheckCtx *cc = (CheckCtx *)ctx;
    char path[MAX_PATH];
    path_join(path, cc->g->folder, name);
    if (pe_ddraw_state(path) != PE_FPSDD) {
        if (cc->out[0]) lstrcatA(cc->out, "; ");
        lstrcatA(cc->out, name);
        lstrcatA(cc->out, " was replaced (not patched)");
    }
}

void game_check(const Game *g, char *out)
{
    CheckCtx cc;
    char p[MAX_PATH];
    int a;
    out[0] = 0;
    if (!file_exists(g->folder)) { lstrcpyA(out, "Game folder not found"); return; }
    for (a = 0; a < NAPI; a++) {
        if (!(g->installed & (1u << a))) continue;
        path_join(p, g->folder, g_apis[a].file);
        if (!file_exists(p) || !file_is_ours(p)) {
            if (out[0]) lstrcatA(out, "; ");
            lstrcatA(out, g_apis[a].file);
            lstrcatA(out, file_exists(p) ? " was replaced by the game" : " is missing");
        }
        if (g->renamed[a][0] && !exists_in(g->folder, g->renamed[a])) {
            if (out[0]) lstrcatA(out, "; ");
            lstrcatA(out, g->renamed[a]);
            lstrcatA(out, " is missing");
        }
    }
    cc.g = g; cc.out = out; cc.problems = 0;
    if (g->installed & (1u << API_DDRAW)) list_each(g->patched, check_patched, &cc);
}

static void repatch_one(const char *name, void *ctx)
{
    PatchCtx *pc = (PatchCtx *)ctx;
    char path[MAX_PATH], err[300];
    path_join(path, pc->g->folder, name);
    if (pe_ddraw_state(path) == PE_DDRAW) {
        if (pe_ddraw_patch(path, 1, err)) { say(pc->report, "DirectDraw: re-patched %s.%s", name, ""); pc->ok++; }
        else say(pc->report, "DirectDraw: could not re-patch %s: %s", name, err);
    }
}

int game_repair_conflict(const Game *g)
{
    int a;
    char p[MAX_PATH];
    for (a = 0; a < API_DDRAW; a++) {
        if (!(g->installed & (1u << a)) || !g->renamed[a][0]) continue;
        path_join(p, g->folder, g_apis[a].file);
        if (file_exists(p) && !file_is_ours(p) && exists_in(g->folder, g->renamed[a])) return 1;
    }
    return 0;
}

int game_repair(Game *g, const char *appdir, int replace_kept, char *report)
{
    int a, fixed = 0;
    for (a = 0; a < NAPI; a++) {
        const ApiInfo *ai = &g_apis[a];
        char src[MAX_PATH], dst[MAX_PATH], moved[MAX_PATH], nn[64];
        if (!(g->installed & (1u << a))) continue;
        path_join(src, appdir, ai->file);
        path_join(dst, g->folder, ai->file);
        if (file_exists(dst) && file_is_ours(dst)) continue;
        if (file_exists(dst) && a != API_DDRAW) {
            /* the game (or its patch) put its own driver back */
            if (g->renamed[a][0] && exists_in(g->folder, g->renamed[a])) {
                if (!replace_kept) {
                    say(report, "%s: left alone; the game's new %s was not moved.", ai->label, ai->file);
                    continue;
                }
                path_join(moved, g->folder, g->renamed[a]);
                if (!DeleteFileA(moved)) { say(report, "%s: could not replace %s.", ai->label, g->renamed[a]); continue; }
                say(report, "%s: old kept copy %s replaced by the game's newer one.", ai->label, g->renamed[a]);
            }
            if (g->renamed[a][0]) lstrcpyA(nn, g->renamed[a]); else suggest_rename(g->folder, a, nn);
            path_join(moved, g->folder, nn);
            if (!MoveFileA(dst, moved)) { say(report, "%s: could not rename the game's %s.", ai->label, ai->file); continue; }
            lstrcpyA(g->renamed[a], nn);
            say3(report, "%s: game's new %s renamed to %s.", ai->label, ai->file, nn);
        }
        if (CopyFileA(src, dst, FALSE)) { say(report, "%s: %s put back.", ai->label, ai->file); fixed++; }
        else say(report, "%s: could not copy %s.", ai->label, ai->file);
    }
    if (g->installed & (1u << API_DDRAW)) {
        PatchCtx pc;
        pc.g = g; pc.report = report; pc.ok = 0;
        list_each(g->patched, repatch_one, &pc);
        fixed += pc.ok;
    }
    return fixed;
}
