/* retrometer game database, detection and install/remove engine. */
#ifndef GAMEDB_H
#define GAMEDB_H

#include <windows.h>

#define NAPI       5
#define API_DDRAW  4
#define MAX_GAMES  64
#define LIST_LEN   600

typedef struct {
    const char *label;     /* "Glide 2" */
    const char *file;      /* our proxy's file name in the game folder */
    const char *own;       /* name of a game-supplied driver we may need to move aside ("" = none) */
    const char *alt;       /* default new name for it (what the proxy looks for first) */
    const char *section;   /* retrometer.ini section for Real= */
    const char *local;     /* copy of the system driver the proxy makes ("" = none) */
    const char *import;    /* DLL name in import tables */
    const char *probe;     /* string looked for in game files for run-time loading */
} ApiInfo;
extern const ApiInfo g_apis[NAPI];

/* One managed game. Lists are comma separated file names inside folder. */
typedef struct {
    char name[64];
    char exe[MAX_PATH];        /* full path of the main exe */
    char folder[MAX_PATH];     /* with trailing '\' */
    int  useGlobal;            /* settings follow "All games" */
    unsigned installed;        /* bit per API */
    char renamed[NAPI][64];    /* game's own driver now lives under this name ("" = not renamed) */
    char patched[LIST_LEN];    /* files whose DDRAW.DLL import we redirected */
    char created[LIST_LEN];    /* files that didn't exist before us; deleted on removal */
} Game;

typedef struct {
    int  count;
    Game g[MAX_GAMES];
} GameDb;

/* What a folder scan found. */
#define DET_NONE     0
#define DET_RUNTIME  1     /* name found inside a game file: loaded with LoadLibrary */
#define DET_IMPORT   2     /* in an import table */
typedef struct {
    int  level[NAPI];
    char where[NAPI][120];     /* which files */
    int  ownDll[NAPI];         /* the folder holds a driver of this name that isn't ours */
    int  oursPresent[NAPI];    /* our proxy already there (installed by hand/older version) */
    char ddrawFiles[LIST_LEN]; /* files importing DDRAW.DLL (to patch) */
    char fpsddFiles[LIST_LEN]; /* files already patched to FPSDD.DLL */
} Detect;

void db_load(const char *path, GameDb *db);
void db_save(const char *path, const GameDb *db);
int  db_find_exe(const GameDb *db, const char *exe);

void detect_folder(const char *folder, Detect *d);
int  file_is_ours(const char *path);

/* Suggested new name for a game's own driver for API i: "<name>a.dll", or the
 * next free letter if that's taken. */
void suggest_rename(const char *folder, int api, char *out);

/* Install / remove one API for a game, recording every change in g.
 * newname: for a game-supplied driver, what to rename it to.
 * Appends a human-readable line to report. Returns 1 on success. */
int  game_install_api(Game *g, int api, const char *newname, const char *appdir, const Detect *d, char *report);
int  game_remove_api(Game *g, int api, char *report);
/* Remove everything (all APIs, ini, log, backups we made). */
int  game_remove_all(Game *g, char *report);
/* Re-copy missing proxies / re-patch files a game update undid. Returns problems fixed. */
int  game_repair(Game *g, const char *appdir, int replace_kept, char *report);
/* A game put its own driver back while our renamed copy of the old one still exists. */
int  game_repair_conflict(const Game *g);
/* Problems with an installed game, as text ("" = OK). */
void game_check(const Game *g, char *out);

/* helpers */
void path_join(char *out, const char *dir, const char *file);
int  file_exists(const char *p);
int  list_has(const char *list, const char *item);
void list_add(char *list, const char *item);
void list_remove(char *list, const char *item);
int  istr_eq(const char *a, const char *b);

#endif
