/*
 * main.c - Novela visual: parte grafica y controles (SDL2) para PC y PS Vita.
 *
 * Todo lo que entiende el guion esta en vn.c. Aqui solo se dibuja, se oye
 * y se leen los botones.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vn.h"

#ifdef __vita__
#include <psp2/io/stat.h>
#include <psp2/touch.h>
/* Memoria normal (malloc) disponible para la app. */
int _newlib_heap_size_user = 64 * 1024 * 1024;
#endif

#define SCREEN_W 960
#define SCREEN_H 544

/* Cuadro de dialogo */
#define BOX_X 30
#define BOX_Y 364
#define BOX_W 900
#define BOX_H 154
#define TEXT_X 60
#define TEXT_Y 378
#define TEXT_W 810
#define PAGE_LINES 4

#define LINE_LEN 512
#define MAX_LINES 96

#define FADE_FRAMES 24

/* ------------------------------------------------------------------ */
/* Rutas                                                               */
/* ------------------------------------------------------------------ */

#define MAX_ROOTS 3
static char g_roots[MAX_ROOTS][512];
static int  g_nroots;
static char g_savepath[640];
static char g_pathbuf[1100];

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Busca primero en la carpeta de edicion rapida y luego dentro del juego. */
static const char *resolve_asset(const char *rel, void *ud)
{
    (void)ud;
    for (int i = 0; i < g_nroots; i++) {
        snprintf(g_pathbuf, sizeof(g_pathbuf), "%s%s", g_roots[i], rel);
        if (file_exists(g_pathbuf)) return g_pathbuf;
    }
    return NULL;
}

static void init_paths(void)
{
#ifdef __vita__
    snprintf(g_roots[0], sizeof(g_roots[0]), "ux0:data/NovelaVisual/");
    snprintf(g_roots[1], sizeof(g_roots[1]), "app0:assets/");
    g_nroots = 2;
    sceIoMkdir("ux0:data/NovelaVisual", 0777);
    snprintf(g_savepath, sizeof(g_savepath), "ux0:data/NovelaVisual/partida.sav");
#else
    int n = 0;
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(g_roots[n++], sizeof(g_roots[0]), "%sassets/", base);
        SDL_free(base);
    }
    snprintf(g_roots[n++], sizeof(g_roots[0]), "assets/");
    g_nroots = n;
    char *pref = SDL_GetPrefPath("NovelaVisual", "novela");
    if (pref) {
        snprintf(g_savepath, sizeof(g_savepath), "%spartida.sav", pref);
        SDL_free(pref);
    } else {
        snprintf(g_savepath, sizeof(g_savepath), "partida.sav");
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Globales                                                            */
/* ------------------------------------------------------------------ */

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static unsigned      g_frame;
static int           g_running = 1;

enum { FONT_TEXT = 0, FONT_NAME, FONT_SMALL, FONT_TITLE, FONT_SUB, FONT_COUNT };
static TTF_Font *g_fonts[FONT_COUNT];

static VnScript g_script;
static VnState  g_st;

enum { MODE_ERRORS, MODE_TITLE, MODE_TEXT, MODE_MENU };
static int g_mode = MODE_TITLE;

/* Configuracion (config.txt) */
static char g_title[128] = "Novela visual";
static char g_subtitle[160] = "";
static char g_title_bg[VN_NAME] = "";
static char g_title_music[VN_NAME] = "";

static char g_toast[VN_ERRLEN] = "";
static int  g_toast_frames;

#ifdef __vita__
#define HINT_TITLE "X: aceptar  |  O: ocultar cuadro  |  START: guardar  |  TRIÁNGULO: título  |  SELECT: recargar"
#else
#define HINT_TITLE "Enter / clic: aceptar  |  X: ocultar cuadro  |  S: guardar  |  Esc: título  |  F5: recargar"
#endif

static void toast(const char *msg)
{
    snprintf(g_toast, sizeof(g_toast), "%s", msg);
    g_toast_frames = 60 * 4;
}

/* ------------------------------------------------------------------ */
/* Utilidades UTF-8 y colores                                          */
/* ------------------------------------------------------------------ */

static int utf8_len(const char *s)
{
    int n = 0;
    for (; *s; s++) if ((((unsigned char)*s) & 0xC0) != 0x80) n++;
    return n;
}

/* Bytes que ocupan los primeros ncp caracteres. */
static size_t utf8_prefix(const char *s, int ncp)
{
    size_t i = 0;
    int c = 0;
    while (s[i]) {
        if ((((unsigned char)s[i]) & 0xC0) != 0x80) {
            if (c == ncp) break;
            c++;
        }
        i++;
    }
    return i;
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return *a == *b;
}

static int is_narrator(const char *sp)
{
    return !*sp || ieq(sp, "narrador") || ieq(sp, "narradora") || ieq(sp, "narracion") ||
           ieq(sp, "narración");
}

static unsigned hash_str(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++) h = h * 33u + (unsigned char)*s;
    return h;
}

static SDL_Color color_from_name(const char *name, int bright)
{
    float hue = (float)(hash_str(name) % 360u);
    float s = 0.55f, v = bright ? 0.80f : 0.55f;
    float c = v * s, hp = hue / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    float m = v - c;
    SDL_Color col;
    col.r = (Uint8)((r + m) * 255);
    col.g = (Uint8)((g + m) * 255);
    col.b = (Uint8)((b + m) * 255);
    col.a = 255;
    return col;
}

/* ------------------------------------------------------------------ */
/* Cache de texturas (imagenes)                                        */
/* ------------------------------------------------------------------ */

#define TEX_CACHE 24
typedef struct {
    int          used;
    char         key[VN_NAME + VN_SMALL + 16];
    SDL_Texture *tex;
    int          w, h;
    unsigned     stamp;
} TexEntry;
static TexEntry g_tex[TEX_CACHE];

static void tex_clear_all(void)
{
    for (int i = 0; i < TEX_CACHE; i++) {
        if (g_tex[i].tex) SDL_DestroyTexture(g_tex[i].tex);
        memset(&g_tex[i], 0, sizeof(g_tex[i]));
    }
}

/* Devuelve NULL si la imagen no existe (el juego dibuja un marcador). */
static TexEntry *tex_get(int kind, const char *a, const char *b)
{
    char rel[VN_NAME + VN_SMALL + 16];
    vn_asset_rel(kind, a, b, 0, rel, sizeof(rel));

    for (int i = 0; i < TEX_CACHE; i++) {
        if (g_tex[i].used && strcmp(g_tex[i].key, rel) == 0) {
            g_tex[i].stamp = g_frame;
            return g_tex[i].tex ? &g_tex[i] : NULL;
        }
    }

    int slot = -1;
    unsigned oldest = 0xFFFFFFFFu;
    for (int i = 0; i < TEX_CACHE; i++) {
        if (!g_tex[i].used) { slot = i; break; }
        if (g_tex[i].stamp < oldest) { oldest = g_tex[i].stamp; slot = i; }
    }
    if (g_tex[slot].tex) SDL_DestroyTexture(g_tex[slot].tex);
    memset(&g_tex[slot], 0, sizeof(g_tex[slot]));
    g_tex[slot].used = 1;
    g_tex[slot].stamp = g_frame;
    snprintf(g_tex[slot].key, sizeof(g_tex[slot].key), "%s", rel);

    for (int v = 0; v < vn_asset_variants(kind); v++) {
        char try_rel[VN_NAME + VN_SMALL + 16];
        vn_asset_rel(kind, a, b, v, try_rel, sizeof(try_rel));
        const char *path = resolve_asset(try_rel, NULL);
        if (!path) continue;
        SDL_Texture *t = IMG_LoadTexture(g_ren, path);
        if (t) {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_QueryTexture(t, NULL, NULL, &g_tex[slot].w, &g_tex[slot].h);
            g_tex[slot].tex = t;
            break;
        }
    }
    return g_tex[slot].tex ? &g_tex[slot] : NULL;
}

/* ------------------------------------------------------------------ */
/* Texto (con cache)                                                   */
/* ------------------------------------------------------------------ */

#define TXT_CACHE 80
typedef struct {
    char        *str;
    int          font;
    Uint32       color;
    SDL_Texture *tex;
    int          w, h;
    unsigned     stamp;
} TxtEntry;
static TxtEntry g_txt[TXT_CACHE];

static Uint32 pack_color(SDL_Color c)
{
    return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) | ((Uint32)c.b << 8) | c.a;
}

static void txt_clear_all(void)
{
    for (int i = 0; i < TXT_CACHE; i++) {
        if (g_txt[i].tex) SDL_DestroyTexture(g_txt[i].tex);
        free(g_txt[i].str);
        memset(&g_txt[i], 0, sizeof(g_txt[i]));
    }
}

static TxtEntry *txt_get(int font, const char *s, SDL_Color col)
{
    if (!s || !*s || !g_fonts[font]) return NULL;
    Uint32 pc = pack_color(col);
    for (int i = 0; i < TXT_CACHE; i++) {
        if (g_txt[i].str && g_txt[i].font == font && g_txt[i].color == pc && strcmp(g_txt[i].str, s) == 0) {
            g_txt[i].stamp = g_frame;
            return g_txt[i].tex ? &g_txt[i] : NULL;
        }
    }
    int slot = -1;
    unsigned oldest = 0xFFFFFFFFu;
    for (int i = 0; i < TXT_CACHE; i++) {
        if (!g_txt[i].str) { slot = i; break; }
        if (g_txt[i].stamp < oldest) { oldest = g_txt[i].stamp; slot = i; }
    }
    if (g_txt[slot].tex) SDL_DestroyTexture(g_txt[slot].tex);
    free(g_txt[slot].str);
    memset(&g_txt[slot], 0, sizeof(g_txt[slot]));

    g_txt[slot].str = (char *)malloc(strlen(s) + 1);
    if (!g_txt[slot].str) return NULL;
    strcpy(g_txt[slot].str, s);
    g_txt[slot].font = font;
    g_txt[slot].color = pc;
    g_txt[slot].stamp = g_frame;

    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_fonts[font], s, col);
    if (surf) {
        g_txt[slot].tex = SDL_CreateTextureFromSurface(g_ren, surf);
        g_txt[slot].w = surf->w;
        g_txt[slot].h = surf->h;
        SDL_FreeSurface(surf);
        if (g_txt[slot].tex) SDL_SetTextureBlendMode(g_txt[slot].tex, SDL_BLENDMODE_BLEND);
    }
    return g_txt[slot].tex ? &g_txt[slot] : NULL;
}

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

static int text_width(int font, const char *s)
{
    int w = 0;
    if (g_fonts[font] && s && *s) TTF_SizeUTF8(g_fonts[font], s, &w, NULL);
    return w;
}

static void draw_text(int font, const char *s, int x, int y, SDL_Color col, Uint8 alpha, int align)
{
    TxtEntry *t = txt_get(font, s, col);
    if (!t) return;
    SDL_Rect dst;
    dst.w = t->w;
    dst.h = t->h;
    dst.x = align == ALIGN_CENTER ? x - t->w / 2 : align == ALIGN_RIGHT ? x - t->w : x;
    dst.y = y;
    SDL_SetTextureAlphaMod(t->tex, alpha);
    SDL_RenderCopy(g_ren, t->tex, NULL, &dst);
}

static void fill_rect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Rect rc;
    rc.x = x; rc.y = y; rc.w = w; rc.h = h;
    SDL_SetRenderDrawColor(g_ren, r, g, b, a);
    SDL_RenderFillRect(g_ren, &rc);
}

static void frame_rect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Rect rc;
    rc.x = x; rc.y = y; rc.w = w; rc.h = h;
    SDL_SetRenderDrawColor(g_ren, r, g, b, a);
    SDL_RenderDrawRect(g_ren, &rc);
    rc.x++; rc.y++; rc.w -= 2; rc.h -= 2;
    SDL_RenderDrawRect(g_ren, &rc);
}

/* Parte un texto en renglones que quepan en maxw. Respeta los saltos \n. */
static int wrap_text(int font, const char *text, int maxw, char lines[][LINE_LEN], int maxlines)
{
    int nl = 0;
    char cur[LINE_LEN];
    int curlen = 0, last_space = -1;
    cur[0] = 0;

#define FLUSH_LINE(len_)                                         \
    do {                                                         \
        if (nl < maxlines) {                                     \
            memcpy(lines[nl], cur, (size_t)(len_));              \
            lines[nl][(len_)] = 0;                               \
            nl++;                                                \
        }                                                        \
    } while (0)

    for (const char *p = text; *p;) {
        int cplen = 1;
        unsigned char c0 = (unsigned char)*p;
        if (c0 >= 0xF0) cplen = 4;
        else if (c0 >= 0xE0) cplen = 3;
        else if (c0 >= 0xC0) cplen = 2;

        if (c0 == '\n') {
            FLUSH_LINE(curlen);
            curlen = 0; cur[0] = 0; last_space = -1;
            p++;
            continue;
        }
        if (c0 == ' ' && curlen == 0) { p++; continue; }       /* sin espacios al inicio */
        if (curlen + cplen >= LINE_LEN - 1) {                   /* renglon absurdamente largo */
            FLUSH_LINE(curlen);
            curlen = 0; cur[0] = 0; last_space = -1;
        }

        int before = curlen;
        for (int k = 0; k < cplen && p[k]; k++) cur[curlen++] = p[k];
        cur[curlen] = 0;
        if (c0 == ' ') last_space = before;
        p += cplen;

        if (before > 0 && text_width(font, cur) > maxw) {
            if (last_space >= 0) {
                int rest = curlen - (last_space + 1);
                char tail[LINE_LEN];
                memcpy(tail, cur + last_space + 1, (size_t)rest);
                tail[rest] = 0;
                FLUSH_LINE(last_space);
                memcpy(cur, tail, (size_t)rest + 1);
                curlen = rest;
                last_space = -1;
            } else {
                char tail[8];
                int tl = curlen - before;
                memcpy(tail, cur + before, (size_t)tl);
                FLUSH_LINE(before);
                memcpy(cur, tail, (size_t)tl);
                cur[tl] = 0;
                curlen = tl;
                last_space = -1;
            }
        }
    }
    if (curlen > 0 || nl == 0) FLUSH_LINE(curlen);
#undef FLUSH_LINE
    return nl;
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

static int        g_audio_ok;
static Mix_Music *g_music;
static char       g_music_name[VN_NAME];

#define SFX_CACHE 8
static struct { char name[VN_NAME]; Mix_Chunk *chunk; } g_sfx[SFX_CACHE];
static int g_sfx_next;

static void audio_clear(void)
{
    if (g_audio_ok) Mix_HaltMusic();
    if (g_music) { Mix_FreeMusic(g_music); g_music = NULL; }
    g_music_name[0] = 0;
    for (int i = 0; i < SFX_CACHE; i++) {
        if (g_sfx[i].chunk) Mix_FreeChunk(g_sfx[i].chunk);
        memset(&g_sfx[i], 0, sizeof(g_sfx[i]));
    }
}

static void play_music(const char *name)
{
    if (!g_audio_ok) return;
    if (strcmp(name, g_music_name) == 0) return;
    snprintf(g_music_name, sizeof(g_music_name), "%s", name);
    if (g_music) {
        Mix_HaltMusic();
        Mix_FreeMusic(g_music);
        g_music = NULL;
    }
    if (!*name || strcmp(name, "ninguna") == 0) return;

    for (int v = 0; v < vn_asset_variants(VN_ASSET_MUSICA) && !g_music; v++) {
        char rel[VN_NAME + 32];
        vn_asset_rel(VN_ASSET_MUSICA, name, NULL, v, rel, sizeof(rel));
        const char *path = resolve_asset(rel, NULL);
        if (path) g_music = Mix_LoadMUS(path);
    }
    if (g_music) Mix_PlayMusic(g_music, -1);
}

static void play_sfx(const char *name)
{
    if (!g_audio_ok || !*name) return;
    Mix_Chunk *ch = NULL;
    for (int i = 0; i < SFX_CACHE; i++)
        if (g_sfx[i].chunk && strcmp(g_sfx[i].name, name) == 0) { ch = g_sfx[i].chunk; break; }
    if (!ch) {
        for (int v = 0; v < vn_asset_variants(VN_ASSET_SONIDO) && !ch; v++) {
            char rel[VN_NAME + 32];
            vn_asset_rel(VN_ASSET_SONIDO, name, NULL, v, rel, sizeof(rel));
            const char *path = resolve_asset(rel, NULL);
            if (path) ch = Mix_LoadWAV(path);
        }
        if (!ch) return;
        int slot = g_sfx_next++ % SFX_CACHE;
        if (g_sfx[slot].chunk) Mix_FreeChunk(g_sfx[slot].chunk);
        g_sfx[slot].chunk = ch;
        snprintf(g_sfx[slot].name, sizeof(g_sfx[slot].name), "%s", name);
    }
    Mix_PlayChannel(-1, ch, 0);
}

/* ------------------------------------------------------------------ */
/* Lo que se ve en pantalla                                            */
/* ------------------------------------------------------------------ */

typedef struct { char id[VN_SMALL]; char expr[VN_SMALL]; int pos; int alpha; } PChar;

static struct {
    char  bg[VN_NAME];
    char  prev_bg[VN_NAME];
    int   fade;                       /* 0..FADE_FRAMES */
    PChar chars[VN_MAXCHARS];
    int   nchars;
} g_pres;

static void pres_reset(void)
{
    memset(&g_pres, 0, sizeof(g_pres));
    g_pres.fade = FADE_FRAMES;
}

/* Aplica lo que cambio en el estado de la historia (fondo, personajes, sonido). */
static void apply_events(int instant)
{
    if (g_st.ev_bg) {
        if (strcmp(g_pres.bg, g_st.bg) != 0) {
            snprintf(g_pres.prev_bg, sizeof(g_pres.prev_bg), "%s", g_pres.bg);
            snprintf(g_pres.bg, sizeof(g_pres.bg), "%s", g_st.bg);
            g_pres.fade = instant ? FADE_FRAMES : 0;
        }
        g_st.ev_bg = 0;
    }
    if (g_st.ev_music) {
        play_music(g_st.music);
        g_st.ev_music = 0;
    }
    if (g_st.ev_sfx) {
        play_sfx(g_st.sfx);
        g_st.ev_sfx = 0;
    }
    if (g_st.ev_chars) {
        PChar next[VN_MAXCHARS];
        int n = 0;
        for (int i = 0; i < g_st.nchars && n < VN_MAXCHARS; i++) {
            PChar *c = &next[n++];
            memset(c, 0, sizeof(*c));
            snprintf(c->id, sizeof(c->id), "%s", g_st.chars[i].id);
            snprintf(c->expr, sizeof(c->expr), "%s", g_st.chars[i].expr);
            c->pos = g_st.chars[i].pos;
            c->alpha = instant ? 255 : 0;
            for (int k = 0; k < g_pres.nchars; k++)
                if (!strcmp(g_pres.chars[k].id, c->id)) { c->alpha = g_pres.chars[k].alpha; break; }
        }
        memcpy(g_pres.chars, next, sizeof(next));
        g_pres.nchars = n;
        g_st.ev_chars = 0;
    }
}

static void update_anim(void)
{
    if (g_pres.fade < FADE_FRAMES) g_pres.fade++;
    for (int i = 0; i < g_pres.nchars; i++) {
        g_pres.chars[i].alpha += 22;
        if (g_pres.chars[i].alpha > 255) g_pres.chars[i].alpha = 255;
    }
    if (g_toast_frames > 0) g_toast_frames--;
}

static void draw_bg_one(const char *name, int alpha)
{
    if (!*name) {
        fill_rect(0, 0, SCREEN_W, SCREEN_H, 12, 16, 34, (Uint8)alpha);
        return;
    }
    TexEntry *t = tex_get(VN_ASSET_FONDO, name, NULL);
    if (t) {
        SDL_SetTextureAlphaMod(t->tex, (Uint8)alpha);
        SDL_RenderCopy(g_ren, t->tex, NULL, NULL);
    } else {
        fill_rect(0, 0, SCREEN_W, SCREEN_H, 48, 50, 62, (Uint8)alpha);
        char msg[VN_NAME + 48];
        snprintf(msg, sizeof(msg), "(falta fondos/%s.png)", name);
        SDL_Color gray = { 190, 190, 200, 255 };
        draw_text(FONT_SMALL, msg, SCREEN_W / 2, 120, gray, (Uint8)alpha, ALIGN_CENTER);
    }
}

static void draw_background(void)
{
    if (g_pres.fade < FADE_FRAMES && *g_pres.prev_bg) {
        draw_bg_one(g_pres.prev_bg, 255);
        draw_bg_one(g_pres.bg, g_pres.fade * 255 / FADE_FRAMES);
    } else if (g_pres.fade < FADE_FRAMES) {
        draw_bg_one(g_pres.bg, g_pres.fade * 255 / FADE_FRAMES);
    } else {
        draw_bg_one(g_pres.bg, 255);
    }
}

static void draw_characters(void)
{
    static const int cx[3] = { 230, 480, 730 };
    for (int i = 0; i < g_pres.nchars; i++) {
        const PChar *c = &g_pres.chars[i];
        int pos = c->pos < 0 ? 0 : c->pos > 2 ? 2 : c->pos;
        TexEntry *t = tex_get(VN_ASSET_PERSONAJE, c->id, c->expr);
        if (t) {
            float scale = t->h > 470 ? 470.0f / (float)t->h : 1.0f;
            SDL_Rect dst;
            dst.w = (int)(t->w * scale);
            dst.h = (int)(t->h * scale);
            dst.x = cx[pos] - dst.w / 2;
            dst.y = SCREEN_H - dst.h;
            SDL_SetTextureAlphaMod(t->tex, (Uint8)c->alpha);
            SDL_RenderCopy(g_ren, t->tex, NULL, &dst);
        } else {
            SDL_Color col = color_from_name(c->id, 1);
            fill_rect(cx[pos] - 90, 150, 180, 394, col.r, col.g, col.b, (Uint8)(c->alpha * 180 / 255));
            char msg[VN_SMALL * 2 + 40];
            snprintf(msg, sizeof(msg), "(falta %s_%s.png)", c->id, c->expr);
            SDL_Color white = { 255, 255, 255, 255 };
            draw_text(FONT_SMALL, msg, cx[pos], 300, white, (Uint8)c->alpha, ALIGN_CENTER);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Dialogo                                                             */
/* ------------------------------------------------------------------ */

static struct {
    char  speaker[VN_SMALL];
    char  lines[MAX_LINES][LINE_LEN];
    int   nlines;
    int   page, npages;
    int   page_cps;                   /* letras de la pagina actual */
    float shown;                      /* letras reveladas           */
    int   done;
} g_dlg;

static int g_ui_hidden;

static void dlg_start_page(int page)
{
    g_dlg.page = page;
    int first = page * PAGE_LINES;
    int last = first + PAGE_LINES;
    if (last > g_dlg.nlines) last = g_dlg.nlines;
    g_dlg.page_cps = 0;
    for (int i = first; i < last; i++) g_dlg.page_cps += utf8_len(g_dlg.lines[i]);
    g_dlg.shown = 0;
    g_dlg.done = g_dlg.page_cps == 0;
}

static void dlg_begin(const char *speaker, const char *text, int instant)
{
    snprintf(g_dlg.speaker, sizeof(g_dlg.speaker), "%s", speaker);
    g_dlg.nlines = wrap_text(FONT_TEXT, text, TEXT_W, g_dlg.lines, MAX_LINES);
    g_dlg.npages = (g_dlg.nlines + PAGE_LINES - 1) / PAGE_LINES;
    if (g_dlg.npages < 1) g_dlg.npages = 1;
    dlg_start_page(0);
    if (instant) { g_dlg.shown = (float)g_dlg.page_cps; g_dlg.done = 1; }
    g_ui_hidden = 0;
}

static void dlg_update(void)
{
    if (g_dlg.done || g_ui_hidden) return;
    g_dlg.shown += 0.9f;
    if ((int)g_dlg.shown >= g_dlg.page_cps) {
        g_dlg.shown = (float)g_dlg.page_cps;
        g_dlg.done = 1;
    }
}

static void draw_dialog(void)
{
    int narrator = is_narrator(g_dlg.speaker);
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color cream = { 238, 232, 205, 255 };

    fill_rect(BOX_X, BOX_Y, BOX_W, BOX_H, 8, 10, 24, 205);
    frame_rect(BOX_X, BOX_Y, BOX_W, BOX_H, 220, 220, 235, 150);

    if (!narrator) {
        SDL_Color tag = color_from_name(g_dlg.speaker, 0);
        int w = text_width(FONT_NAME, g_dlg.speaker) + 44;
        if (w < 120) w = 120;
        fill_rect(BOX_X + 18, BOX_Y - 36, w, 42, tag.r, tag.g, tag.b, 245);
        frame_rect(BOX_X + 18, BOX_Y - 36, w, 42, 235, 235, 245, 180);
        draw_text(FONT_NAME, g_dlg.speaker, BOX_X + 18 + w / 2, BOX_Y - 33, white, 255, ALIGN_CENTER);
    }

    int skip = g_fonts[FONT_TEXT] ? TTF_FontLineSkip(g_fonts[FONT_TEXT]) : 32;
    int first = g_dlg.page * PAGE_LINES;
    int last = first + PAGE_LINES;
    if (last > g_dlg.nlines) last = g_dlg.nlines;
    int shown = (int)g_dlg.shown;
    int before = 0;
    for (int i = first; i < last; i++) {
        int len = utf8_len(g_dlg.lines[i]);
        int vis = shown - before;
        if (vis > len) vis = len;
        if (vis > 0) {
            char buf[LINE_LEN];
            size_t nb = utf8_prefix(g_dlg.lines[i], vis);
            memcpy(buf, g_dlg.lines[i], nb);
            buf[nb] = 0;
            draw_text(FONT_TEXT, buf, TEXT_X, TEXT_Y + (i - first) * skip, narrator ? cream : white, 255, ALIGN_LEFT);
        }
        before += len;
    }

    if (g_dlg.done) {
        int a = 150 + (int)(100.0f * sinf((float)g_frame * 0.12f));
        draw_text(FONT_SMALL, "\xE2\x96\xBC", BOX_X + BOX_W - 24, BOX_Y + BOX_H - 30, white, (Uint8)a, ALIGN_CENTER);
    }
}

/* ------------------------------------------------------------------ */
/* Menus (elecciones y pantalla de titulo)                             */
/* ------------------------------------------------------------------ */

static int g_sel;

static SDL_Rect choice_rect(int i, int n)
{
    int h = 58, gap = 18, w = 740;
    int total = n * h + (n - 1) * gap;
    SDL_Rect r;
    r.w = w;
    r.h = h;
    r.x = (SCREEN_W - w) / 2;
    r.y = (330 - total) / 2 + 40 + i * (h + gap);
    return r;
}

static SDL_Rect title_item_rect(int i)
{
    SDL_Rect r;
    r.w = 380;
    r.h = 54;
    r.x = (SCREEN_W - r.w) / 2;
    r.y = 318 + i * (r.h + 14);
    return r;
}

static void draw_button(SDL_Rect r, const char *label, int selected, int font)
{
    SDL_Color white = { 255, 255, 255, 255 };
    if (selected) {
        fill_rect(r.x, r.y, r.w, r.h, 34, 52, 104, 235);
        frame_rect(r.x, r.y, r.w, r.h, 255, 222, 120, 255);
    } else {
        fill_rect(r.x, r.y, r.w, r.h, 8, 10, 24, 190);
        frame_rect(r.x, r.y, r.w, r.h, 200, 200, 215, 110);
    }
    int th = g_fonts[font] ? TTF_FontHeight(g_fonts[font]) : 30;
    draw_text(font, label, r.x + r.w / 2, r.y + (r.h - th) / 2, white, 255, ALIGN_CENTER);
}

static int point_in(SDL_Rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Pantalla de titulo */
enum { TI_NEW, TI_CONTINUE, TI_QUIT };
static int g_title_items[3];
static int g_title_n;
static int g_has_save;

static void title_build(void)
{
    g_has_save = vn_save_exists(&g_script, g_savepath);
    g_title_n = 0;
    g_title_items[g_title_n++] = TI_NEW;
    if (g_has_save) g_title_items[g_title_n++] = TI_CONTINUE;
#ifndef __vita__
    g_title_items[g_title_n++] = TI_QUIT;
#endif
    if (g_sel >= g_title_n) g_sel = 0;
}

static const char *title_label(int item)
{
    switch (item) {
        case TI_NEW:      return "Nueva partida";
        case TI_CONTINUE: return "Continuar";
        default:          return "Salir";
    }
}

static void go_title(void)
{
    pres_reset();
    snprintf(g_pres.bg, sizeof(g_pres.bg), "%s", g_title_bg);
    play_music(g_title_music);
    g_mode = MODE_TITLE;
    g_sel = 0;
    g_ui_hidden = 0;
    title_build();
}

/* ------------------------------------------------------------------ */
/* Flujo de la historia                                                */
/* ------------------------------------------------------------------ */

static void run_step(void)
{
    int r = vn_step(&g_st, &g_script);
    if (g_st.err[0]) toast(g_st.err);
    apply_events(0);
    if (r == VN_WAIT_TEXT) {
        dlg_begin(g_st.speaker, g_st.text, 0);
        g_mode = MODE_TEXT;
    } else if (r == VN_WAIT_MENU) {
        g_mode = MODE_MENU;
        g_sel = 0;
        g_ui_hidden = 0;
    } else {
        go_title();
    }
}

static void start_new_game(void)
{
    pres_reset();
    vn_state_reset(&g_st, &g_script);
    g_st.ev_bg = g_st.ev_music = g_st.ev_chars = 1;
    run_step();
}

static void continue_game(void)
{
    int in_menu = 0;
    if (vn_load_save(&g_st, &g_script, &in_menu, g_savepath) != 0) {
        toast("No se pudo cargar la partida guardada.");
        return;
    }
    pres_reset();
    apply_events(1);
    if (in_menu) {
        run_step();
    } else {
        dlg_begin(g_st.speaker, g_st.text, 1);
        g_mode = MODE_TEXT;
    }
}

static void save_game(void)
{
    if (g_mode != MODE_TEXT && g_mode != MODE_MENU) return;
    if (vn_save(&g_st, &g_script, g_mode == MODE_MENU, g_savepath) == 0) toast("Partida guardada");
    else toast("No se pudo guardar la partida.");
}

/* ------------------------------------------------------------------ */
/* Carga del guion y configuracion                                     */
/* ------------------------------------------------------------------ */

static void load_config(void)
{
    snprintf(g_title, sizeof(g_title), "Novela visual");
    g_subtitle[0] = g_title_bg[0] = g_title_music[0] = 0;

    const char *path = resolve_asset("config.txt", NULL);
    if (!path) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        if (first && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
        first = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\r' || *p == '\n' || !*p) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p, *val = eq + 1;
        size_t kl = strlen(key);
        while (kl > 0 && (key[kl - 1] == ' ' || key[kl - 1] == '\t')) key[--kl] = 0;
        while (*val == ' ' || *val == '\t') val++;
        size_t vl = strlen(val);
        while (vl > 0 && (val[vl - 1] == '\r' || val[vl - 1] == '\n' || val[vl - 1] == ' ' || val[vl - 1] == '\t')) val[--vl] = 0;

        if (ieq(key, "titulo")) snprintf(g_title, sizeof(g_title), "%s", val);
        else if (ieq(key, "subtitulo")) snprintf(g_subtitle, sizeof(g_subtitle), "%s", val);
        else if (ieq(key, "fondo_titulo")) {
            snprintf(g_title_bg, sizeof(g_title_bg), "%s", val);
            for (char *c = g_title_bg; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        } else if (ieq(key, "musica_titulo")) {
            snprintf(g_title_music, sizeof(g_title_music), "%s", val);
            for (char *c = g_title_music; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        }
    }
    fclose(f);
}

static void reload_game(void)
{
    tex_clear_all();
    txt_clear_all();
    audio_clear();
    vn_script_free(&g_script);
    vn_script_load(&g_script, resolve_asset, NULL, "guion.txt");
    vn_script_check_assets(&g_script, resolve_asset, NULL);
    load_config();
    if (g_win) SDL_SetWindowTitle(g_win, g_title);

    for (int i = 0; i < g_script.nerrors; i++) fprintf(stderr, "[guion] %s\n", g_script.errors[i]);

    vn_state_reset(&g_st, &g_script);
    go_title();
    if (g_script.nerrors_total > 0) g_mode = MODE_ERRORS;
}

static void draw_errors(void)
{
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 60, 14, 20, 255);
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color pink = { 255, 205, 205, 255 };
    char head[160];
    snprintf(head, sizeof(head), "Se encontraron %d problema%s en tu guion",
             g_script.nerrors_total, g_script.nerrors_total == 1 ? "" : "s");
    draw_text(FONT_NAME, head, 40, 22, white, 255, ALIGN_LEFT);

    int y = 66;
    int shown = 0;
    char lines[3][LINE_LEN];
    for (int i = 0; i < g_script.nerrors; i++) {
        int n = wrap_text(FONT_SMALL, g_script.errors[i], SCREEN_W - 100, lines, 3);
        if (y + n * 24 > 428) break;                      /* no cabe: se resume abajo */
        for (int k = 0; k < n; k++) {
            draw_text(FONT_SMALL, lines[k], k == 0 ? 40 : 62, y, pink, 255, ALIGN_LEFT);
            y += 24;
        }
        y += 6;
        shown++;
    }
    if (g_script.nerrors_total > shown) {
        char more[120];
        snprintf(more, sizeof(more), "... y %d más (la lista completa sale en la consola de PC)",
                 g_script.nerrors_total - shown);
        draw_text(FONT_SMALL, more, 40, 440, white, 255, ALIGN_LEFT);
    }
    draw_text(FONT_SMALL, "Arréglalos en guion.txt y recarga, o continúa de todos modos.", 40, 478, white, 255, ALIGN_LEFT);
    draw_text(FONT_SMALL, HINT_TITLE, 40, 514, pink, 255, ALIGN_LEFT);
}

/* ------------------------------------------------------------------ */
/* Entrada                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int confirm, back, up, down, save, reload, menu, quit;
    int tap, tx, ty;
} Input;

static void open_controllers(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
}

static void poll_input(Input *in)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                in->quit = 1;
                break;

            case SDL_KEYDOWN:
                if (ev.key.repeat) break;
                switch (ev.key.keysym.sym) {
                    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_z:
                        in->confirm = 1; break;
                    case SDLK_x: case SDLK_BACKSPACE:
                        in->back = 1; break;
                    case SDLK_UP:   in->up = 1; break;
                    case SDLK_DOWN: in->down = 1; break;
                    case SDLK_s:    in->save = 1; break;
                    case SDLK_F5:   in->reload = 1; break;
                    case SDLK_ESCAPE: in->menu = 1; break;
                    default: break;
                }
                break;

            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(ev.cdevice.which);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                switch (ev.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A:         in->confirm = 1; break;
                    case SDL_CONTROLLER_BUTTON_B:         in->back = 1; break;
                    case SDL_CONTROLLER_BUTTON_Y:         in->menu = 1; break;
                    case SDL_CONTROLLER_BUTTON_START:     in->save = 1; break;
                    case SDL_CONTROLLER_BUTTON_BACK:      in->reload = 1; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:   in->up = 1; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: in->down = 1; break;
                    default: break;
                }
                break;

            case SDL_FINGERDOWN:
#ifdef __vita__
                /* En Vita el toque se lee directamente del panel delantero
                   (ver mas abajo). Los eventos de SDL se ignoran para que el
                   panel trasero no pueda hacer nada. */
                break;
#else
                in->tap = 1;
                in->tx = (int)(ev.tfinger.x * SCREEN_W);
                in->ty = (int)(ev.tfinger.y * SCREEN_H);
                break;
#endif

            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.which == SDL_TOUCH_MOUSEID) break;     /* ya llego como dedo */
#ifdef __vita__
                break;      /* en Vita no hay raton; evita toques fantasma del panel trasero */
#endif
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                {
                    float lx, ly;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                    SDL_RenderWindowToLogical(g_ren, ev.button.x, ev.button.y, &lx, &ly);
#else
                    int ww = SCREEN_W, wh = SCREEN_H;
                    SDL_GetWindowSize(g_win, &ww, &wh);
                    lx = (float)ev.button.x * SCREEN_W / (float)(ww > 0 ? ww : 1);
                    ly = (float)ev.button.y * SCREEN_H / (float)(wh > 0 ? wh : 1);
#endif
                    in->tap = 1;
                    in->tx = (int)lx;
                    in->ty = (int)ly;
                }
                break;

            default:
                break;
        }
    }
#ifdef __vita__
    {
        /* Solo panel DELANTERO, leido directamente del sistema. */
        static int was_down = 0;
        SceTouchData td;
        int n = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
        int down = (n > 0 && td.reportNum > 0);
        if (down && !was_down) {
            in->tap = 1;
            in->tx = td.report[0].x * SCREEN_W / 1920;
            in->ty = td.report[0].y * SCREEN_H / 1088;
        }
        was_down = down;
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Actualizar y dibujar cada cuadro                                    */
/* ------------------------------------------------------------------ */

static void title_activate(int item)
{
    switch (item) {
        case TI_NEW:      start_new_game(); break;
        case TI_CONTINUE: continue_game(); break;
        default:          g_running = 0; break;
    }
}

static void update(const Input *in)
{
    if (in->quit) { g_running = 0; return; }

    if (in->reload) { reload_game(); return; }

    update_anim();

    switch (g_mode) {
        case MODE_ERRORS:
            if (in->confirm || in->tap) go_title();
            break;

        case MODE_TITLE: {
            if (in->up)   g_sel = (g_sel + g_title_n - 1) % g_title_n;
            if (in->down) g_sel = (g_sel + 1) % g_title_n;
            if (in->tap) {
                for (int i = 0; i < g_title_n; i++) {
                    if (point_in(title_item_rect(i), in->tx, in->ty)) { title_activate(g_title_items[i]); return; }
                }
            }
            if (in->confirm) title_activate(g_title_items[g_sel]);
#ifndef __vita__
            if (in->menu) g_running = 0;
#endif
            break;
        }

        case MODE_TEXT:
            if (in->menu) { go_title(); return; }
            if (in->save) save_game();
            if (in->back) g_ui_hidden = !g_ui_hidden;
            if (in->confirm || in->tap) {
                if (g_ui_hidden) {
                    g_ui_hidden = 0;
                } else if (!g_dlg.done) {
                    g_dlg.shown = (float)g_dlg.page_cps;
                    g_dlg.done = 1;
                } else if (g_dlg.page + 1 < g_dlg.npages) {
                    dlg_start_page(g_dlg.page + 1);
                } else {
                    run_step();
                    return;
                }
            }
            dlg_update();
            break;

        case MODE_MENU: {
            int n = g_st.menu ? g_st.menu->n : 0;
            if (n == 0) { run_step(); return; }
            if (in->menu) { go_title(); return; }
            if (in->save) save_game();
            if (in->up)   g_sel = (g_sel + n - 1) % n;
            if (in->down) g_sel = (g_sel + 1) % n;
            int chosen = -1;
            if (in->tap) {
                for (int i = 0; i < n; i++)
                    if (point_in(choice_rect(i, n), in->tx, in->ty)) { chosen = i; break; }
            }
            if (in->confirm) chosen = g_sel;
            if (chosen >= 0) {
                vn_choose(&g_st, &g_script, chosen);
                run_step();
            }
            break;
        }
    }
}

static void render(void)
{
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);

    if (g_mode == MODE_ERRORS) {
        draw_errors();
    } else {
        draw_background();

        if (g_mode == MODE_TITLE) {
            fill_rect(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 120);
            SDL_Color white = { 255, 255, 255, 255 };
            SDL_Color shadow = { 0, 0, 0, 255 };
            SDL_Color soft = { 205, 218, 240, 255 };
            draw_text(FONT_TITLE, g_title, SCREEN_W / 2 + 3, 103, shadow, 255, ALIGN_CENTER);
            draw_text(FONT_TITLE, g_title, SCREEN_W / 2, 100, white, 255, ALIGN_CENTER);
            draw_text(FONT_SUB, g_subtitle, SCREEN_W / 2, 190, soft, 255, ALIGN_CENTER);
            for (int i = 0; i < g_title_n; i++)
                draw_button(title_item_rect(i), title_label(g_title_items[i]), i == g_sel, FONT_NAME);
            SDL_Color hint = { 185, 195, 215, 255 };
            draw_text(FONT_SMALL, HINT_TITLE, SCREEN_W / 2, 512, hint, 255, ALIGN_CENTER);
        } else {
            draw_characters();
            if (g_mode == MODE_TEXT && !g_ui_hidden) {
                draw_dialog();
            } else if (g_mode == MODE_MENU) {
                fill_rect(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 70);
                int n = g_st.menu ? g_st.menu->n : 0;
                for (int i = 0; i < n; i++)
                    draw_button(choice_rect(i, n), g_st.menu->opts[i].text, i == g_sel, FONT_TEXT);
            }
        }

        if (g_toast_frames > 0) {
            int a = g_toast_frames > 30 ? 255 : g_toast_frames * 255 / 30;
            SDL_Color white = { 255, 255, 255, 255 };
            int w = text_width(FONT_SMALL, g_toast) + 40;
            if (w > SCREEN_W - 20) w = SCREEN_W - 20;
            fill_rect((SCREEN_W - w) / 2, 14, w, 38, 0, 0, 0, (Uint8)(a * 200 / 255));
            draw_text(FONT_SMALL, g_toast, SCREEN_W / 2, 20, white, (Uint8)a, ALIGN_CENTER);
        }
    }
    SDL_RenderPresent(g_ren);
}

/* ------------------------------------------------------------------ */
/* Inicio y fin                                                        */
/* ------------------------------------------------------------------ */

static int load_fonts(void)
{
    static const int sizes[FONT_COUNT] = { 26, 28, 19, 58, 28 };
    const char *path = resolve_asset("fuente.ttf", NULL);
    if (!path) {
        fprintf(stderr, "No se encuentra assets/fuente.ttf\n");
        return 0;
    }
    char fontpath[1100];
    snprintf(fontpath, sizeof(fontpath), "%s", path);       /* resolve_asset reutiliza su buffer */
    for (int i = 0; i < FONT_COUNT; i++) {
        g_fonts[i] = TTF_OpenFont(fontpath, sizes[i]);
        if (!g_fonts[i]) {
            fprintf(stderr, "No se pudo abrir la fuente: %s\n", TTF_GetError());
            return 0;
        }
    }
    return 1;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    init_paths();

#ifdef __vita__
    /* Sin panel trasero: el aviso debe darse ANTES de SDL_Init. */
    SDL_SetHint("SDL_VITA_ENABLE_BACK_TOUCH", "0");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#ifdef __vita__
    /* Por si acaso: apagar el muestreo del panel trasero directamente. */
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP);
#endif

#ifdef __vita__
    g_win = SDL_CreateWindow("Novela visual", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
#else
    g_win = SDL_CreateWindow("Novela visual", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(g_ren, SCREEN_W, SCREEN_H);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);

    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return 1;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0) {
        g_audio_ok = 1;
        Mix_VolumeMusic(90);
    } else {
        fprintf(stderr, "Sin audio: %s\n", Mix_GetError());
    }
    open_controllers();

    if (!load_fonts()) return 1;

    reload_game();

    Uint32 t0;
    while (g_running) {
        t0 = SDL_GetTicks();
        g_frame++;
        Input in;
        memset(&in, 0, sizeof(in));
        poll_input(&in);
        update(&in);
        render();
        Uint32 spent = SDL_GetTicks() - t0;
        if (spent < 15) SDL_Delay(15 - spent);
    }

    tex_clear_all();
    txt_clear_all();
    audio_clear();
    vn_script_free(&g_script);
    for (int i = 0; i < FONT_COUNT; i++) if (g_fonts[i]) TTF_CloseFont(g_fonts[i]);
    if (g_audio_ok) Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    return 0;
}
