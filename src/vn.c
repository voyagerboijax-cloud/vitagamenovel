/*
 * vn.c - Nucleo de la novela visual: lee el guion, lo valida y lo ejecuta.
 * No usa SDL ni nada de la consola, asi que se puede probar en cualquier PC.
 */
#include "vn.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Utilidades de texto                                                 */
/* ------------------------------------------------------------------ */

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static void copy_str(char *dst, size_t n, const char *src)
{
    if (n == 0) return;
    size_t i = 0;
    while (src[i] && i + 1 < n) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static char *trim(char *s)
{
    while (*s && is_space((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && is_space((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}

/* Minusculas y sin acentos (a e i o u): "Música" -> "musica". */
static void normalize_word(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0xC3 && in[i + 1]) {
            unsigned char d = (unsigned char)in[i + 1];
            char r = 0;
            switch (d) {
                case 0xA1: case 0x81: r = 'a'; break;
                case 0xA9: case 0x89: r = 'e'; break;
                case 0xAD: case 0x8D: r = 'i'; break;
                case 0xB3: case 0x93: r = 'o'; break;
                case 0xBA: case 0x9A: case 0xBC: case 0x9C: r = 'u'; break;
                default: break;
            }
            if (r) { out[o++] = r; i++; continue; }
        }
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        out[o++] = (char)c;
    }
    out[o] = 0;
}

static void lower_ascii(char *s)
{
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

static int ends_with_ci(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    if (b > a) return 0;
    for (size_t i = 0; i < b; i++) {
        char x = s[a - b + i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (x != y) return 0;
    }
    return 1;
}

/* Si escribes "faro.png" en vez de "faro", lo corrige. */
static void strip_ext(char *name)
{
    static const char *exts[] = { ".png", ".jpg", ".jpeg", ".ogg", ".wav", ".mp3", NULL };
    for (int i = 0; exts[i]; i++) {
        if (ends_with_ci(name, exts[i])) {
            name[strlen(name) - strlen(exts[i])] = 0;
            return;
        }
    }
}

static char *tok(char **pp)
{
    char *p = *pp;
    while (*p && is_space((unsigned char)*p)) p++;
    if (!*p) { *pp = p; return NULL; }
    char *start = p;
    while (*p && !is_space((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
    *pp = p;
    return start;
}

static int parse_int(const char *s, int *out)
{
    if (!s || !*s) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end) return 0;
    *out = (int)v;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Errores                                                             */
/* ------------------------------------------------------------------ */

static void add_error(VnScript *s, const char *file, int line, const char *fmt, ...)
{
    s->nerrors_total++;
    if (s->nerrors >= VN_MAXERRS) return;

    char msg[VN_ERRLEN - 60];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (line > 0)
        snprintf(s->errors[s->nerrors], VN_ERRLEN, "%s, línea %d: %s", file, line, msg);
    else
        snprintf(s->errors[s->nerrors], VN_ERRLEN, "%s: %s", file, msg);
    s->nerrors++;
}

/* ------------------------------------------------------------------ */
/* Construccion del guion                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    VnScript  *s;
    VnResolve  resolve;
    void      *ud;
    int        depth;
    int        last_was_option;
    int        cur_file;
} Parser;

static VnCmd *emit(Parser *p, int type, int line)
{
    VnScript *s = p->s;
    if (s->ncmds >= s->cap) {
        int ncap = s->cap ? s->cap * 2 : 256;
        VnCmd *n = (VnCmd *)realloc(s->cmds, (size_t)ncap * sizeof(VnCmd));
        if (!n) return NULL;
        s->cmds = n;
        s->cap = ncap;
    }
    VnCmd *c = &s->cmds[s->ncmds++];
    memset(c, 0, sizeof(*c));
    c->type = type;
    c->line = line;
    c->target = -1;
    c->file = p->cur_file;
    return c;
}

static const char *file_name(const VnScript *s, int idx)
{
    if (idx >= 0 && idx < s->nfiles) return s->files[idx];
    return "guion.txt";
}

static void add_label(Parser *p, const char *file, int line, const char *name)
{
    VnScript *s = p->s;
    for (int i = 0; i < s->nlabels; i++) {
        if (strcmp(s->labels[i].name, name) == 0) {
            add_error(s, file, line, "La etiqueta '%s' ya existe (hay dos @etiqueta con el mismo nombre).", name);
            return;
        }
    }
    if (s->nlabels >= s->lcap) {
        int ncap = s->lcap ? s->lcap * 2 : 32;
        struct VnLabel *n = (struct VnLabel *)realloc(s->labels, (size_t)ncap * sizeof(*n));
        if (!n) return;
        s->labels = n;
        s->lcap = ncap;
    }
    copy_str(s->labels[s->nlabels].name, VN_NAME, name);
    s->labels[s->nlabels].index = s->ncmds;
    s->nlabels++;
}

static int find_label(const VnScript *s, const char *name)
{
    for (int i = 0; i < s->nlabels; i++)
        if (strcmp(s->labels[i].name, name) == 0) return s->labels[i].index;
    return -1;
}

static int parse_pos(const char *word)
{
    char w[VN_SMALL];
    normalize_word(word, w, sizeof(w));
    if (!strcmp(w, "izquierda") || !strcmp(w, "izq") || !strcmp(w, "left")) return VN_POS_IZQ;
    if (!strcmp(w, "centro") || !strcmp(w, "center") || !strcmp(w, "centre")) return VN_POS_CENTRO;
    if (!strcmp(w, "derecha") || !strcmp(w, "der") || !strcmp(w, "right")) return VN_POS_DER;
    return -1;
}

/* Comillas: " o las tipograficas de los celulares. Devuelve bytes que ocupa. */
static int quote_len(const char *p)
{
    if (*p == '"') return 1;
    if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 &&
        ((unsigned char)p[2] == 0x9C || (unsigned char)p[2] == 0x9D)) return 3;
    return 0;
}

static int arrow_len(const char *p)
{
    if (p[0] == '-' && p[1] == '>') return 2;
    if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x86 && (unsigned char)p[2] == 0x92) return 3;
    return 0;
}

static void parse_option(Parser *p, const char *file, int lineno, char *rest)
{
    VnScript *s = p->s;
    static const char *ejemplo = "Ejemplo: @opcion \"Saludar\" -> saludar";

    char *q = rest;
    while (*q && is_space((unsigned char)*q)) q++;
    int ql = quote_len(q);
    if (!ql) {
        add_error(s, file, lineno, "Escribe el texto de la opción entre comillas. %s", ejemplo);
        return;
    }
    q += ql;
    char *text_start = q;
    while (*q && !quote_len(q)) q++;
    if (!*q) {
        add_error(s, file, lineno, "Falta cerrar las comillas. %s", ejemplo);
        return;
    }
    char *text_end = q;
    q += quote_len(q);
    while (*q && is_space((unsigned char)*q)) q++;
    int al = arrow_len(q);
    if (!al) {
        add_error(s, file, lineno, "Falta la flecha -> y la etiqueta. %s", ejemplo);
        return;
    }
    q += al;
    char *lab = tok(&q);
    if (!lab) {
        add_error(s, file, lineno, "Falta el nombre de la etiqueta después de ->. %s", ejemplo);
        return;
    }
    *text_end = 0;
    char *text = trim(text_start);
    if (!*text) {
        add_error(s, file, lineno, "El texto de la opción está vacío. %s", ejemplo);
        return;
    }

    VnCmd *c = NULL;
    if (p->last_was_option && p->s->ncmds > 0 && p->s->cmds[p->s->ncmds - 1].type == VN_CMD_MENU) {
        c = &p->s->cmds[p->s->ncmds - 1];
    } else {
        c = emit(p, VN_CMD_MENU, lineno);
        if (!c) return;
        c->menu = (VnMenu *)calloc(1, sizeof(VnMenu));
        if (!c->menu) return;
    }
    if (!c->menu) return;
    if (c->menu->n >= VN_MAXOPTS) {
        add_error(s, file, lineno, "Un menú puede tener como máximo %d opciones.", VN_MAXOPTS);
        return;
    }
    VnOpt *o = &c->menu->opts[c->menu->n++];
    o->text = (char *)malloc(strlen(text) + 1);
    if (o->text) strcpy(o->text, text);
    normalize_word(lab, o->label, sizeof(o->label));
    o->target = -1;
    o->line = lineno;
    p->last_was_option = 1;
}

static void parse_dialogue(Parser *p, const char *file, int lineno, char *line)
{
    VnScript *s = p->s;
    char speaker[VN_SMALL] = "";
    char *text = line;

    char *colon = strchr(line, ':');
    if (colon && colon != line && (colon - line) <= 24) {
        int ok = 1, spaces = 0;
        for (char *c = line; c < colon; c++) {
            unsigned char ch = (unsigned char)*c;
            if (ch == ' ') spaces++;
            if (strchr(".,!?;\"", ch) || ch == 0xC2 || ch == 0xE2) { ok = 0; break; }
        }
        if (spaces > 2) ok = 0;
        if (ok) {
            *colon = 0;
            copy_str(speaker, sizeof(speaker), trim(line));
            text = trim(colon + 1);
        }
    }

    if (!*text) {
        add_error(s, file, lineno, "Esta línea no tiene texto después de '%s:'.", speaker);
        return;
    }

    /* \n escrito a mano = salto de linea */
    for (char *r = text, *w = text;; r++) {
        if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r++; continue; }
        *w++ = *r;
        if (!*r) break;
    }

    size_t len = strlen(text);
    if (len >= VN_TEXT) {
        add_error(s, file, lineno, "El texto es demasiado largo (máximo %d letras); se cortó.", VN_TEXT - 1);
        len = VN_TEXT - 1;
        while (len > 0 && (((unsigned char)text[len]) & 0xC0) == 0x80) len--;
        text[len] = 0;
    }

    VnCmd *c = emit(p, VN_CMD_DIALOGO, lineno);
    if (!c) return;
    copy_str(c->a, VN_NAME, speaker);
    c->text = (char *)malloc(len + 1);
    if (c->text) { memcpy(c->text, text, len); c->text[len] = 0; }
    p->last_was_option = 0;
}

static void parse_file(Parser *p, const char *relname);

static void parse_command(Parser *p, const char *file, int lineno, char *line)
{
    VnScript *s = p->s;
    char *rest = line + 1;                    /* despues de la @ */
    char *word = tok(&rest);
    if (!word) {
        add_error(s, file, lineno, "Hay una @ sola. Escribe una orden, por ejemplo: @fondo muelle_dia");
        return;
    }
    char cmd[VN_SMALL];
    normalize_word(word, cmd, sizeof(cmd));

    if (!strcmp(cmd, "opcion")) { parse_option(p, file, lineno, rest); return; }

    char *a = NULL, *b = NULL, *c3 = NULL;

    if (!strcmp(cmd, "fondo") || !strcmp(cmd, "musica") || !strcmp(cmd, "sonido")) {
        int type = !strcmp(cmd, "fondo") ? VN_CMD_FONDO : !strcmp(cmd, "musica") ? VN_CMD_MUSICA : VN_CMD_SONIDO;
        const char *que = type == VN_CMD_FONDO ? "del fondo" : type == VN_CMD_MUSICA ? "de la música" : "del sonido";
        a = tok(&rest);
        b = tok(&rest);
        if (!a) {
            add_error(s, file, lineno, "Falta el nombre %s. Ejemplo: @%s nombre_del_archivo", que, cmd);
            return;
        }
        if (b) {
            add_error(s, file, lineno, "El nombre %s no puede llevar espacios (usa guion bajo, ej: muelle_de_dia).", que);
            return;
        }
        char name[VN_NAME];
        copy_str(name, sizeof(name), a);
        strip_ext(name);
        lower_ascii(name);
        if (type == VN_CMD_MUSICA) {
            char n2[VN_NAME];
            normalize_word(name, n2, sizeof(n2));
            if (!strcmp(n2, "ninguna") || !strcmp(n2, "ninguno") || !strcmp(n2, "silencio") ||
                !strcmp(n2, "nada") || !strcmp(n2, "stop") || !strcmp(n2, "parar"))
                copy_str(name, sizeof(name), "ninguna");
        }
        VnCmd *c = emit(p, type, lineno);
        if (c) copy_str(c->a, VN_NAME, name);
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "personaje")) {
        a = tok(&rest); b = tok(&rest); c3 = tok(&rest);
        if (!a) {
            add_error(s, file, lineno, "Falta el personaje. Ejemplo: @personaje ana feliz izquierda");
            return;
        }
        char id[VN_SMALL], expr[VN_SMALL] = "normal";
        int pos = VN_POS_CENTRO;
        copy_str(id, sizeof(id), a);
        lower_ascii(id);
        if (b) {
            int pb = parse_pos(b);
            if (pb >= 0 && !c3) {
                pos = pb;                      /* @personaje ana derecha */
            } else {
                copy_str(expr, sizeof(expr), b);
                lower_ascii(expr);
                strip_ext(expr);
                if (c3) {
                    pos = parse_pos(c3);
                    if (pos < 0) {
                        add_error(s, file, lineno,
                                  "No entiendo la posición '%s'. Usa: izquierda, centro o derecha.", c3);
                        return;
                    }
                }
            }
        }
        VnCmd *c = emit(p, VN_CMD_PERSONAJE, lineno);
        if (c) {
            copy_str(c->a, VN_NAME, id);
            copy_str(c->b, VN_SMALL, expr);
            c->pos = pos;
        }
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "quitar")) {
        a = tok(&rest);
        if (!a) {
            add_error(s, file, lineno, "Falta qué quitar. Ejemplo: @quitar ana   o   @quitar todos");
            return;
        }
        char id[VN_SMALL];
        copy_str(id, sizeof(id), a);
        lower_ascii(id);
        VnCmd *c = emit(p, VN_CMD_QUITAR, lineno);
        if (c) copy_str(c->a, VN_NAME, id);
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "etiqueta")) {
        a = tok(&rest);
        if (!a) {
            add_error(s, file, lineno, "Falta el nombre de la etiqueta. Ejemplo: @etiqueta final_bueno");
            return;
        }
        char name[VN_NAME];
        normalize_word(a, name, sizeof(name));
        add_label(p, file, lineno, name);
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "ir")) {
        a = tok(&rest);
        if (!a) {
            add_error(s, file, lineno, "Falta la etiqueta a la que saltar. Ejemplo: @ir final_bueno");
            return;
        }
        VnCmd *c = emit(p, VN_CMD_IR, lineno);
        if (c) normalize_word(a, c->label, sizeof(c->label));
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "si")) {
        static const char *ejemplo = "Ejemplo: @si confianza >= 2 -> final_bueno";
        char *var = tok(&rest), *op = tok(&rest), *num = tok(&rest), *arr = tok(&rest);
        char *lab = NULL;
        if (arr) {
            int al = arrow_len(arr);
            if (al && arr[al]) lab = arr + al;       /* ->etiqueta pegado */
            else if (al) lab = tok(&rest);
        }
        int n = 0, opc = -1;
        if (op) {
            if (!strcmp(op, "==") || !strcmp(op, "=")) opc = VN_OP_EQ;
            else if (!strcmp(op, "!=")) opc = VN_OP_NE;
            else if (!strcmp(op, ">")) opc = VN_OP_GT;
            else if (!strcmp(op, ">=")) opc = VN_OP_GE;
            else if (!strcmp(op, "<")) opc = VN_OP_LT;
            else if (!strcmp(op, "<=")) opc = VN_OP_LE;
        }
        if (!var || opc < 0 || !parse_int(num, &n) || !lab) {
            add_error(s, file, lineno, "La condición no se entiende. %s (comparaciones: == != > >= < <=)", ejemplo);
            return;
        }
        VnCmd *c = emit(p, VN_CMD_SI, lineno);
        if (c) {
            normalize_word(var, c->a, VN_SMALL);
            c->op = opc;
            c->n = n;
            normalize_word(lab, c->label, sizeof(c->label));
        }
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "poner") || !strcmp(cmd, "sumar")) {
        int is_sum = !strcmp(cmd, "sumar");
        a = tok(&rest);
        b = tok(&rest);
        int n = is_sum ? 1 : 0;
        if (!a || (b && !parse_int(b, &n)) || (!is_sum && !b)) {
            add_error(s, file, lineno, is_sum ? "Faltan datos. Ejemplo: @sumar confianza 1" : "Faltan datos. Ejemplo: @poner confianza 0");
            return;
        }
        VnCmd *c = emit(p, is_sum ? VN_CMD_SUMAR : VN_CMD_PONER, lineno);
        if (c) {
            normalize_word(a, c->a, VN_SMALL);
            c->n = n;
        }
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "fin")) {
        emit(p, VN_CMD_FIN, lineno);
        p->last_was_option = 0;
        return;
    }

    if (!strcmp(cmd, "incluir")) {
        a = tok(&rest);
        if (!a) {
            add_error(s, file, lineno, "Falta el archivo. Ejemplo: @incluir capitulo2.txt");
            return;
        }
        if (p->depth >= 4) {
            add_error(s, file, lineno, "Demasiados @incluir anidados.");
            return;
        }
        p->last_was_option = 0;
        char name[VN_NAME];
        copy_str(name, sizeof(name), a);
        parse_file(p, name);
        p->last_was_option = 0;
        return;
    }

    add_error(s, file, lineno,
              "No conozco la orden @%s. Órdenes: fondo, musica, sonido, personaje, quitar, opcion, "
              "etiqueta, ir, si, poner, sumar, fin, incluir.", word);
}

static void parse_line(Parser *p, const char *file, int lineno, char *raw)
{
    char *line = trim(raw);
    if (!*line) return;
    if (line[0] == '#') return;                               /* comentario */
    if (line[0] == '/' && line[1] == '/') return;             /* comentario */
    if (line[0] == '@') parse_command(p, file, lineno, line);
    else parse_dialogue(p, file, lineno, line);
}

static char *read_file(const char *path, long *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 4L * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    if (size_out) *size_out = (long)got;
    return buf;
}

static void parse_file(Parser *p, const char *relname)
{
    VnScript *s = p->s;
    const char *path = p->resolve(relname, p->ud);
    if (!path) {
        add_error(s, relname, 0, "No se encuentra el archivo.");
        return;
    }
    char *buf = read_file(path, NULL);
    if (!buf) {
        add_error(s, relname, 0, "No se pudo leer el archivo.");
        return;
    }

    int file_idx = -1;
    for (int i = 0; i < s->nfiles; i++)
        if (strcmp(s->files[i], relname) == 0) { file_idx = i; break; }
    if (file_idx < 0 && s->nfiles < VN_MAXFILES) {
        copy_str(s->files[s->nfiles], VN_NAME, relname);
        file_idx = s->nfiles++;
    }
    int saved_file = p->cur_file;
    p->cur_file = file_idx < 0 ? 0 : file_idx;
    p->depth++;

    char *cur = buf;
    if ((unsigned char)cur[0] == 0xEF && (unsigned char)cur[1] == 0xBB && (unsigned char)cur[2] == 0xBF)
        cur += 3;                                             /* BOM de Windows */

    int lineno = 0;
    while (*cur) {
        char *nl = strchr(cur, '\n');
        if (nl) *nl = 0;
        lineno++;
        parse_line(p, relname, lineno, cur);
        if (!nl) break;
        cur = nl + 1;
    }

    p->depth--;
    p->cur_file = saved_file;
    free(buf);
}

int vn_script_load(VnScript *s, VnResolve resolve, void *ud, const char *mainfile)
{
    memset(s, 0, sizeof(*s));
    Parser p;
    memset(&p, 0, sizeof(p));
    p.s = s;
    p.resolve = resolve;
    p.ud = ud;

    parse_file(&p, mainfile);

    /* Resolver saltos */
    for (int i = 0; i < s->ncmds; i++) {
        VnCmd *c = &s->cmds[i];
        const char *file = file_name(s, c->file);
        if (c->type == VN_CMD_IR || c->type == VN_CMD_SI) {
            c->target = find_label(s, c->label);
            if (c->target < 0)
                add_error(s, file, c->line, "No existe la etiqueta '%s'. Crea una línea @etiqueta %s", c->label, c->label);
        } else if (c->type == VN_CMD_MENU && c->menu) {
            for (int k = 0; k < c->menu->n; k++) {
                VnOpt *o = &c->menu->opts[k];
                o->target = find_label(s, o->label);
                if (o->target < 0)
                    add_error(s, file, o->line, "No existe la etiqueta '%s'. Crea una línea @etiqueta %s", o->label, o->label);
            }
        }
    }

    s->start = find_label(s, "inicio");
    if (s->start < 0) s->start = 0;
    if (s->ncmds == 0 && s->nerrors_total == 0)
        add_error(s, mainfile, 0, "El guion está vacío.");
    return 0;
}

void vn_script_free(VnScript *s)
{
    for (int i = 0; i < s->ncmds; i++) {
        free(s->cmds[i].text);
        if (s->cmds[i].menu) {
            for (int k = 0; k < s->cmds[i].menu->n; k++) free(s->cmds[i].menu->opts[k].text);
            free(s->cmds[i].menu);
        }
    }
    free(s->cmds);
    free(s->labels);
    memset(s, 0, sizeof(*s));
}

/* ------------------------------------------------------------------ */
/* Nombres de recursos                                                 */
/* ------------------------------------------------------------------ */

int vn_asset_variants(int kind)
{
    switch (kind) {
        case VN_ASSET_FONDO:     return 2;
        case VN_ASSET_PERSONAJE: return 1;
        case VN_ASSET_MUSICA:    return 3;
        case VN_ASSET_SONIDO:    return 2;
    }
    return 0;
}

void vn_asset_rel(int kind, const char *a, const char *b, int variant, char *out, size_t outn)
{
    static const char *ext_fondo[]  = { ".png", ".jpg" };
    static const char *ext_musica[] = { ".ogg", ".wav", ".mp3" };
    static const char *ext_sonido[] = { ".wav", ".ogg" };
    switch (kind) {
        case VN_ASSET_FONDO:
            snprintf(out, outn, "fondos/%s%s", a, ext_fondo[variant % 2]);
            break;
        case VN_ASSET_PERSONAJE:
            snprintf(out, outn, "personajes/%s_%s.png", a, b ? b : "normal");
            break;
        case VN_ASSET_MUSICA:
            snprintf(out, outn, "musica/%s%s", a, ext_musica[variant % 3]);
            break;
        case VN_ASSET_SONIDO:
            snprintf(out, outn, "sonidos/%s%s", a, ext_sonido[variant % 2]);
            break;
        default:
            out[0] = 0;
    }
}

void vn_script_check_assets(VnScript *s, VnResolve resolve, void *ud)
{
    char reported[64][VN_NAME + VN_SMALL + 16];
    int nrep = 0;

    for (int i = 0; i < s->ncmds; i++) {
        const VnCmd *c = &s->cmds[i];
        int kind = -1;
        const char *que = "";
        switch (c->type) {
            case VN_CMD_FONDO:     kind = VN_ASSET_FONDO;     que = "el fondo"; break;
            case VN_CMD_PERSONAJE: kind = VN_ASSET_PERSONAJE; que = "la imagen del personaje"; break;
            case VN_CMD_MUSICA:    kind = VN_ASSET_MUSICA;    que = "la música"; break;
            case VN_CMD_SONIDO:    kind = VN_ASSET_SONIDO;    que = "el sonido"; break;
            default: break;
        }
        if (kind < 0) continue;
        if (c->type == VN_CMD_MUSICA && !strcmp(c->a, "ninguna")) continue;

        char first[VN_NAME + VN_SMALL + 16];
        vn_asset_rel(kind, c->a, c->b, 0, first, sizeof(first));

        int found = 0;
        for (int v = 0; v < vn_asset_variants(kind) && !found; v++) {
            char rel[VN_NAME + VN_SMALL + 16];
            vn_asset_rel(kind, c->a, c->b, v, rel, sizeof(rel));
            if (resolve(rel, ud)) found = 1;
        }
        if (found) continue;

        int dup = 0;
        for (int k = 0; k < nrep; k++) if (!strcmp(reported[k], first)) { dup = 1; break; }
        if (dup) continue;
        if (nrep < 64) copy_str(reported[nrep++], sizeof(reported[0]), first);

        add_error(s, file_name(s, c->file), c->line, "No encuentro %s: falta el archivo %s", que, first);
    }
}

/* ------------------------------------------------------------------ */
/* Ejecucion                                                           */
/* ------------------------------------------------------------------ */

void vn_state_reset(VnState *st, const VnScript *s)
{
    memset(st, 0, sizeof(*st));
    st->pc = s->start;
}

static VnVar *var_find(VnState *st, const char *name, int create)
{
    for (int i = 0; i < st->nvars; i++)
        if (strcmp(st->vars[i].name, name) == 0) return &st->vars[i];
    if (!create) return NULL;
    if (st->nvars >= VN_MAXVARS) {
        snprintf(st->err, sizeof(st->err), "Demasiadas variables (máximo %d).", VN_MAXVARS);
        return NULL;
    }
    VnVar *v = &st->vars[st->nvars++];
    copy_str(v->name, sizeof(v->name), name);
    v->val = 0;
    return v;
}

int vn_var_get(const VnState *st, const char *name)
{
    for (int i = 0; i < st->nvars; i++)
        if (strcmp(st->vars[i].name, name) == 0) return st->vars[i].val;
    return 0;
}

static int compare(int a, int op, int b)
{
    switch (op) {
        case VN_OP_EQ: return a == b;
        case VN_OP_NE: return a != b;
        case VN_OP_GT: return a > b;
        case VN_OP_GE: return a >= b;
        case VN_OP_LT: return a < b;
        case VN_OP_LE: return a <= b;
    }
    return 0;
}

int vn_step(VnState *st, const VnScript *s)
{
    int guard = 0;
    st->menu = NULL;
    st->err[0] = 0;

    while (st->pc >= 0 && st->pc < s->ncmds) {
        if (++guard > 20000) {
            snprintf(st->err, sizeof(st->err),
                     "La historia da vueltas sin mostrar texto (¿un @ir que vuelve a sí mismo?).");
            st->pc = s->ncmds;
            return VN_END;
        }
        const VnCmd *c = &s->cmds[st->pc++];
        switch (c->type) {
            case VN_CMD_DIALOGO:
                copy_str(st->speaker, sizeof(st->speaker), c->a);
                copy_str(st->text, sizeof(st->text), c->text ? c->text : "");
                return VN_WAIT_TEXT;

            case VN_CMD_FONDO:
                copy_str(st->bg, sizeof(st->bg), c->a);
                st->ev_bg = 1;
                break;

            case VN_CMD_MUSICA:
                copy_str(st->music, sizeof(st->music), c->a);
                st->ev_music = 1;
                break;

            case VN_CMD_SONIDO:
                copy_str(st->sfx, sizeof(st->sfx), c->a);
                st->ev_sfx = 1;
                break;

            case VN_CMD_PERSONAJE: {
                int slot = -1;
                for (int i = 0; i < st->nchars; i++)
                    if (!strcmp(st->chars[i].id, c->a)) { slot = i; break; }
                if (slot < 0) {
                    if (st->nchars >= VN_MAXCHARS) {
                        snprintf(st->err, sizeof(st->err),
                                 "Ya hay %d personajes en pantalla; usa @quitar antes de mostrar a '%s'.",
                                 VN_MAXCHARS, c->a);
                        break;
                    }
                    slot = st->nchars++;
                    copy_str(st->chars[slot].id, VN_SMALL, c->a);
                }
                copy_str(st->chars[slot].expr, VN_SMALL, c->b);
                st->chars[slot].pos = c->pos;
                st->ev_chars = 1;
                break;
            }

            case VN_CMD_QUITAR:
                if (!strcmp(c->a, "todos") || !strcmp(c->a, "todas")) {
                    st->nchars = 0;
                } else {
                    for (int i = 0; i < st->nchars; i++) {
                        if (!strcmp(st->chars[i].id, c->a)) {
                            for (int k = i; k + 1 < st->nchars; k++) st->chars[k] = st->chars[k + 1];
                            st->nchars--;
                            break;
                        }
                    }
                }
                st->ev_chars = 1;
                break;

            case VN_CMD_MENU:
                st->menu = c->menu;
                st->menu_pc = st->pc - 1;
                return VN_WAIT_MENU;

            case VN_CMD_IR:
                if (c->target >= 0) st->pc = c->target;
                break;

            case VN_CMD_SI: {
                VnVar *v = var_find(st, c->a, 0);
                int val = v ? v->val : 0;
                if (compare(val, c->op, c->n) && c->target >= 0) st->pc = c->target;
                break;
            }

            case VN_CMD_PONER: {
                VnVar *v = var_find(st, c->a, 1);
                if (v) v->val = c->n;
                break;
            }

            case VN_CMD_SUMAR: {
                VnVar *v = var_find(st, c->a, 1);
                if (v) v->val += c->n;
                break;
            }

            case VN_CMD_FIN:
                st->pc = s->ncmds;
                return VN_END;
        }
    }
    return VN_END;
}

void vn_choose(VnState *st, const VnScript *s, int idx)
{
    (void)s;
    if (st->menu && idx >= 0 && idx < st->menu->n && st->menu->opts[idx].target >= 0)
        st->pc = st->menu->opts[idx].target;
    st->menu = NULL;
}

/* ------------------------------------------------------------------ */
/* Guardado                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char   magic[8];
    int    struct_size;
    int    ncmds;
    int    in_menu;
    int    pc;
    char   bg[VN_NAME];
    char   music[VN_NAME];
    VnChar chars[VN_MAXCHARS];
    int    nchars;
    VnVar  vars[VN_MAXVARS];
    int    nvars;
    char   speaker[VN_SMALL];
    char   text[VN_TEXT];
} SaveFile;

static int read_save(const VnScript *s, const char *path, SaveFile *sf)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(sf, 1, sizeof(*sf), f);
    fclose(f);
    if (got != sizeof(*sf)) return -1;
    if (memcmp(sf->magic, "VNSAVE1", 8) != 0) return -1;
    if (sf->struct_size != (int)sizeof(SaveFile)) return -1;
    if (sf->ncmds != s->ncmds) return -1;            /* el guion cambio */
    if (sf->pc < 0 || sf->pc > s->ncmds) return -1;
    if (sf->nchars < 0 || sf->nchars > VN_MAXCHARS) return -1;
    if (sf->nvars < 0 || sf->nvars > VN_MAXVARS) return -1;
    sf->bg[VN_NAME - 1] = sf->music[VN_NAME - 1] = 0;
    sf->speaker[VN_SMALL - 1] = 0;
    sf->text[VN_TEXT - 1] = 0;
    return 0;
}

int vn_save(const VnState *st, const VnScript *s, int in_menu, const char *path)
{
    SaveFile sf;
    memset(&sf, 0, sizeof(sf));
    memcpy(sf.magic, "VNSAVE1", 8);
    sf.struct_size = (int)sizeof(SaveFile);
    sf.ncmds = s->ncmds;
    sf.in_menu = in_menu;
    sf.pc = in_menu ? st->menu_pc : st->pc;
    memcpy(sf.bg, st->bg, sizeof(sf.bg));
    memcpy(sf.music, st->music, sizeof(sf.music));
    memcpy(sf.chars, st->chars, sizeof(sf.chars));
    sf.nchars = st->nchars;
    memcpy(sf.vars, st->vars, sizeof(sf.vars));
    sf.nvars = st->nvars;
    memcpy(sf.speaker, st->speaker, sizeof(sf.speaker));
    memcpy(sf.text, st->text, sizeof(sf.text));

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t put = fwrite(&sf, 1, sizeof(sf), f);
    fclose(f);
    return put == sizeof(sf) ? 0 : -1;
}

int vn_load_save(VnState *st, const VnScript *s, int *in_menu, const char *path)
{
    SaveFile sf;
    if (read_save(s, path, &sf) != 0) return -1;
    vn_state_reset(st, s);
    st->pc = sf.pc;
    memcpy(st->bg, sf.bg, sizeof(st->bg));
    memcpy(st->music, sf.music, sizeof(st->music));
    memcpy(st->chars, sf.chars, sizeof(st->chars));
    st->nchars = sf.nchars;
    memcpy(st->vars, sf.vars, sizeof(st->vars));
    st->nvars = sf.nvars;
    memcpy(st->speaker, sf.speaker, sizeof(st->speaker));
    memcpy(st->text, sf.text, sizeof(st->text));
    st->ev_bg = st->ev_music = st->ev_chars = 1;
    if (in_menu) *in_menu = sf.in_menu;
    return 0;
}

int vn_save_exists(const VnScript *s, const char *path)
{
    SaveFile sf;
    return read_save(s, path, &sf) == 0;
}
