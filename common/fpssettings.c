#include "fpssettings.h"

static const char SEC[] = "FPS";

const FpsKeyName fps_key_names[] = {
    {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5}, {"F6", VK_F6},
    {"F7", VK_F7}, {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
    {"Scroll Lock", VK_SCROLL}, {"Pause", VK_PAUSE}, {"Num Lock", VK_NUMLOCK},
    {"Insert", VK_INSERT}, {"Delete", VK_DELETE}, {"Home", VK_HOME}, {"End", VK_END},
    {"Page Up", VK_PRIOR}, {"Page Down", VK_NEXT},
    {"Num 0", VK_NUMPAD0}, {"Num 1", VK_NUMPAD1}, {"Num 2", VK_NUMPAD2}, {"Num 3", VK_NUMPAD3},
    {"Num 4", VK_NUMPAD4}, {"Num 5", VK_NUMPAD5}, {"Num 6", VK_NUMPAD6}, {"Num 7", VK_NUMPAD7},
    {"Num 8", VK_NUMPAD8}, {"Num 9", VK_NUMPAD9}, {"Num *", VK_MULTIPLY}, {"Num /", VK_DIVIDE},
    {"Num +", VK_ADD}, {"Num -", VK_SUBTRACT}, {"`", 0xC0},
};
const int fps_key_name_count = sizeof fps_key_names / sizeof fps_key_names[0];

static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/* compare ignoring case and spaces ("PageUp" == "Page Up", "scroll" prefix ok handled by aliases) */
static int keyeq(const char *a, const char *b)
{
    for (;;) {
        while (*a == ' ') a++;
        while (*b == ' ') b++;
        if (up(*a) != up(*b)) return 0;
        if (!*a) return 1;
        a++; b++;
    }
}

DWORD fps_parse_hex(const char *s, DWORD def)
{
    DWORD v = 0; int any = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    else if (*s == '#') s++;
    for (; *s; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = (v << 4) | d; any = 1;
    }
    return any ? v : def;
}

static int parse_one_key(const char *s)
{
    int i;
    static const FpsKeyName alias[] = {
        {"SCROLL", VK_SCROLL}, {"PGUP", VK_PRIOR}, {"PGDN", VK_NEXT}, {"NUMLOCK", VK_NUMLOCK},
        {"INS", VK_INSERT}, {"DEL", VK_DELETE},
    };
    for (i = 0; i < fps_key_name_count; i++) if (keyeq(s, fps_key_names[i].name)) return fps_key_names[i].vk;
    for (i = 0; i < (int)(sizeof alias / sizeof alias[0]); i++) if (keyeq(s, alias[i].name)) return alias[i].vk;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return (int)fps_parse_hex(s, 0);
    if (s[0] >= '0' && s[0] <= '9' && s[1]) {               /* decimal VK code (2+ digits) */
        int v = 0;
        for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
        return v;
    }
    if (s[0] && !s[1]) {                                     /* single letter or digit */
        int c = up(s[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    return 0;
}

int fps_text_to_key(const char *text, int *vk, int *mods)
{
    char part[32];
    int m = 0, n;
    *vk = 0;
    for (;;) {
        n = 0;
        while (*text == ' ') text++;
        while (*text && *text != '+' && n < 31) part[n++] = *text++;
        /* "Num +" : a trailing '+' right after "Num " belongs to the key name */
        if (*text == '+' && n >= 3 && up(part[0]) == 'N' && up(part[1]) == 'U' && up(part[2]) == 'M' &&
            (text[1] == 0 || text[1] == ' ')) part[n++] = *text++;
        while (n && part[n - 1] == ' ') n--;
        part[n] = 0;
        if (keyeq(part, "CTRL") || keyeq(part, "CONTROL")) m |= FPS_MOD_CTRL;
        else if (keyeq(part, "ALT")) m |= FPS_MOD_ALT;
        else if (keyeq(part, "SHIFT")) m |= FPS_MOD_SHIFT;
        else if (part[0]) *vk = parse_one_key(part);
        if (*text != '+') break;
        text++;
    }
    *mods = m;
    return *vk != 0;
}

void fps_key_to_text(int vk, int mods, char *out)
{
    int i;
    out[0] = 0;
    if (mods & FPS_MOD_CTRL) lstrcatA(out, "Ctrl+");
    if (mods & FPS_MOD_ALT) lstrcatA(out, "Alt+");
    if (mods & FPS_MOD_SHIFT) lstrcatA(out, "Shift+");
    for (i = 0; i < fps_key_name_count; i++)
        if (fps_key_names[i].vk == vk) { lstrcatA(out, fps_key_names[i].name); return; }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        char c[2]; c[0] = (char)vk; c[1] = 0; lstrcatA(out, c); return;
    }
    wsprintfA(out + lstrlenA(out), "0x%02X", vk);
}

void fps_settings_defaults(FpsSettings *s)
{
    s->vk = VK_F11; s->mods = 0; s->visible = 1;
    s->corner = FPS_TOPLEFT; s->x = 8; s->y = 8; s->scale = 3;
    s->color = 0xFFFF00; s->bg = 1; s->bgcolor = 0x000000;
    s->label = 0; s->frametime = 0; s->interval = 500; s->log = 0;
    s->capfps = 60; s->capon = 0; s->capvk = VK_F12; s->capmods = 0; s->capmode = FPS_CAP_TOGGLE;
    s->caplist[0] = 30; s->caplist[1] = 60; s->caplist[2] = 85; s->capcount = 3;
}

static int clamp_fps(int v) { return v < 10 ? 10 : v > 500 ? 500 : v; }

void fps_text_to_caplist(const char *t, FpsSettings *s)
{
    int n = 0, v, any;
    while (*t && n < 6) {
        v = 0; any = 0;
        while (*t && (*t < '0' || *t > '9')) t++;
        while (*t >= '0' && *t <= '9') { v = v * 10 + (*t - '0'); t++; any = 1; }
        if (any && v > 0) s->caplist[n++] = clamp_fps(v);
    }
    if (n) s->capcount = n;
}

void fps_caplist_to_text(const FpsSettings *s, char *out)
{
    int i;
    out[0] = 0;
    for (i = 0; i < s->capcount; i++) wsprintfA(out + lstrlenA(out), i ? ",%d" : "%d", s->caplist[i]);
}

static int geti(const char *ini, const char *k, int def) { return (int)GetPrivateProfileIntA(SEC, k, def, ini); }

void fps_settings_load(const char *ini, FpsSettings *s)
{
    char tmp[64];
    int vk, mods;
    fps_settings_defaults(s);
    GetPrivateProfileStringA(SEC, "Hotkey", "F11", tmp, sizeof tmp, ini);
    if (fps_text_to_key(tmp, &vk, &mods)) { s->vk = vk; s->mods = mods; }
    s->visible = geti(ini, "StartVisible", 1);
    GetPrivateProfileStringA(SEC, "Corner", "TopLeft", tmp, sizeof tmp, ini);
    if (keyeq(tmp, "TopRight")) s->corner = FPS_TOPRIGHT;
    else if (keyeq(tmp, "BottomLeft")) s->corner = FPS_BOTTOMLEFT;
    else if (keyeq(tmp, "BottomRight")) s->corner = FPS_BOTTOMRIGHT;
    else s->corner = FPS_TOPLEFT;
    s->x = geti(ini, "X", 8);
    s->y = geti(ini, "Y", 8);
    s->scale = geti(ini, "Scale", 3);
    if (s->scale < 1) s->scale = 1;
    if (s->scale > 8) s->scale = 8;
    GetPrivateProfileStringA(SEC, "Color", "FFFF00", tmp, sizeof tmp, ini);
    s->color = fps_parse_hex(tmp, 0xFFFF00) & 0xFFFFFF;
    s->bg = geti(ini, "Background", 1);
    GetPrivateProfileStringA(SEC, "BackgroundColor", "000000", tmp, sizeof tmp, ini);
    s->bgcolor = fps_parse_hex(tmp, 0) & 0xFFFFFF;
    s->label = geti(ini, "ShowLabel", 0);
    s->frametime = geti(ini, "ShowFrameTime", 0);
    s->interval = geti(ini, "UpdateMs", 500);
    if (s->interval < 100) s->interval = 100;
    if (s->interval > 5000) s->interval = 5000;
    s->log = geti(ini, "Log", 0);
    s->capfps = clamp_fps(geti(ini, "CapFps", 60));
    s->capon = geti(ini, "CapOn", 0);
    GetPrivateProfileStringA(SEC, "CapHotkey", "F12", tmp, sizeof tmp, ini);
    if (!tmp[0] || keyeq(tmp, "None")) s->capvk = 0, s->capmods = 0;
    else if (fps_text_to_key(tmp, &vk, &mods)) { s->capvk = vk; s->capmods = mods; }
    GetPrivateProfileStringA(SEC, "CapHotkeyMode", "Toggle", tmp, sizeof tmp, ini);
    s->capmode = keyeq(tmp, "Cycle") ? FPS_CAP_CYCLE : FPS_CAP_TOGGLE;
    GetPrivateProfileStringA(SEC, "CapCycle", "30,60,85", tmp, sizeof tmp, ini);
    fps_text_to_caplist(tmp, s);
}

static void puti(const char *ini, const char *k, int v)
{
    char b[16];
    wsprintfA(b, "%d", v);
    WritePrivateProfileStringA(SEC, k, b, ini);
}

void fps_settings_save(const char *ini, const FpsSettings *s)
{
    static const char *const corners[] = {"TopLeft", "TopRight", "BottomLeft", "BottomRight"};
    char b[64];
    fps_key_to_text(s->vk, s->mods, b);
    WritePrivateProfileStringA(SEC, "Hotkey", b, ini);
    puti(ini, "StartVisible", s->visible);
    WritePrivateProfileStringA(SEC, "Corner", corners[s->corner & 3], ini);
    puti(ini, "X", s->x);
    puti(ini, "Y", s->y);
    puti(ini, "Scale", s->scale);
    wsprintfA(b, "%06lX", s->color);
    WritePrivateProfileStringA(SEC, "Color", b, ini);
    puti(ini, "Background", s->bg);
    wsprintfA(b, "%06lX", s->bgcolor);
    WritePrivateProfileStringA(SEC, "BackgroundColor", b, ini);
    puti(ini, "ShowLabel", s->label);
    puti(ini, "ShowFrameTime", s->frametime);
    puti(ini, "UpdateMs", s->interval);
    puti(ini, "Log", s->log);
    puti(ini, "CapFps", s->capfps);
    puti(ini, "CapOn", s->capon);
    if (s->capvk) fps_key_to_text(s->capvk, s->capmods, b); else lstrcpyA(b, "None");
    WritePrivateProfileStringA(SEC, "CapHotkey", b, ini);
    WritePrivateProfileStringA(SEC, "CapHotkeyMode", s->capmode == FPS_CAP_CYCLE ? "Cycle" : "Toggle", ini);
    fps_caplist_to_text(s, b);
    WritePrivateProfileStringA(SEC, "CapCycle", b, ini);
    WritePrivateProfileStringA(NULL, NULL, NULL, ini);   /* flush the Win9x ini cache */
}

/* ---------- 5-row bitmap font ---------- */
typedef struct { char ch; unsigned char w; unsigned char rows[5]; } Glyph;
static const Glyph glyphs[] = {
    {'0', 3, {7,5,5,5,7}}, {'1', 3, {2,6,2,2,7}}, {'2', 3, {7,1,7,4,7}}, {'3', 3, {7,1,7,1,7}},
    {'4', 3, {5,5,7,1,1}}, {'5', 3, {7,4,7,1,7}}, {'6', 3, {7,4,7,5,7}}, {'7', 3, {7,1,1,1,1}},
    {'8', 3, {7,5,7,5,7}}, {'9', 3, {7,5,7,1,7}},
    {'F', 3, {7,4,6,4,4}}, {'P', 3, {7,5,7,4,4}}, {'S', 3, {3,4,2,1,6}},
    {'C', 3, {7,4,4,4,7}}, {'A', 3, {2,5,7,5,5}}, {'O', 3, {7,5,5,5,7}},
    {'M', 5, {17,27,21,17,17}}, {'.', 1, {0,0,0,0,1}}, {' ', 1, {0,0,0,0,0}},
};

static const Glyph *glyph(char c)
{
    int i;
    for (i = 0; i < (int)(sizeof glyphs / sizeof glyphs[0]); i++) if (glyphs[i].ch == c) return &glyphs[i];
    return &glyphs[sizeof glyphs / sizeof glyphs[0] - 1];
}

void fps_format(const FpsSettings *s, int fps, int ms10, char *out)
{
    if (fps < 0) fps = 0;
    if (fps > 9999) fps = 9999;
    if (ms10 < 0) ms10 = 0;
    if (ms10 > 99999) ms10 = 99999;
    wsprintfA(out, "%d", fps);
    if (s->label) lstrcatA(out, " FPS");
    if (s->frametime) wsprintfA(out + lstrlenA(out), "  %d.%dMS", ms10 / 10, ms10 % 10);
}

static int text_cols(const char *t)
{
    int c = 0;
    for (; *t; t++) c += glyph(*t)->w + 1;
    return c ? c - 1 : 0;
}

int fps_layout(const FpsSettings *s, const char *text, int sw, int sh, int ox, int oy,
               FpsRect *out, int max)
{
    int sc = s->scale, cnt = 0, row, col, cx;
    int w = (text_cols(text) + 2) * sc, h = 7 * sc;
    int x0, y0;
    const char *t;

    x0 = (s->corner == FPS_TOPRIGHT || s->corner == FPS_BOTTOMRIGHT) ? sw - s->x - w : s->x;
    y0 = (s->corner == FPS_BOTTOMLEFT || s->corner == FPS_BOTTOMRIGHT) ? sh - s->y - h : s->y;
    if (x0 > sw - w) x0 = sw - w;     /* keep it on screen */
    if (y0 > sh - h) y0 = sh - h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    x0 += ox; y0 += oy;

    if (s->bg && cnt < max) {
        out[cnt].x = x0; out[cnt].y = y0; out[cnt].w = w; out[cnt].h = h; out[cnt].rgb = s->bgcolor;
        cnt++;
    }
    cx = x0 + sc;
    for (t = text; *t; t++) {
        const Glyph *g = glyph(*t);
        for (row = 0; row < 5; row++) {
            unsigned bits = g->rows[row];
            col = 0;
            while (col < g->w) {             /* merge horizontal runs */
                int start;
                if (!(bits & (1u << (g->w - 1 - col)))) { col++; continue; }
                start = col;
                while (col < g->w && (bits & (1u << (g->w - 1 - col)))) col++;
                if (cnt >= max) return cnt;
                out[cnt].x = cx + start * sc;
                out[cnt].y = y0 + (row + 1) * sc;
                out[cnt].w = (col - start) * sc;
                out[cnt].h = sc;
                out[cnt].rgb = s->color;
                cnt++;
            }
        }
        cx += (g->w + 1) * sc;
    }
    return cnt;
}
