/* vita_watch.c -- DIAGNOSTICO. Vigila el cursor de animacion (field40, offset 0x40) de los 30 objetos
 * Plyr (umk3_common_Plyr: 30 x 108 bytes) al final de cada fotograma. Si aparece un valor que parece un
 * puntero pero cae fuera de la memoria de la aplicacion, escribe ux0:data/umk3/corrupt.txt con el estado
 * del objeto ahora y en el fotograma anterior, el historial del campo, los 32 bytes que hay justo antes de
 * Plyr (el final de umk3_common_Pp) y el ultimo archivo abierto. No modifica nada del juego. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>

extern uint32_t umk3_common_Plyr[];      /* 30 x 108 bytes */
extern uint32_t umk3_common_Pp[];        /* 4200 bytes, justo antes de Plyr */
extern volatile unsigned vita_swaps;
extern char vita_last_open[256];

#define NOBJ 30
#define OW   27                          /* 108 bytes / 4 */
#define F40  16                          /* 0x40 / 4 */
#define HIST 16
#define MAXREP 12

static uint32_t g_prev[NOBJ][OW];
static uint32_t g_hist[NOBJ][HIST];
static unsigned g_hpos, g_nrep, g_frames;
static unsigned char g_reported[NOBJ];

static int ptr_like_but_wild(uint32_t v) {
  return v > 0x01000000u && !(v >= 0x81000000u && v < 0x90000000u);
}

static void report(int i) {
  FILE *f = fopen("ux0:data/umk3/corrupt.txt", "a");
  if (!f) return;
  fprintf(f, "=== fotograma %u (swaps=%u) objeto Plyr[%d] field40=0x%08x ultimo_archivo=%s\n",
          g_frames, vita_swaps, i, (unsigned)umk3_common_Plyr[i * OW + F40], vita_last_open);
  fprintf(f, "ahora   :");
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)umk3_common_Plyr[i * OW + k]);
  fprintf(f, "\nantes   :");
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)g_prev[i][k]);
  fprintf(f, "\nhistorial de field40 (viejo -> nuevo):");
  for (unsigned h = 0; h < HIST; h++) fprintf(f, " %08x", (unsigned)g_hist[i][(g_hpos + 1 + h) % HIST]);
  fprintf(f, "\nfinal de Pp (8 palabras antes de Plyr):");
  const uint32_t *tail = umk3_common_Plyr - 8;
  for (int k = 0; k < 8; k++) fprintf(f, " %08x", (unsigned)tail[k]);
  fprintf(f, "\ndireccion de Plyr=%p  de Pp=%p\n\n", (void *)umk3_common_Plyr, (void *)umk3_common_Pp);
  fclose(f);
}

static void freeze_check(void);
void vita_watch_frame(void) {
  g_frames++;
  freeze_check();
  for (int i = 0; i < NOBJ; i++) {
    uint32_t v = umk3_common_Plyr[i * OW + F40];
    g_hist[i][g_hpos % HIST] = v;
    if (ptr_like_but_wild(v) && !g_reported[i] && g_nrep < MAXREP) {
      g_reported[i] = 1; g_nrep++;
      report(i);
    } else if (!ptr_like_but_wild(v)) {
      g_reported[i] = 0;                 /* volvio a ser razonable: se puede avisar otra vez */
    }
  }
  g_hpos++;
  for (int i = 0; i < NOBJ; i++) memcpy(g_prev[i], &umk3_common_Plyr[i * OW], sizeof g_prev[i]);
}

/* Llamada desde ani_fetch (other.c): why=1 personaje absurdo (se devuelve 0), why=2 animacion absurda (se devuelve
 * la primera de la fila), why=0 solo aviso (animacion >= 38). Maximo 60 lineas. */
void vita_ani_bad(unsigned c, unsigned i, unsigned why) {
  static unsigned n;
  if (n >= 60) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/ani_bad.txt", "a");
  if (!f) return;
  fprintf(f, "fotograma %u swaps=%u ani_fetch c=%u i=0x%x motivo=%u ultimo_archivo=%s\n",
          g_frames, vita_swaps, c, i, why, vita_last_open);
  fclose(f);
}

/* ---- comprobacion de cursores de animacion (la usa other.c con UMK3_VITA) ---- */
#ifndef VITA_PTR_LO
#define VITA_PTR_LO 0x81000000u        /* memoria de la aplicacion en la Vita: texto, datos y monton */
#define VITA_PTR_HI 0x90000000u
#endif
int vita_ptr_ok(uint32_t v) { return v >= VITA_PTR_LO && v < VITA_PTR_HI && (v & 3u) == 0; }

/* where: 1 next_anirate, 2 entrada de do_next_a9_frame_pxob, 3 fotograma invalido, 4 salto invalido.
 * Anota en ux0:data/umk3/cursor_bad.txt (maximo 40) y el juego sigue: el script se da por terminado. */
void vita_bad_cursor(const void *obj, int where, uint32_t v) {
  static unsigned n;
  if (n >= 40) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/cursor_bad.txt", "a");
  if (!f) return;
  long idx = -1;
  if ((const uint32_t *)obj >= umk3_common_Plyr && (const uint32_t *)obj < umk3_common_Plyr + NOBJ * OW)
    idx = (long)(((const uint32_t *)obj - umk3_common_Plyr) / OW);
  fprintf(f, "fotograma %u swaps=%u donde=%d valor=0x%08x obj=%p Plyr[%ld] ultimo_archivo=%s\n  obj:",
          g_frames, vita_swaps, where, (unsigned)v, obj, idx, vita_last_open);
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)((const uint32_t *)obj)[k]);
  fprintf(f, "\n");
  fclose(f);
}

/* ---- quien llama a get_char_ani* con un PUNTERO en vez de un indice (la usa other.c) ---- */
int vita_ani_keep;   /* vita.cfg anikeep=1: con indice-puntero se deja el cursor como esta en vez de volver a la animacion 0 */

/* which: 1 get_char_ani  2 get_char_ani2  3 get_his_char_ani  4 get_his_char_ani2.  Escribe ani_site.txt (maximo 60). */
extern void *port_mk3_running;           /* mk3_update: el manejador de hilo que esta corriendo ahora... */
extern unsigned port_mk3_running_token;  /* ...y su token (el estado en el que se reanudo) */
void vita_ani_site(const void *obj, int which, void *lr) {
  static unsigned n;
  if (n >= 60) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/ani_site.txt", "a");
  if (!f) return;
  const uint32_t *o = (const uint32_t *)obj;
  unsigned c = 0xffffffffu;
  if (which <= 2 && vita_ptr_ok(o[2])) c = ((const uint32_t *)(uintptr_t)o[2])[9];
  fprintf(f, "fotograma %u which=%d lr=0x%08x ancla(vita_ani_site)=0x%08x field40=0x%08x c=%u ultimo_archivo=%s handler=0x%08x token=0x%x\n  obj:",
          g_frames, which, (unsigned)(uintptr_t)lr, (unsigned)(uintptr_t)&vita_ani_site, (unsigned)o[16], c, vita_last_open,
          (unsigned)(uintptr_t)port_mk3_running, (unsigned)port_mk3_running_token);
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)o[k]);
  fprintf(f, "\n");
  fclose(f);
}

/* ---- vuelca los hilos (threads.txt) con L+R+Select, o solo si el cursor de animacion del jugador 1 no cambia en 150 fotogramas ---- */
extern void vita_dump_threads(unsigned frame, const char *why);   /* en mk3.c */
static void freeze_check(void) {
  static unsigned prev_cur, same, ndump, prev_btn;
  static int armed = 1;
  SceCtrlData pad;
  const unsigned combo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
  sceCtrlPeekBufferPositive(0, &pad, 1);
  int manual = ((pad.buttons & combo) == combo) && ((prev_btn & combo) != combo);
  prev_btn = pad.buttons;
  uint32_t cur = umk3_common_Plyr[F40];
  if (cur == prev_cur) same++; else { same = 0; armed = 1; }
  prev_cur = cur;
  int autod = (same == 150 && armed && vita_ptr_ok(cur));
  if ((manual || autod) && ndump < 10) {
    ndump++;
    if (autod) armed = 0;
    vita_dump_threads(g_frames, manual ? "manual (L+R+Select)" : "auto: cursor del jugador 1 sin cambiar en 150 fotogramas");
  }
}

/* ---- sueno de hilo con forma de puntero (la usa mk3_update en mk3.c) ---- */
/* Anota en ux0:data/umk3/sleep_bad.txt (maximo 40): la rutina de hilo que acababa de correr, su token, el valor y el objeto. */
void vita_bad_sleep(void *handler, unsigned token, uint32_t sleep, uint32_t proc) {
  static unsigned n;
  if (n >= 40) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/sleep_bad.txt", "a");
  if (!f) return;
  fprintf(f, "fotograma %u handler=0x%08x ancla(vita_bad_sleep)=0x%08x token=0x%x sueno=0x%08x proc=0x%08x ultimo_archivo=%s\n  obj:",
          g_frames, (unsigned)(uintptr_t)handler, (unsigned)(uintptr_t)&vita_bad_sleep, token, (unsigned)sleep, (unsigned)proc, vita_last_open);
  if (vita_ptr_ok(proc)) for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)((const uint32_t *)(uintptr_t)proc)[k]);
  fprintf(f, "\n");
  fclose(f);
}

/* vita.cfg sgn=0/1: 1 (por defecto) trata las direcciones de la Vita como positivas en las comparaciones con signo de la logica */
volatile int vita_sgn = 1;

/* vita.cfg facemax=N: maximo de gotas/charcos/lanza dibujados por fotograma (0 = sin limite) */
volatile int vita_face_max = 200;

/* vita.cfg skipdraw=0: dibujar todos los pasos de logica como antes (1 = solo el ultimo de cada vuelta) */
volatile int vita_skipdraw = 1;

/* vita.cfg chain=1: quita el limite de la cadena de punos (0 = como el arcade) */
volatile int vita_chain = 0;
