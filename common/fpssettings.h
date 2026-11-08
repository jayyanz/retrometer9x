/* Retrometer 9x settings + text layout, shared by the proxy DLLs and retrometer.exe. */
#ifndef FPSSETTINGS_H
#define FPSSETTINGS_H

#include <windows.h>

#define FPS_MOD_CTRL  1
#define FPS_MOD_ALT   2
#define FPS_MOD_SHIFT 4

enum { FPS_TOPLEFT, FPS_TOPRIGHT, FPS_BOTTOMLEFT, FPS_BOTTOMRIGHT };

typedef struct {
    int   vk;          /* toggle key (Windows virtual key) */
    int   mods;        /* FPS_MOD_* that must be held */
    int   visible;     /* shown at start */
    int   corner;      /* FPS_TOPLEFT.. */
    int   x, y;        /* offset from that corner, pixels */
    int   scale;       /* 1..8: pixels per font dot */
    DWORD color;       /* 0xRRGGBB */
    int   bg;          /* draw a box behind the text */
    DWORD bgcolor;     /* 0xRRGGBB */
    int   label;       /* append " FPS" */
    int   frametime;   /* append frame time in ms */
    int   interval;    /* update period in ms */
    int   log;
    /* frame rate cap */
    int   capfps;      /* target fps when the cap is on (10..500) */
    int   capon;       /* cap on when the game starts */
    int   capvk, capmods; /* cap hotkey (0 = none) */
    int   capmode;     /* FPS_CAP_TOGGLE or FPS_CAP_CYCLE */
    int   caplist[6];  /* cycle steps (fps), capcount used */
    int   capcount;
} FpsSettings;

enum { FPS_CAP_TOGGLE, FPS_CAP_CYCLE };

/* "30,60,85" <-> caplist */
void fps_caplist_to_text(const FpsSettings *s, char *out);
void fps_text_to_caplist(const char *t, FpsSettings *s);

typedef struct {
    int x, y, w, h;
    DWORD rgb;         /* 0x00RRGGBB */
} FpsRect;

#define FPS_MAX_RECTS 160

void fps_settings_defaults(FpsSettings *s);
void fps_settings_load(const char *ini, FpsSettings *s);
void fps_settings_save(const char *ini, const FpsSettings *s);

/* "Ctrl+F11" style text for a key + modifiers, and back. */
void fps_key_to_text(int vk, int mods, char *out);
int  fps_text_to_key(const char *text, int *vk, int *mods);

/* Named keys offered by retrometer (and accepted in the ini). */
typedef struct { const char *name; int vk; } FpsKeyName;
extern const FpsKeyName fps_key_names[];
extern const int fps_key_name_count;

/* Build the overlay text, e.g. "60 FPS  16.7MS". ms10 = frame time in tenths of ms. */
void fps_format(const FpsSettings *s, int fps, int ms10, char *out);

/* Lay the text out as solid rectangles for a sw x sh screen, shifted by (ox, oy). */
int  fps_layout(const FpsSettings *s, const char *text, int sw, int sh, int ox, int oy,
                FpsRect *out, int max);

DWORD fps_parse_hex(const char *s, DWORD def);

#endif
