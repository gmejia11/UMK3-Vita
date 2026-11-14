/* vita_plat.c -- implementa platform.h para PS Vita.
 *   ventana/GL: vitaGL     input: sceCtrl + sceTouch     tiempo: sceKernel
 * Todo es WEAK: si ui_alert.c / sdl_audio.c ya definen algo, su version gana sin error de
 * "multiple definition"; lo que falte lo cubre esto (stubs inofensivos). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include "platform.h"

#define WEAK __attribute__((weak))
#define SCR_W 960
#define SCR_H 544

int _newlib_heap_size_user = 96 * 1024 * 1024;   /* RAM de usuario para malloc */

WEAK int plat_open_borderless = 0;
WEAK void (*plat_ask_hook)(int begin) = 0;

static int g_quit, g_open;
static uint64_t g_t0;
static SceCtrlData g_pad;
static int g_style[2];

/* ------------------------------------------------------------ pad */
static void rear_poll(void);
static void read_pad(void) { sceCtrlPeekBufferPositive(0, &g_pad, 1); rear_poll(); }
static int B(unsigned m) { return (g_pad.buttons & m) != 0; }
static int d_up(void)    { return B(SCE_CTRL_UP)    || g_pad.ly < 64;  }
static int d_down(void)  { return B(SCE_CTRL_DOWN)  || g_pad.ly > 192; }
static int d_left(void)  { return B(SCE_CTRL_LEFT)  || g_pad.lx < 64;  }
static int d_right(void) { return B(SCE_CTRL_RIGHT) || g_pad.lx > 192; }

/* Mapeo: Cuadrado=HP Triangulo=LP R=Block X=HK Circulo=LK L=Run */
static int pad_word(void) {
  int w = 0;
  if (d_up())    w |= 1 << 0;
  if (d_down())  w |= 1 << 1;
  if (d_left())  w |= 1 << 2;
  if (d_right()) w |= 1 << 3;
  if (B(SCE_CTRL_SQUARE))   w |= 1 << 4;
  if (B(SCE_CTRL_TRIANGLE)) w |= 1 << 5;
  if (B(SCE_CTRL_RTRIGGER)) w |= 1 << 6;
  if (B(SCE_CTRL_CROSS))    w |= 1 << 7;
  if (B(SCE_CTRL_CIRCLE))   w |= 1 << 8;
  if (B(SCE_CTRL_LTRIGGER)) w |= 1 << 9;
  return w;
}

/* ------------------------------------------------------------ ventana / GL */
/* Tamanos de memoria de vitaGL, editables SIN recompilar: ux0:data/umk3/vita.cfg
   (lineas "clave=valor"; los valores son MB).  Claves: circular_pool_mb (0 = defecto de vitaGL, 32),
   legacy_pool_mb (defecto 16), ram_threshold_mb (defecto 16). */
unsigned vita_cfg_cp, vita_cfg_lp, vita_cfg_th;
int vita_rear_swap;             /* vita.cfg rearswap=1: si el panel trasero sale con los lados cambiados */
int vita_rear_on = 1;           /* vita.cfg rear=0 apaga el panel trasero (L2/R2) */
int vita_tex16;                /* 0 = RGBA8888 (defecto); 1 = opacas a RGB565; 2 = ademas con alfa a RGBA4444 */
/* vita.cfg se lee UNA vez a memoria: antes se abria una vez por clave (~20 aperturas en la tarjeta al arrancar). */
static char g_cfg[4096];
static int g_cfg_state;                 /* 0 = sin leer, 1 = leido, -1 = no existe */
static unsigned cfg_mb(const char *key, unsigned def) {
  if (g_cfg_state == 0) {
    FILE *f = fopen("ux0:data/umk3/vita.cfg", "r");
    g_cfg_state = -1;
    if (f) { size_t n = fread(g_cfg, 1, sizeof g_cfg - 1, f); g_cfg[n] = 0; fclose(f); g_cfg_state = 1; }
  }
  if (g_cfg_state < 0) return def;
  unsigned v = def;
  size_t kl = strlen(key);
  for (const char *line = g_cfg; *line; ) {
    if (strncmp(line, key, kl) == 0 && line[kl] == '=') v = (unsigned)atoi(line + kl + 1);
    const char *nl = strchr(line, '\n');
    if (!nl) break;
    line = nl + 1;
  }
  return v;
}

WEAK bool plat_open(const char *title, int width, int height) {
  (void)title; (void)width; (void)height;
  sceIoMkdir("ux0:data/umk3", 0777);
  sceIoMkdir("ux0:data/umk3/saves", 0777);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  /* Port Vita: relojes al maximo que da una app (CPU 444, bus 222, GPU 222, xbar 166 MHz). Por defecto una app corre
   * a CPU 333 / GPU 111: la carga de una pelea (descompresion de texturas, montaje de los personajes) es casi toda CPU.
   * vita.cfg clock=0 deja los relojes del sistema. */
  if (cfg_mb("clock", 1)) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
  }
  vita_cfg_cp = cfg_mb("circular_pool_mb", 12);
  vita_cfg_lp = cfg_mb("legacy_pool_mb", 4);
  vita_cfg_th = cfg_mb("ram_threshold_mb", 8);
  vita_tex16 = (int)cfg_mb("tex16", 0);
  /* facebatch=1 (defecto): las gotas de sangre se dibujan agrupadas por textura (draw_gl.c), asi que ya no hace falta saltarse
   * gotas: facemax pasa a 0 (todas). facebatch=0 vuelve a una llamada por gota, con el limite de 200 de antes. */
  { extern volatile int vita_face_batch; vita_face_batch = (int)cfg_mb("facebatch", 1); }
  { extern volatile int vita_face_max, vita_face_batch; vita_face_max = (int)cfg_mb("facemax", vita_face_batch ? 0 : 200); }
  /* kombo_slide=0: los kombos ya no separan a los luchadores (issue #114; fuera de linea). Solo si la clave esta en vita.cfg, para no
   * pisar un kombo_slide= de umk3.ini, que se lee antes. */
  { extern int port_kombo_slide; unsigned ks = cfg_mb("kombo_slide", 2u); if (ks != 2u) port_kombo_slide = ks != 0; }
  { extern volatile int vita_log_on; vita_log_on = (int)cfg_mb("log", 1); }
  { extern volatile int vita_skipdraw; vita_skipdraw = (int)cfg_mb("skipdraw", 1); }
  { extern volatile int vita_chain; vita_chain = (int)cfg_mb("chain", 0); }
  { extern volatile int vita_sndcache, vita_sndcache_mb; vita_sndcache = (int)cfg_mb("sndcache", 1); vita_sndcache_mb = (int)cfg_mb("sndcache_mb", 16); }
  { extern volatile int vita_sndfast; vita_sndfast = (int)cfg_mb("sndfast", 1); }
  { extern volatile int vita_quiet; vita_quiet = (int)cfg_mb("quiet", 0); }
  { extern volatile int vita_diag; vita_diag = (int)cfg_mb("diag", 0); }
  { extern volatile int vita_sgn; vita_sgn = (int)cfg_mb("sgn", 1); }
  vita_rear_swap = (int)cfg_mb("rearswap", 0);
  vita_rear_on = (int)cfg_mb("rear", 1);
  { extern volatile int vita_prof; vita_prof = (int)cfg_mb("prof", 0); }
  { extern volatile int vita_par; vita_par = (int)cfg_mb("par", 0); }
  { extern int vita_ani_keep; vita_ani_keep = (int)cfg_mb("anikeep", 0); }
  { extern volatile int vita_zmalloc; vita_zmalloc = (int)cfg_mb("zmalloc", 0); }
  if (vita_cfg_cp) vglSetCircularPoolSize(vita_cfg_cp * 1024u * 1024u);   /* antes de vglInit */
  vglInitExtended(vita_cfg_lp * 1024u * 1024u, SCR_W, SCR_H, vita_cfg_th * 1024u * 1024u, SCE_GXM_MULTISAMPLE_NONE);
  { extern volatile int vita_gl_ready; vita_gl_ready = 1; }   /* el hilo de log ya puede consultar vitaGL */
  g_t0 = sceKernelGetProcessTimeWide();
  g_open = 1;
  read_pad();
  return true;
}
WEAK void plat_close(void) { if (g_open) { g_open = 0; } }
WEAK void plat_quit(void) { g_quit = 1; }
/* Mejoras del port (no estan en el juego original) que en Vita son demasiado pesadas.
   Simbolo "weak": si el nombre no existe en tu version, simplemente no hace nada. */
extern int port_smooth_models __attribute__((weak));
WEAK bool plat_poll(void) {
  read_pad();
  if (&port_smooth_models) port_smooth_models = 0;
  return !g_quit;
}
WEAK bool plat_swap(void) { vglSwapBuffers(GL_FALSE); return true; }
WEAK void plat_size(int *w, int *h) { if (w) *w = SCR_W; if (h) *h = SCR_H; }
WEAK int  plat_refresh_hz(void) { return 60; }
WEAK bool plat_focused(void) { return true; }
WEAK void plat_input_used(void) {}
WEAK void *plat_gl_proc(const char *name) { (void)name; return 0; }  /* sin FBO por ahora */

WEAK double plat_time(void) { return (double)(sceKernelGetProcessTimeWide() - g_t0) / 1e6; }
WEAK void plat_wait_until(double t) {
  for (;;) {
    double d = t - plat_time();
    if (d <= 0) break;
    if (d > 0.002) sceKernelDelayThread((uint32_t)((d - 0.001) * 1e6));
  }
}

WEAK void plat_fullscreen(void) {}
WEAK void plat_fullscreen_exclusive(void) {}
WEAK void plat_display(int mode, int w, int h) { (void)mode; (void)w; (void)h; }
WEAK void plat_monitor_select(int i) { (void)i; }
WEAK int  plat_monitor(void) { return 0; }
WEAK int  plat_monitor_count(void) { return 1; }
WEAK void plat_monitor_name(int i, char *out, int n) { (void)i; snprintf(out, n, "PS Vita 960x544 *"); }
WEAK void plat_monitor_list(void) { printf("0\tPS Vita\n"); }

/* ------------------------------------------------------------ texto / mouse */
WEAK int  plat_text_take(char *out, int n) { (void)out; (void)n; return 0; }
WEAK void plat_text_clear(void) {}
WEAK int  plat_clipboard_text(char *out, int n) { (void)out; (void)n; return 0; }
WEAK int  plat_clipboard_set(const char *utf8) { (void)utf8; return 0; }
WEAK void plat_language(char *out, int n) { snprintf(out, n, "en"); }

WEAK int plat_mouse(int *x, int *y) {           /* pantalla tactil frontal = puntero */
  SceTouchData t;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
  if (t.reportNum > 0) {
    if (x) *x = t.report[0].x / 2;
    if (y) *y = t.report[0].y / 2;
    return 1;
  }
  return 0;
}
WEAK int plat_mouse_wheel(void) { return 0; }

WEAK int plat_ask(const unsigned short *msg, const unsigned short *ok, const unsigned short *cancel) {
  (void)msg; (void)ok; (void)cancel; return 0;   /* acepta siempre */
}

/* ------------------------------------------------------------ teclas / pads */
/* Panel tactil TRASERO como L2/R2. Solo cuentan las dos esquinas de ARRIBA (donde no descansan los dedos al agarrar la consola):
 * esquina izquierda = L2 (correr), esquina derecha = R2 (especial). El panel trasero esta espejado respecto a la pantalla; si te
 * salen cambiadas, rearswap=1 en vita.cfg. */
static int g_rear_l2, g_rear_r2;          /* leido UNA vez por fotograma en read_pad (antes, hasta 4 lecturas del panel) */
static void rear_poll(void) {
  SceTouchData t;
  int *l2 = &g_rear_l2, *r2 = &g_rear_r2;
  *l2 = *r2 = 0;
  if (!vita_rear_on || !g_open) return;
  sceTouchPeek(SCE_TOUCH_PORT_BACK, &t, 1);
  for (unsigned i = 0; i < t.reportNum && i < 8; i++) {
    int x = t.report[i].x, y = t.report[i].y;
    if (y > 380) continue;                                   /* franja superior (el panel mide ~890 de alto) */
    int mine_right = x < 640, mine_left = x > 1279;          /* espejado: x pequena = lado derecho del jugador */
    if (vita_rear_swap) { int tmp = mine_right; mine_right = mine_left; mine_left = tmp; }
    if (mine_left) *l2 = 1;
    if (mine_right) *r2 = 1;
  }
}
static void rear_read(int *l2, int *r2) { *l2 = g_rear_l2; *r2 = g_rear_r2; }
static int rear_l2(void) { return g_rear_l2; }
static int rear_r2(void) { return g_rear_r2; }

WEAK int plat_key(int code) {
  switch (code) {
    case PK_UP:    return d_up();
    case PK_DOWN:  return d_down();
    case PK_LEFT:  return d_left();
    case PK_RIGHT: return d_right();
    /* Distribucion del PC (umk3.ini: pad_hp=WEST pad_lp=SOUTH pad_hk=NORTH pad_lk=EAST pad_block=R1 pad_run=L1+L2) */
    case PK_HP:    return B(SCE_CTRL_SQUARE);                          /* cuadrado  = puno fuerte */
    case PK_LP:    return B(SCE_CTRL_CROSS);                           /* X         = puno debil  */
    case PK_BL:    return B(SCE_CTRL_RTRIGGER);                        /* R         = bloqueo     */
    case PK_HK:    return B(SCE_CTRL_TRIANGLE);                        /* triangulo = patada fuerte */
    case PK_LK:    return B(SCE_CTRL_CIRCLE);                          /* circulo   = patada debil */
    case PK_RUN:   return B(SCE_CTRL_LTRIGGER) || rear_l2();                        /* L         = correr      */
    /* Modo de 5 botones (pad5_p=WEST+NORTH pad5_k=SOUTH+EAST pad5_b=R1 pad5_r=L1+L2): antes NO estaban y los combos no iban */
    case PK_5_P:   return B(SCE_CTRL_SQUARE) || B(SCE_CTRL_TRIANGLE);  /* puno  = cuadrado o triangulo */
    case PK_5_B:   return B(SCE_CTRL_RTRIGGER);                        /* bloqueo = R */
    case PK_5_K:   return B(SCE_CTRL_CROSS) || B(SCE_CTRL_CIRCLE);     /* patada = X o circulo */
    case PK_5_R:   return B(SCE_CTRL_LTRIGGER) || rear_l2();                        /* correr = L */
    case PK_SPECIAL: return rear_r2();                                  /* R2 = panel trasero, derecha */
    case PK_PAUSE: return B(SCE_CTRL_START);
    case PK_MOVES: return B(SCE_CTRL_SELECT);
    case PK_OK:    return B(SCE_CTRL_CROSS);
    case PK_BACK:  return B(SCE_CTRL_CIRCLE);
    default:       return 0;
  }
}
WEAK void plat_key_label(int code, char *out, int n) {
  const char *s = "-";
  switch (code) {
    case PK_HP: s = "SQUARE"; break;   case PK_LP: s = "CROSS";    break;
    case PK_BL: s = "R";      break;   case PK_HK: s = "TRIANGLE"; break;
    case PK_LK: s = "CIRCLE"; break;   case PK_RUN: s = "L";       break;
    case PK_5_P: s = "SQUARE/TRIANGLE"; break;  case PK_5_B: s = "R"; break;
    case PK_5_K: s = "CROSS/CIRCLE";    break;  case PK_5_R: s = "L"; break;
    case PK_PAUSE: s = "START"; break; case PK_MOVES: s = "SELECT"; break;
    default: break;
  }
  snprintf(out, n, "%s", s);
}
WEAK void plat_key_glyph(int code, char *out, int n) { plat_key_label(code, out, n); }
WEAK void plat_bind_key(int code, int key) { (void)code; (void)key; }
WEAK int  plat_key_bound(int code) { (void)code; return 0; }
WEAK int  plat_key_any(void) { return 0; }
WEAK int  plat_key_backspace(void) { return 0; }

WEAK int plat_pad(int which) { return which == 0 ? pad_word() : -1; }

WEAK int plat_pad_read(PLAT_PAD *o) {
  if (!o) return 0;
  memset(o, 0, sizeof *o);
  o->connected = 1;
  o->up = d_up(); o->down = d_down(); o->left = d_left(); o->right = d_right();
  if (B(SCE_CTRL_CROSS))    o->buttons |= 1u << PB_SOUTH;
  if (B(SCE_CTRL_CIRCLE))   o->buttons |= 1u << PB_EAST;
  if (B(SCE_CTRL_SQUARE))   o->buttons |= 1u << PB_WEST;
  if (B(SCE_CTRL_TRIANGLE)) o->buttons |= 1u << PB_NORTH;
  if (B(SCE_CTRL_LTRIGGER)) o->buttons |= 1u << PB_L1;
  if (B(SCE_CTRL_RTRIGGER)) o->buttons |= 1u << PB_R1;
  { int l2, r2; rear_read(&l2, &r2); if (l2) o->buttons |= 1u << PB_L2; if (r2) o->buttons |= 1u << PB_R2; }
  if (B(SCE_CTRL_SELECT))   o->buttons |= 1u << PB_SELECT;
  if (B(SCE_CTRL_START))    o->buttons |= 1u << PB_START;
  o->rx = g_pad.rx < 64 ? -1 : (g_pad.rx > 192 ? 1 : 0);
  snprintf(o->name, sizeof o->name, "PS Vita");
  o->kind = 4;                                   /* iconos estilo PlayStation */
  return 1;
}
WEAK void plat_pad_select(const char *which) { (void)which; }
WEAK void plat_pad2_select(const char *which) { (void)which; }
WEAK int  plat_pad2_read(PLAT_PAD *o) { if (o) { memset(o, 0, sizeof *o); } return 0; }
WEAK void plat_pad_list(void) { printf("auto\tPS Vita\n"); }

WEAK void plat_pad_glyph(int kind, int pb, char *out, int n) {
  static const char *nm[PB_COUNT] = { "CROSS","CIRCLE","SQUARE","TRIANGLE","L","R",
                                      "L2","R2","SELECT","START","L3","R3","HOME" };
  (void)kind;
  snprintf(out, n, "%s", (pb >= 0 && pb < PB_COUNT) ? nm[pb] : "?");
}
WEAK int plat_pad_glyph_style(int player, int style) {
  int p = player ? 1 : 0;
  if (style >= 0) g_style[p] = style;
  return g_style[p];
}
WEAK int plat_pad_glyph_kind(int player, int kind) { (void)player; return kind; }
WEAK int plat_pad_sony_kind(unsigned product) { (void)product; return 4; }

/* ------------------------------------------------------------ UI dibujada por el backend:
   NULL = "usa tu propia fuente" (ver platform.h). Si ui_alert.c las define, ganan. */
WEAK unsigned char *plat_ui_menu(const char *title, const char *const *tabs, int ntab, int tab,
    const char *const *label, const char *const *value, const int *enabled, int n, int sel,
    const char *footer, float width, int split, float s, int *w, int *h) {
  (void)title;(void)tabs;(void)ntab;(void)tab;(void)label;(void)value;(void)enabled;(void)n;
  (void)sel;(void)footer;(void)width;(void)split;(void)s;(void)w;(void)h; return 0;
}
WEAK unsigned char *plat_ui_grid(const char *title, const unsigned char *const *cells, int n,
    int cols, int cpx, float cell, int sel, const char *footer, float s, int *w, int *h) {
  (void)title;(void)cells;(void)n;(void)cols;(void)cpx;(void)cell;(void)sel;(void)footer;
  (void)s;(void)w;(void)h; return 0;
}
WEAK unsigned char *plat_ui_badge(const char *name, const unsigned char *icon, int icon_px,
    float s, int *w, int *h) {
  (void)name;(void)icon;(void)icon_px;(void)s;(void)w;(void)h; return 0;
}
WEAK unsigned char *plat_ui_gradient(const struct plat_grad *g, float k,
    struct plat_grad_hit *hit, int *w, int *h) {
  (void)g;(void)k;(void)hit;(void)w;(void)h; return 0;
}

/* ------------------------------------------------------------ audio: stubs de respaldo.
   Lo normal es que sdl_audio.c (SDL2 de vdpm) defina todo esto y gane. */
WEAK int  plat_audio_open(int rate) { (void)rate; return 0; }
WEAK void plat_audio_close(void) {}
WEAK int  plat_audio_play(const unsigned char *pcm, int frames, float gain) { (void)pcm;(void)frames;(void)gain; return 0; }
WEAK int  plat_audio_play_at(const unsigned char *pcm, int frames, int rate, float gain) { (void)pcm;(void)frames;(void)rate;(void)gain; return 0; }
WEAK void plat_audio_stop_pcm(const unsigned char *pcm) { (void)pcm; }
WEAK void plat_audio_stop_all(void) {}
WEAK void plat_audio_update(void) {}
WEAK void plat_music_play(const char *path, int loop) { (void)path; (void)loop; }
WEAK void plat_music_stop(void) {}
WEAK void plat_music_volume(float gain) { (void)gain; }
