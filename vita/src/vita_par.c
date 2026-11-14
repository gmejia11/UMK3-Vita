/* vita_par.c -- a thread pool for lime_par_for (texture decoding) on the Vita.
 * Enabled with par=1 in ux0:data/umk3/vita.cfg; off, everything goes through par.c's own implementation.
 * The calling thread works too (with NW=2 workers that is 3 threads, the 3 cores the Vita gives an app). */
#include <psp2/kernel/threadmgr.h>

typedef void (*par_fn)(void *ctx, int lo, int hi);

volatile int vita_par = 0;

void __real_lime_par_for(int n, par_fn fn, void *ctx);
int  __real_lime_par_threads(void);

#define NW 2
static SceUID g_go, g_ack;
static par_fn g_fn;
static void *g_ctx;
static int g_n, g_chunk, g_chunks;
static volatile int g_next;
static int g_nw = -1;                 /* -1 = not started */

static void run_chunks(void) {
  for (;;) {
    int c = __sync_fetch_and_add(&g_next, 1);
    if (c >= g_chunks) return;
    int lo = c * g_chunk;
    int hi = lo + g_chunk < g_n ? lo + g_chunk : g_n;
    g_fn(g_ctx, lo, hi);
  }
}

static int worker(SceSize args, void *argp) {
  (void)args; (void)argp;
  for (;;) {
    sceKernelWaitSema(g_go, 1, 0);
    run_chunks();
    sceKernelSignalSema(g_ack, 1);    /* each worker signals when done: nobody is left "halfway" when the job ends */
  }
  return 0;
}

static void start(void) {
  g_nw = 0;
  g_go = sceKernelCreateSema("umk3_par_go", 0, 0, 64, 0);
  g_ack = sceKernelCreateSema("umk3_par_ack", 0, 0, 64, 0);
  if (g_go < 0 || g_ack < 0) return;
  int prio = sceKernelGetThreadCurrentPriority();
  for (int i = 0; i < NW; i++) {
    SceUID t = sceKernelCreateThread("umk3_par", worker, prio, 0x40000, 0, 0, 0);
    if (t < 0) break;
    sceKernelStartThread(t, 0, 0);
    g_nw++;
  }
}

int __wrap_lime_par_threads(void) {
  if (!vita_par) return __real_lime_par_threads();
  if (g_nw < 0) start();
  return g_nw + 1;
}

void __wrap_lime_par_for(int n, par_fn fn, void *ctx) {
  if (!vita_par) { __real_lime_par_for(n, fn, ctx); return; }
  if (n <= 0) return;
  if (g_nw < 0) start();
  if (g_nw == 0 || n < 2) { fn(ctx, 0, n); return; }
  int chunks = (g_nw + 1) * 4;                       /* four pieces per thread: a slow one does not hold the others back */
  if (chunks > n) chunks = n;
  g_fn = fn; g_ctx = ctx; g_n = n;
  g_chunk = (n + chunks - 1) / chunks;
  g_chunks = (n + g_chunk - 1) / g_chunk;
  g_next = 0;
  __sync_synchronize();
  for (int i = 0; i < g_nw; i++) sceKernelSignalSema(g_go, 1);
  run_chunks();                                      /* the caller works too */
  for (int i = 0; i < g_nw; i++) sceKernelWaitSema(g_ack, 1, 0);
  __sync_synchronize();
}
