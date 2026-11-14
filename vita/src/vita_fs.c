/* vita_fs.c
 * 1) El juego abre "res/...", "umk3.ini", etc. con rutas relativas. La Vita no tiene cwd util,
 *    asi que (via -Wl,--wrap) toda ruta relativa se redirige a ux0:data/umk3/.
 * 2) INDICE DE ASSETS: en la Vita real cada apertura de un archivo que NO existe tarda ~130 ms,
 *    y el juego prueba ~12 variantes (.pvr/.PVR/.png/.PNG, res/ y res/Textures/) por textura y
 *    reintenta decenas de veces. Aqui se lista cada carpeta de res/ UNA vez y los archivos que no
 *    estan en la lista fallan al instante, sin tocar la tarjeta. Ademas se resuelve el nombre sin
 *    distinguir mayusculas (como en Windows). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#define ROOT   "ux0:data/umk3/"
#define ASSETS ROOT "res/"

char vita_last_open[256];                                        /* para vita_log.c */
volatile unsigned vita_open_count, vita_open_fail, vita_fast_miss, vita_idx_ms;
extern volatile int vita_prof;
extern volatile unsigned long long vita_prof_us[10];
extern volatile unsigned vita_prof_n[10];

static const char *fix(const char *p, char *buf, size_t n) {
  if (!p || p[0] == '/' || strchr(p, ':')) return p;      /* absoluta o con dispositivo */
  while (p[0] == '.' && p[1] == '/') p += 2;
  snprintf(buf, n, ROOT "%s", p);
  return buf;
}

/* ---------------------------------------------------------------- indice de carpetas */
typedef struct { char *lower; char *actual; } ent_t;
typedef struct { char path[256]; ent_t *e; int n; int ok; } dirindex_t;
#define MAX_DIRS 32
static dirindex_t g_dirs[MAX_DIRS];
static int g_ndirs;
static volatile int g_lock;

static void lock(void)   { while (__sync_lock_test_and_set(&g_lock, 1)) sceKernelDelayThread(100); }
static void unlock(void) { __sync_lock_release(&g_lock); }

static void lower_copy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) dst[i] = (char)tolower((unsigned char)src[i]);
  dst[i] = 0;
}
static int cmp_ent(const void *a, const void *b) {
  return strcmp(((const ent_t *)a)->lower, ((const ent_t *)b)->lower);
}

static dirindex_t *get_dir(const char *dir) {              /* con g_lock tomado */
  for (int i = 0; i < g_ndirs; i++)
    if (strcmp(g_dirs[i].path, dir) == 0) return &g_dirs[i];
  if (g_ndirs >= MAX_DIRS) return NULL;
  dirindex_t *d = &g_dirs[g_ndirs++];
  memset(d, 0, sizeof *d);
  strncpy(d->path, dir, sizeof d->path - 1);
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  SceUID h = sceIoDopen(dir);
  if (h < 0) {                                              /* no se pudo listar: no filtrar... */
    extern int vita_pak_loaded(void);
    if (vita_pak_loaded()) d->ok = 1;                       /* ...salvo con paquete: carpeta ausente, ningun suelto */
    return d;
  }
  int cap = 256;
  d->e = (ent_t *)malloc((size_t)cap * sizeof(ent_t));
  if (!d->e) { sceIoDclose(h); return d; }
  SceIoDirent de;
  for (;;) {
    memset(&de, 0, sizeof de);
    if (sceIoDread(h, &de) <= 0) break;
    if (d->n == cap) {
      cap *= 2;
      ent_t *ne = (ent_t *)realloc(d->e, (size_t)cap * sizeof(ent_t));
      if (!ne) break;
      d->e = ne;
    }
    char lo[256];
    lower_copy(lo, de.d_name, sizeof lo);
    d->e[d->n].lower = strdup(lo);
    d->e[d->n].actual = strdup(de.d_name);
    d->n++;
  }
  sceIoDclose(h);
  qsort(d->e, (size_t)d->n, sizeof(ent_t), cmp_ent);
  d->ok = 1;
  vita_idx_ms += (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000ull);
  return d;
}

/* Siempre rellena out. Devuelve 0 = ese archivo seguro NO existe; 1 = existe, o no se sabe. */
static int asset_resolve(const char *q, char *out, size_t n) {
  snprintf(out, n, "%s", q);
  if (strncmp(q, ASSETS, sizeof(ASSETS) - 1) != 0) return 1;     /* no es un asset */
  const char *slash = strrchr(q, '/');
  if (!slash || !slash[1]) return 1;
  size_t dl = (size_t)(slash - q);
  if (dl >= 256) return 1;
  char dir[256];
  memcpy(dir, q, dl);
  dir[dl] = 0;
  char lo[256];
  lower_copy(lo, slash + 1, sizeof lo);
  lock();
  dirindex_t *d = get_dir(dir);
  int ok = d && d->ok;
  ent_t *f = NULL;
  if (ok) {
    ent_t key = { lo, NULL };
    f = (ent_t *)bsearch(&key, d->e, (size_t)d->n, sizeof(ent_t), cmp_ent);
  }
  unlock();
  if (!ok) return 1;
  if (!f) return 0;
  snprintf(out, n, "%s/%s", dir, f->actual);                      /* mayusculas reales */
  return 1;
}


/* ---------------------------------------------------------------- paquete umk3.pak
 * 3) PAQUETE: abrir un archivo de res/ en la Vita cuesta del orden de 100 ms (res/ tiene ~1900 entradas en UNA carpeta y la
 *    tarjeta recorre el directorio en cada apertura), y una pelea abre cientos: dos personajes con sus escenas, meshsets,
 *    eventos, luces y texturas, mas los efectos. vita/make_pak.py junta los archivos de solo lectura en
 *    ux0:data/umk3/umk3.pak; aqui se lee su indice UNA vez, el archivo queda abierto, y cada fopen("res/...", "r") de algo que
 *    este en el paquete es un sceIoPread a memoria, sin tocar el directorio. Si umk3.pak no existe, todo va como antes.
 *    Lo que el paquete no tiene (partidas guardadas, musica .mp3, mods) sigue leyendose suelto.
 *    vita.cfg pak=0 lo desactiva. Formato (little endian):
 *      "UMK3PAK1"  u32 n  u32 names_bytes  { u32 name_off, u32 data_off, u32 size } x n  nombres (minusculas, ordenados)  datos */
typedef struct { uint32_t name, off, size; } pak_ent_t;
static SceUID g_pak_fd = -1;
static pak_ent_t *g_pak_e;
static char *g_pak_names;
static uint32_t g_pak_n;
static unsigned char *g_pak_dead;          /* 1 = el juego escribio ese archivo suelto: ya no se sirve del paquete */
static int g_pak_tried;
volatile unsigned vita_pak_hits, vita_pak_ms;
volatile int vita_pak_on = 1;              /* vita_plat.c lo pone desde vita.cfg (pak=) */

int vita_pak_loaded(void) { return g_pak_fd >= 0; }

static void pak_init(void) {               /* con g_lock tomado */
  g_pak_tried = 1;
  {                                        /* pak=0 en vita.cfg: puede pedirse antes de que vita_plat.c lea la configuracion */
    char cfg[4096];
    SceUID cf = sceIoOpen(ROOT "vita.cfg", SCE_O_RDONLY, 0);
    if (cf >= 0) {
      int n = sceIoRead(cf, cfg, sizeof cfg - 1);
      sceIoClose(cf);
      if (n > 0) {
        cfg[n] = 0;
        for (const char *l = cfg; l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
          if (strncmp(l, "pak=", 4) == 0) vita_pak_on = atoi(l + 4);
      }
    }
  }
  if (!vita_pak_on) return;
  SceUID fd = sceIoOpen(ROOT "umk3.pak", SCE_O_RDONLY, 0);
  if (fd < 0) return;
  char hdr[16];
  if (sceIoPread(fd, hdr, 16, 0) != 16 || memcmp(hdr, "UMK3PAK1", 8) != 0) { sceIoClose(fd); return; }
  uint32_t n, nb;
  memcpy(&n, hdr + 8, 4);
  memcpy(&nb, hdr + 12, 4);
  if (n == 0 || n > 100000 || nb > (16u << 20)) { sceIoClose(fd); return; }
  size_t eb = (size_t)n * sizeof(pak_ent_t);
  pak_ent_t *e = (pak_ent_t *)malloc(eb);
  char *names = (char *)malloc(nb + 1);
  unsigned char *dead = (unsigned char *)calloc(n, 1);
  if (!e || !names || !dead
      || sceIoPread(fd, e, (SceSize)eb, 16) != (int)eb
      || sceIoPread(fd, names, (SceSize)nb, 16 + (SceOff)eb) != (int)nb) {
    free(e); free(names); free(dead); sceIoClose(fd); return;
  }
  names[nb] = 0;
  for (uint32_t i = 0; i < n; i++)
    if (e[i].name >= nb) { free(e); free(names); free(dead); sceIoClose(fd); return; }
  g_pak_fd = fd; g_pak_e = e; g_pak_names = names; g_pak_n = n; g_pak_dead = dead;
}

/* q: ruta ya completa (ux0:data/umk3/...). Devuelve el indice de la entrada o -1. */
static int pak_find(const char *q) {
  if (strncmp(q, ASSETS, sizeof(ASSETS) - 1) != 0) return -1;   /* solo res/: vita.cfg y demas, nunca del paquete */
  char lo[256];
  lower_copy(lo, q + sizeof(ROOT) - 1, sizeof lo);
  lock();
  if (!g_pak_tried) pak_init();
  int r = -1;
  if (g_pak_fd >= 0) {
    uint32_t a = 0, b = g_pak_n;
    while (a < b) {
      uint32_t m = (a + b) / 2;
      int c = strcmp(g_pak_names + g_pak_e[m].name, lo);
      if (c == 0) { r = g_pak_dead[m] ? -1 : (int)m; break; }
      if (c < 0) a = m + 1; else b = m;
    }
  }
  unlock();
  return r;
}

static void pak_kill(const char *q) {       /* el juego crea/escribe ese archivo: desde ahora, el suelto */
  int i = pak_find(q);
  if (i >= 0) g_pak_dead[i] = 1;
}

typedef struct { unsigned char *buf; uint32_t size, pos; } pak_file_t;
/* funopen de newlib: lectura, sin escritura, busqueda y cierre (el cierre libera la copia en memoria) */
#ifdef __LARGE64_FILES
typedef _fpos64_t pak_pos_t;
#else
typedef fpos_t pak_pos_t;
#endif
static int pak_read(void *c, char *buf, _READ_WRITE_BUFSIZE_TYPE n) {
  pak_file_t *f = (pak_file_t *)c;
  uint32_t left = f->size - f->pos;
  uint32_t k = (uint32_t)n > left ? left : (uint32_t)n;
  memcpy(buf, f->buf + f->pos, k);
  f->pos += k;
  return (int)k;
}
static pak_pos_t pak_seek(void *c, pak_pos_t off, int wh) {
  pak_file_t *f = (pak_file_t *)c;
  long long p = wh == SEEK_SET ? 0 : wh == SEEK_CUR ? (long long)f->pos : (long long)f->size;
  p += (long long)off;
  if (p < 0 || p > (long long)f->size) { errno = EINVAL; return (pak_pos_t)-1; }
  f->pos = (uint32_t)p;
  return (pak_pos_t)p;
}
static int pak_close(void *c) {
  pak_file_t *f = (pak_file_t *)c;
  free(f->buf);
  free(f);
  return 0;
}

static FILE *pak_open(int i, const char *mode) {
  const pak_ent_t *e = &g_pak_e[i];
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  pak_file_t *f = (pak_file_t *)malloc(sizeof *f);
  if (!f) return NULL;
  f->size = e->size; f->pos = 0;
  f->buf = (unsigned char *)malloc(e->size ? e->size : 1);
  if (!f->buf) { free(f); return NULL; }
  uint32_t got = 0;
  while (got < e->size) {                  /* en trozos: sceIoPread no garantiza leerlo todo de una vez */
    uint32_t want = e->size - got;
    if (want > (4u << 20)) want = 4u << 20;
    int r = sceIoPread(g_pak_fd, f->buf + got, want, (SceOff)e->off + got);
    if (r <= 0) { free(f->buf); free(f); return NULL; }
    got += (uint32_t)r;
  }
  (void)mode;
  FILE *fp = funopen(f, pak_read, NULL, pak_seek, pak_close);
  if (!fp) { free(f->buf); free(f); return NULL; }
  vita_pak_hits++;
  vita_pak_ms += (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000ull);
  return fp;
}

/* Si el juego CREA un archivo dentro de res/, el indice de esa carpeta queda viejo: se desactiva. */
static void invalidate_dir(const char *q) {
  if (strncmp(q, ASSETS, sizeof(ASSETS) - 1) != 0) return;
  const char *slash = strrchr(q, '/');
  if (!slash) return;
  size_t dl = (size_t)(slash - q);
  if (dl >= 256) return;
  char dir[256];
  memcpy(dir, q, dl);
  dir[dl] = 0;
  lock();
  for (int i = 0; i < g_ndirs; i++)
    if (strcmp(g_dirs[i].path, dir) == 0) g_dirs[i].ok = 0;
  unlock();
}

/* ---------------------------------------------------------------- envoltorios */
FILE *__real_fopen(const char *, const char *);

/* Registros de DIAGNOSTICO (ani_bad, sleep_bad, cursor_bad, ani_site, corrupt, threads): abrir y cerrar un archivo en la SD cuesta
 * del orden de 1 SEGUNDO en la Vita, y cada evento caia en pleno combo (fps de 6). Apagados por defecto; diag=1 en vita.cfg
 * los reactiva. El juego no se entera: fopen devuelve NULL y los registros ya contemplan ese caso. */
volatile int vita_diag = 0;
static int vita_diag_file(const char *q) {
  static const char *const names[] = { "ani_bad.txt", "sleep_bad.txt", "cursor_bad.txt", "ani_site.txt", "corrupt.txt", "threads.txt", 0 };
  const char *b = strrchr(q, '/');
  b = b ? b + 1 : q;
  for (int k = 0; names[k]; k++) if (!strcmp(b, names[k])) return 1;
  return 0;
}
int __real_open(const char *, int, ...);
int __real_stat(const char *, struct stat *);
int __real_access(const char *, int);
DIR *__real_opendir(const char *);
int __real_mkdir(const char *, mode_t);
int __real_remove(const char *);
int __real_rename(const char *, const char *);

/* DIAGNOSTICO: anota en ux0:data/umk3/missing.txt los archivos que el juego pidio y NO existen
   (sin contar las variantes .pvr/.png que prueba a ciegas). Cada nombre una sola vez. */
static char g_missed[300][180];
static int g_nmissed;
static void note_miss(const char *q) {
  if (!vita_diag) return;                   /* escribir en la SD a mitad de partida da tirones: solo con diag=1 */
  const char *dot = strrchr(q, '.');
  if (dot && (strcasecmp(dot, ".pvr") == 0 || strcasecmp(dot, ".png") == 0)) return;
  lock();
  int seen = 0;
  for (int i = 0; i < g_nmissed && !seen; i++) seen = strcmp(g_missed[i], q) == 0;
  int idx = -1;
  if (!seen && g_nmissed < 300) { idx = g_nmissed++; snprintf(g_missed[idx], sizeof g_missed[idx], "%s", q); }
  unlock();
  if (idx >= 0) {
    FILE *f = __real_fopen("ux0:data/umk3/missing.txt", "a");
    if (f) { fprintf(f, "%s\n", g_missed[idx]); fclose(f); }
  }
}

FILE *__wrap_fopen(const char *p, const char *m) {
  char b[512], c[512];
  const char *q = fix(p, b, sizeof b);
  vita_open_count++;
  if (!vita_diag && m && m[0] != 'r' && vita_diag_file(q)) { errno = EACCES; return NULL; }
  strncpy(vita_last_open, q, sizeof vita_last_open - 1);
  if (m && m[0] == 'r' && !strchr(m, '+')) {                      /* solo lectura */
    int pi = pak_find(q);
    if (pi >= 0) {
      FILE *pf = pak_open(pi, "rb");
      if (pf) return pf;
    }
    if (!asset_resolve(q, c, sizeof c)) {
      vita_fast_miss++; vita_open_fail++; note_miss(q); errno = ENOENT; return NULL;
    }
    q = c;
  }
  unsigned long long _p0 = vita_prof ? sceKernelGetProcessTimeWide() : 0;
  FILE *f = __real_fopen(q, m);
  if (vita_prof) { vita_prof_us[8] += sceKernelGetProcessTimeWide() - _p0; vita_prof_n[8]++; }
  if (!f) { vita_open_fail++; if (m && m[0] == 'r') note_miss(q); }
  else if (!(m && m[0] == 'r' && !strchr(m, '+'))) { invalidate_dir(q); pak_kill(q); }
  return f;
}
int __wrap_open(const char *p, int fl, ...) {
  char b[512], c[512];
  va_list ap; va_start(ap, fl); int mode = va_arg(ap, int); va_end(ap);
  const char *q = fix(p, b, sizeof b);
  if ((fl & O_ACCMODE) == O_RDONLY) {
    if (!asset_resolve(q, c, sizeof c)) { errno = ENOENT; return -1; }
    q = c;
  }
  int r = __real_open(q, fl, mode);
  if (r >= 0 && (fl & O_ACCMODE) != O_RDONLY) { invalidate_dir(q); pak_kill(q); }
  return r;
}
int __wrap_stat(const char *p, struct stat *st) {
  char b[512], c[512];
  const char *q = fix(p, b, sizeof b);
  int pi = pak_find(q);
  if (pi >= 0 && st) {
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFREG | 0444;
    st->st_size = (off_t)g_pak_e[pi].size;
    return 0;
  }
  if (!asset_resolve(q, c, sizeof c)) { errno = ENOENT; return -1; }
  return __real_stat(c, st);
}
int __wrap_access(const char *p, int m) {
  char b[512], c[512];
  const char *q = fix(p, b, sizeof b);
  if (!(m & W_OK) && pak_find(q) >= 0) return 0;
  if (!asset_resolve(q, c, sizeof c)) { errno = ENOENT; return -1; }
  return __real_access(c, m);
}
DIR *__wrap_opendir(const char *p) { char b[512]; return __real_opendir(fix(p, b, sizeof b)); }
int __wrap_mkdir(const char *p, mode_t m) { char b[512]; return __real_mkdir(fix(p, b, sizeof b), m); }
int __wrap_remove(const char *p) { char b[512]; const char *q = fix(p, b, sizeof b); pak_kill(q); invalidate_dir(q); return __real_remove(q); }
int __wrap_rename(const char *a, const char *c) {
  char b1[512], b2[512];
  const char *qa = fix(a, b1, sizeof b1), *qc = fix(c, b2, sizeof b2);
  pak_kill(qa); pak_kill(qc); invalidate_dir(qa); invalidate_dir(qc);
  return __real_rename(qa, qc);
}
