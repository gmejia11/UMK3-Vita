/* vita_log.c -- diagnostics for a real Vita (where there is no TTY).
   A thread writes to ux0:data/umk3/vita_log.txt every 2 s: the time, how many screen swaps
   so far, the heap in use and the last file the game opened. */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <vitaGL.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

volatile unsigned vita_swaps, vita_swaps_done;
extern const char *volatile vita_stage;
extern unsigned vita_cfg_cp, vita_cfg_lp, vita_cfg_th;
extern volatile unsigned vita_alloc_in, vita_alloc_kb, vita_alloc_max_kb, vita_alloc_n;                 /* bumped by __wrap_vglSwapBuffers */
extern char vita_last_open[256];
extern volatile unsigned vita_open_count, vita_open_fail, vita_fast_miss, vita_idx_ms;              /* filled in by __wrap_fopen (vita_fs.c) */

extern volatile int vita_prof;
extern volatile unsigned long long vita_prof_bytes;
extern volatile unsigned long long vita_prof_us[10];
extern volatile unsigned vita_prof_n[10];
void __real_vglSwapBuffers(unsigned char has_commondialog);
void __wrap_vglSwapBuffers(unsigned char has_commondialog) {
  vita_swaps++;
  { unsigned long long p0 = vita_prof ? sceKernelGetProcessTimeWide() : 0;
    __real_vglSwapBuffers(has_commondialog);
    if (vita_prof) { vita_prof_us[4] += sceKernelGetProcessTimeWide() - p0; vita_prof_n[4]++; } }
  vita_swaps_done++;
  { extern void vita_frame_note(void); vita_frame_note(); }
  { extern volatile unsigned vita_gl_err_n, vita_gl_err_last; unsigned e = glGetError(); if (e) { vita_gl_err_n++; vita_gl_err_last = e; } }
  { extern volatile int vita_diag; extern void vita_watch_frame(void); if (vita_diag) vita_watch_frame(); }   /* diagnostics: diag=1 only */
}

/* One line per load (fight, menus) in vita_log.txt: how long it took and where the time went. The prof=1 counters
 * (fopen, fread, pvr, png) only count with prof=1; the pack's always do. */
extern volatile unsigned vita_pak_hits, vita_pak_ms;
extern int vita_pak_loaded(void);
static unsigned g_lb_open, g_lb_fail, g_lb_hits, g_lb_pakms;
static unsigned long long g_lb_us[10];
void vita_load_begin(void) {
  g_lb_open = vita_open_count; g_lb_fail = vita_open_fail; g_lb_hits = vita_pak_hits; g_lb_pakms = vita_pak_ms;
  for (int i = 0; i < 10; i++) g_lb_us[i] = vita_prof_us[i];
}
static void put(const char *s);
void vita_load_end(const char *what, double secs) {
  char line[512];
  snprintf(line, sizeof line,
           "LOAD %s: %.2f s  opens=%u (failed %u)  from_pack=%u in %u ms  pack=%s"
           "  [prof=1: fopen %llu ms, fread %llu ms, pvr %llu ms, png %llu ms, tex16 %llu ms]\n",
           what, secs, vita_open_count - g_lb_open, vita_open_fail - g_lb_fail,
           vita_pak_hits - g_lb_hits, vita_pak_ms - g_lb_pakms, vita_pak_loaded() ? "yes" : "NO",
           (vita_prof_us[8] - g_lb_us[8]) / 1000ull, (vita_prof_us[7] - g_lb_us[7]) / 1000ull,
           (vita_prof_us[5] - g_lb_us[5]) / 1000ull, (vita_prof_us[6] - g_lb_us[6]) / 1000ull,
           (vita_prof_us[9] - g_lb_us[9]) / 1000ull);
  put(line);
}

volatile int vita_gl_ready;                     /* set by vita_plat.c after vglInitExtended */
static SceUID g_main_thid;                    /* the game's main thread */

/* The log is opened ONCE and stays open (it used to be opened and closed on the card for every line, every 2 s, and opening a
 * file on the Vita's SD costs on the order of 1 s). sceIoWrite does not go through stdio's buffer: what was written stays in the
 * file even if the game crashes. If it is over 2 MB at start-up it is kept as vita_log_old.txt and a new one is started.
 * vita.cfg log=0 drops the periodic line (the LOAD lines stay). */
volatile int vita_log_on = 1;
static SceUID g_log_fd = -1;
static volatile int g_log_lock;
static void put(const char *s) {
  while (__sync_lock_test_and_set(&g_log_lock, 1)) sceKernelDelayThread(100);
  if (g_log_fd < 0) {
    SceIoStat st;
    if (sceIoGetstat("ux0:data/umk3/vita_log.txt", &st) >= 0 && st.st_size > 2 * 1024 * 1024) {
      sceIoRemove("ux0:data/umk3/vita_log_old.txt");
      sceIoRename("ux0:data/umk3/vita_log.txt", "ux0:data/umk3/vita_log_old.txt");
    }
    g_log_fd = sceIoOpen("ux0:data/umk3/vita_log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  }
  if (g_log_fd >= 0) sceIoWrite(g_log_fd, s, (SceSize)strlen(s));
  __sync_lock_release(&g_log_lock);
}

static int log_thread(SceSize args, void *argp) {
  (void)args; (void)argp;
  char line[1536];
  for (;;) {
    if (!vita_log_on) { sceKernelDelayThread(2 * 1000 * 1000); continue; }
    struct mallinfo mi = mallinfo();
    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof ti);
    ti.size = sizeof ti;
    sceKernelGetThreadInfo(g_main_thid, &ti);
    snprintf(line, sizeof line, "t=%us swaps=%u heap_used=%dKB heap_total=%dKB opens=%u failed=%u fast=%u idx_ms=%u last=%s main_status=0x%x wait=0x%x run_ms=%u done=%u stage=%s alloc_in=%u alloc_kb=%u alloc_max_kb=%u allocs=%u vram_free=%uKB ram_free=%uKB phy_free=%uKB ext_free=%uKB circ_mb=%u legacy_mb=%u thr_mb=%u prof=%d de=%u/%llu da=%u/%llu en=%u/%llu ti=%u/%llu sw=%llu pvr=%u/%llu png=%u/%llu fr=%u/%llu frb=%llu fo=%u/%llu u16=%u/%llu\n",
             (unsigned)(sceKernelGetProcessTimeWide() / 1000000ull), vita_swaps,
             mi.uordblks / 1024, mi.arena / 1024, vita_open_count, vita_open_fail, vita_fast_miss, vita_idx_ms, vita_last_open,
             (unsigned)ti.status, (unsigned)ti.waitType, (unsigned)(ti.runClocks / 1000ull), vita_swaps_done, vita_stage, vita_alloc_in, vita_alloc_kb, vita_alloc_max_kb, vita_alloc_n,
             (unsigned)((vita_gl_ready ? vglMemFree(VGL_MEM_VRAM) : (size_t)0) / 1024), (unsigned)((vita_gl_ready ? vglMemFree(VGL_MEM_RAM) : (size_t)0) / 1024),
             (unsigned)((vita_gl_ready ? vglMemFree(VGL_MEM_PHYCONT) : (size_t)0) / 1024), (unsigned)((vita_gl_ready ? vglMemFree(VGL_MEM_EXTERNAL) : (size_t)0) / 1024),
             vita_cfg_cp, vita_cfg_lp, vita_cfg_th, vita_prof,
             vita_prof_n[0], vita_prof_us[0], vita_prof_n[1], vita_prof_us[1], vita_prof_n[2], vita_prof_us[2],
             vita_prof_n[3], vita_prof_us[3], vita_prof_us[4],
             vita_prof_n[5], vita_prof_us[5], vita_prof_n[6], vita_prof_us[6], vita_prof_n[7], vita_prof_us[7], vita_prof_bytes,
             vita_prof_n[8], vita_prof_us[8], vita_prof_n[9], vita_prof_us[9]);
    put(line);
    if (vita_prof) {
      extern volatile unsigned vita_face_calls, vita_face_total, vita_face_drawn;
      snprintf(line, sizeof line, "  drops: draw_calls=%u seen=%u drawn=%u\n", vita_face_calls, vita_face_total, vita_face_drawn);
      put(line);
    }
    { extern void vita_slow_drain(void (*out)(const char *)); vita_slow_drain(put); }
    sceKernelDelayThread(2 * 1000 * 1000);
  }
  return 0;
}

__attribute__((constructor)) static void vita_log_start(void) {
  g_main_thid = sceKernelGetThreadId();        /* the constructor runs on the main thread */
  sceIoMkdir("ux0:data/umk3", 0777);
  put("---- start ---- build " __DATE__ " " __TIME__ "\n");
  SceUID th = sceKernelCreateThread("umk3_log", log_thread, 0x10000100, 0x4000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
}
