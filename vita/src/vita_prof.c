/* vita_prof.c -- PERFILADOR. Mide cuanto tarda y cuantas veces se llama a las funciones de dibujo de vitaGL
 * (glDrawElements, glDrawArrays, glEnd = cada quad del modo inmediato, glTexImage2D) y el tiempo dentro de
 * vglSwapBuffers. Se activa con prof=1 en ux0:data/umk3/vita.cfg; vita_log.c imprime los acumulados. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <psp2/kernel/processmgr.h>

volatile int vita_prof = 0;
volatile unsigned long long vita_prof_us[10];     /* 0 DrawElements 1 DrawArrays 2 End 3 TexImage2D 4 Swap 5 pvr_load 6 png_load 7 fread 8 fopen 9 conversion16 */
volatile unsigned vita_prof_n[10];
static void cs_add(unsigned kind, void *ra, unsigned long long us);
void vita_note_drain(void (*out)(const char *));
#define CSITE(k) do { if (vita_prof) cs_add(k, __builtin_return_address(0), sceKernelGetProcessTimeWide() - t0); } while (0)

#define T0()  unsigned long long t0 = vita_prof ? sceKernelGetProcessTimeWide() : 0
#define T1(k) do { if (vita_prof) { vita_prof_us[k] += sceKernelGetProcessTimeWide() - t0; vita_prof_n[k]++; } } while (0)

void __real_glDrawElements(unsigned mode, int count, unsigned type, const void *indices);
void __wrap_glDrawElements(unsigned mode, int count, unsigned type, const void *indices) {
  T0(); __real_glDrawElements(mode, count, type, indices); T1(0); CSITE(0);
}
void __real_glDrawArrays(unsigned mode, int first, int count);
void __wrap_glDrawArrays(unsigned mode, int first, int count) {
  T0(); __real_glDrawArrays(mode, first, count); T1(1); CSITE(1);
}
void __real_glEnd(void);
void __wrap_glEnd(void) {
  T0(); __real_glEnd(); T1(2); CSITE(2);
}
void __real_glTexImage2D(unsigned target, int level, int ifmt, int w, int h, int border, unsigned fmt, unsigned type, const void *data);
void __wrap_glTexImage2D(unsigned target, int level, int ifmt, int w, int h, int border, unsigned fmt, unsigned type, const void *data) {
  T0(); __real_glTexImage2D(target, level, ifmt, w, h, border, fmt, type, data); T1(3);
}

volatile unsigned long long vita_prof_bytes;     /* bytes leidos con fread */

int __real_lime_pvr_load(const void *path, void *out);
int __wrap_lime_pvr_load(const void *path, void *out) {
  T0(); int r = __real_lime_pvr_load(path, out); T1(5); return r;
}
int __real_lime_png_load(const void *path, void *px, void *w, void *h);
int __wrap_lime_png_load(const void *path, void *px, void *w, void *h) {
  T0(); int r = __real_lime_png_load(path, px, w, h); T1(6); return r;
}
unsigned __real_fread(void *p, unsigned size, unsigned n, void *f);
unsigned __wrap_fread(void *p, unsigned size, unsigned n, void *f) {
  T0(); unsigned r = __real_fread(p, size, n, f);
  if (vita_prof) vita_prof_bytes += (unsigned long long)r * size;
  T1(7); return r;
}

/* ---- rutinas de hilo LENTAS (solo con prof=1) ----
 * mk3_update cronometra cada llamada a una rutina de hilo; las que pasan de 8 ms van a un anillo en memoria y el hilo del registro
 * (vita_log.c) las escribe en vita_log.txt: el hilo del juego no abre ningun archivo. Tambien cuenta errores de OpenGL. */
volatile unsigned vita_gl_err_n, vita_gl_err_last;
struct vita_slow_ev { unsigned long long us; unsigned handler, token, frame; };
static struct vita_slow_ev g_slow[32];
static volatile unsigned g_slow_w, g_slow_r, g_slow_lost, g_gl_err_seen;
extern volatile unsigned vita_swaps;

unsigned long long vita_slow_begin(void) { return vita_prof ? sceKernelGetProcessTimeWide() : 0; }
void vita_slow_end(unsigned long long t0, void *f, unsigned token) {
  if (!t0) return;
  unsigned long long dt = sceKernelGetProcessTimeWide() - t0;
  if (dt < 8000ull) return;
  if (g_slow_w - g_slow_r >= 32) { g_slow_lost++; return; }
  struct vita_slow_ev *e = &g_slow[g_slow_w & 31];
  e->us = dt; e->handler = (unsigned)(uintptr_t)f; e->token = token; e->frame = vita_swaps;
  __sync_synchronize();
  g_slow_w++;
}
/* lo llama el hilo del registro cada 2 s: escribe una linea por cada evento pendiente */
void vita_slow_drain(void (*out)(const char *)) {
  char l[200];
  if (vita_prof) {
    extern volatile unsigned vita_ticks_total, vita_ticks_skipped;
    snprintf(l, sizeof l, "  pasos: logica=%u sin_dibujo=%u\n", vita_ticks_total, vita_ticks_skipped);
    out(l);
  }
  vita_note_drain(out);
  { extern void vita_callers_drain(void (*out)(const char *)); vita_callers_drain(out); }
  if (vita_prof) {
    extern void vita_times_drain(void (*out)(const char *));
    vita_times_drain(out);
  }
  while (g_slow_r != g_slow_w) {
    struct vita_slow_ev e = g_slow[g_slow_r & 31];
    g_slow_r++;
    snprintf(l, sizeof l, "  lenta: fotograma=%u rutina=0x%08x token=0x%x ms=%u ancla(vita_slow_end)=0x%08x\n",
             e.frame, e.handler, e.token, (unsigned)(e.us / 1000ull), (unsigned)(uintptr_t)&vita_slow_end);
    out(l);
  }
  if (g_slow_lost) { snprintf(l, sizeof l, "  lenta: (%u eventos perdidos por no caber en el anillo)\n", g_slow_lost); out(l); g_slow_lost = 0; }
  if (vita_gl_err_n != g_gl_err_seen) {
    snprintf(l, sizeof l, "  opengl: errores=%u ultimo=0x%x\n", vita_gl_err_n, vita_gl_err_last);
    out(l); g_gl_err_seen = vita_gl_err_n;
  }
}

/* ---- QUIEN llama a glDrawElements / glDrawArrays / glEnd (solo con prof=1) ----
 * Cuenta, por direccion de retorno (la funcion del juego que dibuja), cuantas llamadas hace y cuanto tardan; el hilo del registro
 * escribe las 8 que mas tiempo suman cada 2 s ("quien:") y se traducen a nombres con slow_symbols/callers_symbols. */
struct vita_cs { unsigned addr, kind, n; unsigned long long us; };
static struct vita_cs g_cs[64];
static void cs_add(unsigned kind, void *ra, unsigned long long us) {
  unsigned a = (unsigned)(uintptr_t)ra, h = ((a >> 1) * 2654435761u) >> 26;
  for (unsigned k = 0; k < 64; k++) {
    struct vita_cs *e = &g_cs[(h + k) & 63];
    if (e->addr == 0) { e->addr = a; e->kind = kind; }
    if (e->addr == a && e->kind == kind) { e->n++; e->us += us; return; }
  }
}
void vita_callers_drain(void (*out)(const char *)) {
  struct vita_cs t[64]; char l[200];
  memcpy(t, g_cs, sizeof t); memset(g_cs, 0, sizeof g_cs);
  for (int pass = 0; pass < 8; pass++) {
    int best = -1;
    for (int k = 0; k < 64; k++) if (t[k].addr && (best < 0 || t[k].us > t[best].us)) best = k;
    if (best < 0) break;
    snprintf(l, sizeof l, "  quien: tipo=%s addr=0x%08x n=%u ms=%u ancla(vita_slow_end)=0x%08x\n",
             t[best].kind == 0 ? "DE" : t[best].kind == 1 ? "DA" : "End", t[best].addr, t[best].n,
             (unsigned)(t[best].us / 1000ull), (unsigned)(uintptr_t)&vita_slow_end);
    out(l); t[best].addr = 0;
  }
}

/* ---- TIEMPOS por paso y por fotograma (solo con prof=1) ----
 * mk3_update = la logica del juego; GameCodeMain = un paso entero (logica + preparar y emitir todo el dibujo). Se acumulan con el
 * maximo, y el tiempo ENTRE swaps con su maximo y cuantos pasan de 33/66/100 ms (los tirones que un promedio de 2 s esconde). */
long __real_mk3_update(const long *joy, void **out);
void __real_GameCodeMain(void);
struct vita_ph { unsigned long long us; unsigned n, max; };
static struct vita_ph g_ph_mk, g_ph_gcm;
static unsigned long long g_last_swap_us;
static unsigned g_fmax, g_f33, g_f66, g_f100, g_fn;
static void ph_add(struct vita_ph *p, unsigned long long t0) {
  unsigned d = (unsigned)(sceKernelGetProcessTimeWide() - t0);
  p->us += d; p->n++; if (d > p->max) p->max = d;
}
long __wrap_mk3_update(const long *joy, void **out) {
  if (!vita_prof) return __real_mk3_update(joy, out);
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  long r = __real_mk3_update(joy, out);
  ph_add(&g_ph_mk, t0);
  return r;
}
/* Un PASO que tarda >= 60 ms es un tiron: se anota (en memoria; lo escribe el hilo del registro) cuanto de ese paso se fue en las
 * llamadas de dibujo, en subir texturas, en leer archivos, y si era un paso sin dibujo. Separa "la GPU/OpenGL se para" de "el juego
 * (CPU) tarda" y de "disco". */
/* Tiempo TOTAL (CPU + OpenGL) dentro de las dos funciones que dibujan las mallas de un luchador: el cuerpo (Indexed) y los
 * accesorios (Single). Se miden por paso para ver si el tiron esta dentro de ellas. Mismas firmas que en lime.h (hard-float). */
void __real_LIME_RenderMeshSingleIndexed(void *frame, void *tex, float grey, const void *fade, long second);
void __real_LIME_RenderMeshSingle(void *mesh, void *t0, void *t1, float alpha, long flags);
static volatile unsigned long long g_msi_us, g_ms_us;
static volatile unsigned g_msi_n, g_ms_n, g_msi_max, g_ms_max;
void __wrap_LIME_RenderMeshSingleIndexed(void *frame, void *tex, float grey, const void *fade, long second) {
  if (!vita_prof) { __real_LIME_RenderMeshSingleIndexed(frame, tex, grey, fade, second); return; }
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  __real_LIME_RenderMeshSingleIndexed(frame, tex, grey, fade, second);
  unsigned d = (unsigned)(sceKernelGetProcessTimeWide() - t0);
  g_msi_us += d; g_msi_n++; if (d > g_msi_max) g_msi_max = d;
}
void __wrap_LIME_RenderMeshSingle(void *mesh, void *t0p, void *t1, float alpha, long flags) {
  if (!vita_prof) { __real_LIME_RenderMeshSingle(mesh, t0p, t1, alpha, flags); return; }
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  __real_LIME_RenderMeshSingle(mesh, t0p, t1, alpha, flags);
  unsigned d = (unsigned)(sceKernelGetProcessTimeWide() - t0);
  g_ms_us += d; g_ms_n++; if (d > g_ms_max) g_ms_max = d;
}
extern int port_vita_nodraw;
extern volatile unsigned vita_ticks_total;
/* get_tsound (sound.c) hace un printf("Tsound 0x%x %d") EN CADA LLAMADA ("it ships", dice el decompilado). Se cuentan las llamadas a
 * printf/puts/putchar y su tiempo, y las llamadas a get_tsound, dentro de cada paso. vita.cfg quiet=1 descarta TODO lo que el juego
 * escribe por printf/puts/putchar (experimento: si los tirones desaparecen, era esa salida). */
#include <stdarg.h>
volatile int vita_quiet = 0;
int __real_puts(const char *s);
int __real_putchar(int c);
long __real_get_tsound(long id);
static volatile unsigned long long g_pf_us;
static volatile unsigned g_pf_n, g_ts_n;
/* Avisos del juego que importan en la Vita (alli printf no se ve): "separate us:" (la regla de la cadena separa a los luchadores,
 * issue #114), "corner trap:" (la regla de la esquina) y "death scream:". Se copian a un anillo en memoria y el hilo del registro los
 * escribe en vita_log.txt con el fotograma; el hilo del juego no toca la tarjeta. Siempre activos (con quiet=1 tambien). */
static char g_note[32][160];
static unsigned g_note_frame[32];
static volatile unsigned g_note_w, g_note_r, g_note_lost;
static int note_wanted(const char *fmt) {
  return !strncmp(fmt, "separate us:", 12) || !strncmp(fmt, "corner trap:", 12) || !strncmp(fmt, "death scream:", 13)
      || !strncmp(fmt, "kombo push:", 11);
}
static void note_add(const char *fmt, va_list ap) {
  if (g_note_w - g_note_r >= 32) { g_note_lost++; return; }
  unsigned k = g_note_w & 31;
  vsnprintf(g_note[k], sizeof g_note[k], fmt, ap);
  g_note_frame[k] = vita_swaps;
  __sync_synchronize();
  g_note_w++;
}
void vita_note_drain(void (*out)(const char *)) {
  char l[200];
  while (g_note_r != g_note_w) {
    unsigned k = g_note_r & 31;
    size_t n = strlen(g_note[k]);
    snprintf(l, sizeof l, "  juego: fotograma=%u %s%s", g_note_frame[k], g_note[k], (n && g_note[k][n - 1] == '\n') ? "" : "\n");
    __sync_synchronize();
    g_note_r++;
    out(l);
  }
  if (g_note_lost) { snprintf(l, sizeof l, "  juego: (%u avisos perdidos, anillo lleno)\n", g_note_lost); out(l); g_note_lost = 0; }
}
int __wrap_printf(const char *fmt, ...) {
  va_list ap; int r; unsigned long long t0 = 0;
  if (fmt && note_wanted(fmt)) { va_start(ap, fmt); note_add(fmt, ap); va_end(ap); }
  if (vita_quiet) return 0;
  if (vita_prof) t0 = sceKernelGetProcessTimeWide();
  va_start(ap, fmt); r = vprintf(fmt, ap); va_end(ap);
  if (vita_prof) { g_pf_us += sceKernelGetProcessTimeWide() - t0; g_pf_n++; }
  return r;
}
int __wrap_puts(const char *str) {
  int r; unsigned long long t0 = 0;
  if (vita_quiet) return 0;
  if (vita_prof) t0 = sceKernelGetProcessTimeWide();
  r = __real_puts(str);
  if (vita_prof) { g_pf_us += sceKernelGetProcessTimeWide() - t0; g_pf_n++; }
  return r;
}
int __wrap_putchar(int c) {
  int r; unsigned long long t0 = 0;
  if (vita_quiet) return c;
  if (vita_prof) t0 = sceKernelGetProcessTimeWide();
  r = __real_putchar(c);
  if (vita_prof) { g_pf_us += sceKernelGetProcessTimeWide() - t0; g_pf_n++; }
  return r;
}
long __wrap_get_tsound(long id) { g_ts_n++; return __real_get_tsound(id); }
struct vita_hitch { unsigned dur, de, da, en, tex, rd, op, dec, nodraw, tick, frame, msi, ms, pf, pfn, tsn; };
static struct vita_hitch g_hit[16];
static volatile unsigned g_hit_w, g_hit_r, g_hit_lost;
extern volatile unsigned vita_swaps;
void __wrap_GameCodeMain(void) {
  if (!vita_prof) { __real_GameCodeMain(); return; }
  unsigned long long u0[10], msi0 = g_msi_us, ms0 = g_ms_us, pf0 = g_pf_us; unsigned pfn0 = g_pf_n, ts0 = g_ts_n; int k; unsigned nd = (unsigned)port_vita_nodraw;
  for (k = 0; k < 10; k++) u0[k] = vita_prof_us[k];
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  __real_GameCodeMain();
  unsigned long long dur = sceKernelGetProcessTimeWide() - t0;
  ph_add(&g_ph_gcm, t0);
  if (dur >= 60000ull) {
    if (g_hit_w - g_hit_r >= 16) { g_hit_lost++; return; }
    struct vita_hitch *h = &g_hit[g_hit_w & 15];
    h->dur = (unsigned)dur;
    h->de = (unsigned)(vita_prof_us[0] - u0[0]); h->da = (unsigned)(vita_prof_us[1] - u0[1]); h->en = (unsigned)(vita_prof_us[2] - u0[2]);
    h->tex = (unsigned)(vita_prof_us[3] - u0[3]); h->rd = (unsigned)(vita_prof_us[7] - u0[7]); h->op = (unsigned)(vita_prof_us[8] - u0[8]);
    h->dec = (unsigned)((vita_prof_us[5] - u0[5]) + (vita_prof_us[6] - u0[6]) + (vita_prof_us[9] - u0[9]));
    h->nodraw = nd; h->tick = vita_ticks_total; h->frame = vita_swaps;
    h->msi = (unsigned)(g_msi_us - msi0); h->ms = (unsigned)(g_ms_us - ms0);
    h->pf = (unsigned)(g_pf_us - pf0); h->pfn = g_pf_n - pfn0; h->tsn = g_ts_n - ts0;
    __sync_synchronize();
    g_hit_w++;
  }
}
void vita_frame_note(void) {
  unsigned long long now;
  if (!vita_prof) return;
  now = sceKernelGetProcessTimeWide();
  if (g_last_swap_us) {
    unsigned d = (unsigned)(now - g_last_swap_us);
    g_fn++; if (d > g_fmax) g_fmax = d;
    if (d > 33000u) g_f33++;
    if (d > 66000u) g_f66++;
    if (d > 100000u) g_f100++;
  }
  g_last_swap_us = now;
}
void vita_times_drain(void (*out)(const char *)) {
  char l[300];
  struct vita_ph mk = g_ph_mk, gc = g_ph_gcm;
  unsigned fmax = g_fmax, f33 = g_f33, f66 = g_f66, f100 = g_f100, fn = g_fn;
  memset(&g_ph_mk, 0, sizeof g_ph_mk); memset(&g_ph_gcm, 0, sizeof g_ph_gcm);
  g_fmax = g_f33 = g_f66 = g_f100 = g_fn = 0;
  snprintf(l, sizeof l, "  tiempos: mk3_update n=%u us=%llu max=%u | GameCodeMain n=%u us=%llu max=%u | swaps n=%u max_us=%u >33ms=%u >66ms=%u >100ms=%u\n",
           mk.n, mk.us, mk.max, gc.n, gc.us, gc.max, fn, fmax, f33, f66, f100);
  out(l);
  {
    extern void vita_au_drain(void (*out)(const char *));
    vita_au_drain(out);
  }
  while (g_hit_r != g_hit_w) {
    struct vita_hitch h = g_hit[g_hit_r & 15];
    g_hit_r++;
    snprintf(l, sizeof l, "  tiron: paso=%u fotograma=%u dur_us=%u dibujo_us=%u (DE %u DA %u End %u) texup_us=%u leer_us=%u abrir_us=%u decod_us=%u sin_dibujo=%u malla_cuerpo_us=%u malla_accesorios_us=%u printf_us=%u printf_n=%u tsound_n=%u\n",
             h.tick, h.frame, h.dur, h.de + h.da + h.en, h.de, h.da, h.en, h.tex, h.rd, h.op, h.dec, h.nodraw, h.msi, h.ms, h.pf, h.pfn, h.tsn);
    out(l);
  }
  if (g_hit_lost) { snprintf(l, sizeof l, "  tiron: (%u perdidos, anillo lleno)\n", g_hit_lost); out(l); g_hit_lost = 0; }
}

/* ---- cache de sonidos convertidos (sdl_audio.c) y tiempo de las conversiones ---- */
volatile int vita_sndcache = 1, vita_sndcache_mb = 16;
struct vita_snd { const void *pcm; int frames, rate; unsigned sum; void *buf; unsigned len; };
static struct vita_snd g_snd[256];
static unsigned g_snd_n, g_snd_bytes;
static volatile unsigned g_au_conv_n, g_au_hit, g_au_max;
static volatile unsigned long long g_au_us;
static unsigned snd_sum(const unsigned char *p, int frames) {
  unsigned h = 2166136261u; int step = frames > 64 ? frames / 64 : 1, k;
  for (k = 0; k < frames; k += step) h = (h ^ p[k]) * 16777619u;
  return h ^ (unsigned)frames;
}
int vita_snd_get(const void *pcm, int frames, int rate, void **buf, unsigned *len) {
  unsigned k, sum;
  if (!vita_sndcache || !pcm || frames <= 0) return 0;
  sum = snd_sum((const unsigned char *)pcm, frames);
  for (k = 0; k < g_snd_n; k++) {
    struct vita_snd *e = &g_snd[k];
    if (e->pcm == pcm && e->frames == frames && e->rate == rate && e->sum == sum) { *buf = e->buf; *len = e->len; g_au_hit++; return 1; }
  }
  return 0;
}
/* 1 = la cache se queda con el buffer (el llamador ya no lo libera); 0 = no cabe, sigue siendo del llamador */
int vita_snd_put(const void *pcm, int frames, int rate, void *buf, unsigned len) {
  struct vita_snd *e;
  if (!vita_sndcache || !pcm || !buf || frames <= 0 || g_snd_n >= 256) return 0;
  if ((unsigned long long)g_snd_bytes + len > (unsigned long long)(vita_sndcache_mb > 0 ? vita_sndcache_mb : 0) * 1048576ull) return 0;
  e = &g_snd[g_snd_n++];
  e->pcm = pcm; e->frames = frames; e->rate = rate; e->sum = snd_sum((const unsigned char *)pcm, frames); e->buf = buf; e->len = len;
  g_snd_bytes += len;
  return 1;
}
unsigned long long vita_au_begin(void) { return sceKernelGetProcessTimeWide(); }
void vita_au_end(unsigned long long t0) {
  unsigned d = (unsigned)(sceKernelGetProcessTimeWide() - t0);
  g_au_us += d; g_au_conv_n++; if (d > g_au_max) g_au_max = d;
}
void vita_au_drain(void (*out)(const char *)) {
  char l[220];
  unsigned n = g_au_conv_n, hit = g_au_hit, mx = g_au_max; unsigned long long us = g_au_us;
  g_au_conv_n = g_au_hit = g_au_max = 0; g_au_us = 0;
  snprintf(l, sizeof l, "  audio: conversiones=%u us=%llu max_us=%u cache_aciertos=%u cache_entradas=%u cache_bytes=%u\n", n, us, mx, hit, g_snd_n, g_snd_bytes);
  out(l);
}

/* ---- conversion rapida de efectos: U8 mono a `rate` -> S16 estereo a 44100 Hz, interpolacion lineal ---- */
volatile int vita_sndfast = 1;
int vita_snd_fast_frames(int frames, int rate) {
  long long o;
  if (frames <= 0 || rate <= 0) return 0;
  o = ((long long)frames * 44100 + rate / 2) / rate;
  if (o <= 0 || o > (1 << 24)) return 0;
  return (int)o;
}
void vita_snd_fast_convert(const unsigned char *pcm, int frames, int rate, short *dst, int dst_frames) {
  unsigned long long step = (((unsigned long long)rate) << 16) / 44100ull, pos = 0;
  int i;
  for (i = 0; i < dst_frames; i++, pos += step) {
    unsigned idx = (unsigned)(pos >> 16);
    int frac = (int)(pos & 0xFFFF), s0, s1, s;
    if (idx >= (unsigned)frames) idx = (unsigned)frames - 1;
    s0 = ((int)pcm[idx] - 128) << 8;
    s1 = (idx + 1 < (unsigned)frames) ? (((int)pcm[idx + 1] - 128) << 8) : s0;
    s = s0 + (int)(((long long)(s1 - s0) * frac) >> 16);
    dst[2 * i] = (short)s; dst[2 * i + 1] = (short)s;
  }
}
