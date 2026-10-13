/*
 * vn.h - Nucleo de la novela visual (sin SDL, sin dependencias de consola).
 *
 * Aqui vive todo lo que entiende el guion: leer guion.txt, validarlo y
 * ejecutarlo paso a paso. Esta parte es igual en PC y en Vita.
 */
#ifndef VN_H
#define VN_H

#include <stddef.h>

#define VN_NAME      64     /* nombres de archivos, etiquetas, etc.        */
#define VN_SMALL     32     /* nombres de personajes, variables, expresion */
#define VN_TEXT      1024   /* texto maximo de un cuadro de dialogo        */
#define VN_MAXCHARS  4      /* personajes visibles a la vez                */
#define VN_MAXVARS   64     /* variables distintas                         */
#define VN_MAXOPTS   6      /* opciones por menu                           */
#ifndef VN_MAXERRS
#define VN_MAXERRS   12     /* mensajes de error que se guardan            */
#endif
#define VN_ERRLEN    220
#define VN_MAXFILES  16     /* archivos distintos (@incluir)               */

enum {
    VN_CMD_DIALOGO = 0,
    VN_CMD_FONDO,
    VN_CMD_MUSICA,
    VN_CMD_SONIDO,
    VN_CMD_PERSONAJE,
    VN_CMD_QUITAR,
    VN_CMD_MENU,
    VN_CMD_IR,
    VN_CMD_SI,
    VN_CMD_PONER,
    VN_CMD_SUMAR,
    VN_CMD_FIN
};

enum { VN_POS_IZQ = 0, VN_POS_CENTRO = 1, VN_POS_DER = 2 };
enum { VN_OP_EQ = 0, VN_OP_NE, VN_OP_GT, VN_OP_GE, VN_OP_LT, VN_OP_LE };

typedef struct {
    char *text;
    char  label[VN_NAME];
    int   target;           /* indice de comando, o -1 si la etiqueta no existe */
    int   line;
} VnOpt;

typedef struct {
    int   n;
    VnOpt opts[VN_MAXOPTS];
} VnMenu;

typedef struct {
    int    type;
    int    line;            /* linea del guion (para mensajes de error) */
    int    file;            /* indice en VnScript.files                  */
    char   a[VN_NAME];      /* orador / recurso / id / variable          */
    char   b[VN_SMALL];     /* expresion                                 */
    char   label[VN_NAME];  /* destino de @ir y @si                      */
    int    n;               /* numero (poner, sumar, si)                 */
    int    op;              /* comparacion de @si                        */
    int    pos;             /* posicion del personaje                    */
    int    target;          /* indice de comando destino, -1 si no existe*/
    char  *text;            /* texto del dialogo                         */
    VnMenu *menu;           /* menu de opciones                          */
} VnCmd;

typedef struct {
    VnCmd *cmds;
    int    ncmds, cap;

    struct VnLabel { char name[VN_NAME]; int index; } *labels;
    int    nlabels, lcap;

    char   errors[VN_MAXERRS][VN_ERRLEN];
    int    nerrors;         /* cuantos mensajes se guardaron           */
    int    nerrors_total;   /* cuantos hubo en total (puede ser mayor) */

    int    start;           /* donde empieza la historia ("inicio")    */

    char   files[VN_MAXFILES][VN_NAME];   /* archivos leidos */
    int    nfiles;
} VnScript;

/*
 * Devuelve la ruta completa de un archivo del juego (por ejemplo
 * "fondos/faro.png") o NULL si no existe. La cadena devuelta solo es valida
 * hasta la siguiente llamada.
 */
typedef const char *(*VnResolve)(const char *relname, void *ud);

/* Lee y valida el guion. Siempre devuelve 0; los problemas quedan en errors[] */
int  vn_script_load(VnScript *s, VnResolve resolve, void *ud, const char *mainfile);
void vn_script_free(VnScript *s);

/* Comprueba que existan las imagenes y sonidos que usa el guion. */
void vn_script_check_assets(VnScript *s, VnResolve resolve, void *ud);

/* --- Nombres de recursos: una sola fuente de verdad ------------------- */
enum { VN_ASSET_FONDO = 0, VN_ASSET_PERSONAJE, VN_ASSET_MUSICA, VN_ASSET_SONIDO };
int  vn_asset_variants(int kind);
/* Escribe en out la ruta relativa del recurso. b es la expresion (personaje). */
void vn_asset_rel(int kind, const char *a, const char *b, int variant,
                  char *out, size_t outn);

/* --- Estado de la partida ---------------------------------------------- */
typedef struct { char name[VN_SMALL]; int val; } VnVar;
typedef struct { char id[VN_SMALL]; char expr[VN_SMALL]; int pos; } VnChar;

typedef struct {
    int    pc;                       /* siguiente comando a ejecutar */
    char   bg[VN_NAME];
    char   music[VN_NAME];
    VnChar chars[VN_MAXCHARS];
    int    nchars;
    VnVar  vars[VN_MAXVARS];
    int    nvars;
    char   speaker[VN_SMALL];
    char   text[VN_TEXT];

    /* --- no se guardan --- */
    const VnMenu *menu;              /* menu pendiente (si hay)       */
    int    menu_pc;                  /* indice del comando del menu   */
    int    ev_bg, ev_music, ev_chars, ev_sfx;   /* cambios desde el ultimo paso */
    char   sfx[VN_NAME];
    char   err[VN_ERRLEN];           /* problema detectado al ejecutar */
} VnState;

enum { VN_WAIT_TEXT = 1, VN_WAIT_MENU = 2, VN_END = 3 };

void vn_state_reset(VnState *st, const VnScript *s);
/* Ejecuta hasta que haga falta al jugador: texto, menu o fin. */
int  vn_step(VnState *st, const VnScript *s);
/* Elige la opcion idx del menu pendiente. */
void vn_choose(VnState *st, const VnScript *s, int idx);

int  vn_var_get(const VnState *st, const char *name);

/* Guardado: devuelve 0 si salio bien. */
int  vn_save(const VnState *st, const VnScript *s, int in_menu, const char *path);
int  vn_load_save(VnState *st, const VnScript *s, int *in_menu, const char *path);
int  vn_save_exists(const VnScript *s, const char *path);

#endif
