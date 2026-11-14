/* vita_watch.c -- DIAGNOSTICS (diag=1). Watches the animation cursor (field40, offset 0x40) of the 30 Plyr
 * objects (umk3_common_Plyr: 30 x 108 bytes) at the end of every frame. If a value appears that looks like a
 * pointer but falls outside the application's memory, it writes ux0:data/umk3/corrupt.txt with the object's
 * state now and in the previous frame, the field's history, the 32 bytes just before Plyr (the end of
 * umk3_common_Pp) and the last file opened. It changes nothing in the game. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>

extern uint32_t umk3_common_Plyr[];      /* 30 x 108 bytes */
extern uint32_t umk3_common_Pp[];        /* 4200 bytes, just before Plyr */
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
  fprintf(f, "=== frame %u (swaps=%u) object Plyr[%d] field40=0x%08x last_file=%s\n",
          g_frames, vita_swaps, i, (unsigned)umk3_common_Plyr[i * OW + F40], vita_last_open);
  fprintf(f, "now     :");
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)umk3_common_Plyr[i * OW + k]);
  fprintf(f, "\nbefore  :");
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)g_prev[i][k]);
  fprintf(f, "\nfield40 history (old -> new):");
  for (unsigned h = 0; h < HIST; h++) fprintf(f, " %08x", (unsigned)g_hist[i][(g_hpos + 1 + h) % HIST]);
  fprintf(f, "\nend of Pp (8 words before Plyr):");
  const uint32_t *tail = umk3_common_Plyr - 8;
  for (int k = 0; k < 8; k++) fprintf(f, " %08x", (unsigned)tail[k]);
  fprintf(f, "\naddress of Plyr=%p  of Pp=%p\n\n", (void *)umk3_common_Plyr, (void *)umk3_common_Pp);
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
      g_reported[i] = 0;                 /* sane again: it may be reported again */
    }
  }
  g_hpos++;
  for (int i = 0; i < NOBJ; i++) memcpy(g_prev[i], &umk3_common_Plyr[i * OW], sizeof g_prev[i]);
}

/* Called from ani_fetch (other.c): why=1 absurd character (0 is returned), why=2 absurd animation (the row's
 * first is returned), why=0 notice only (animation >= 38). 60 lines at most. */
void vita_ani_bad(unsigned c, unsigned i, unsigned why) {
  static unsigned n;
  if (n >= 60) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/ani_bad.txt", "a");
  if (!f) return;
  fprintf(f, "frame %u swaps=%u ani_fetch c=%u i=0x%x why=%u last_file=%s\n",
          g_frames, vita_swaps, c, i, why, vita_last_open);
  fclose(f);
}

/* ---- animation cursor check (used by other.c with UMK3_VITA) ---- */
#ifndef VITA_PTR_LO
#define VITA_PTR_LO 0x81000000u        /* the application's memory on the Vita: text, data and heap */
#define VITA_PTR_HI 0x90000000u
#endif
int vita_ptr_ok(uint32_t v) { return v >= VITA_PTR_LO && v < VITA_PTR_HI && (v & 3u) == 0; }

/* where: 1 next_anirate, 2 entry of do_next_a9_frame_pxob, 3 invalid frame, 4 invalid jump.
 * Notes it in ux0:data/umk3/cursor_bad.txt (40 at most) and the game goes on: the script is treated as finished. */
void vita_bad_cursor(const void *obj, int where, uint32_t v) {
  static unsigned n;
  if (n >= 40) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/cursor_bad.txt", "a");
  if (!f) return;
  long idx = -1;
  if ((const uint32_t *)obj >= umk3_common_Plyr && (const uint32_t *)obj < umk3_common_Plyr + NOBJ * OW)
    idx = (long)(((const uint32_t *)obj - umk3_common_Plyr) / OW);
  fprintf(f, "frame %u swaps=%u where=%d value=0x%08x obj=%p Plyr[%ld] last_file=%s\n  obj:",
          g_frames, vita_swaps, where, (unsigned)v, obj, idx, vita_last_open);
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)((const uint32_t *)obj)[k]);
  fprintf(f, "\n");
  fclose(f);
}

/* ---- who calls get_char_ani* with a POINTER instead of an index (used by other.c) ---- */
int vita_ani_keep;   /* vita.cfg anikeep=1: with a pointer as the index, leave the cursor as it is instead of going back to animation 0 */

/* which: 1 get_char_ani  2 get_char_ani2  3 get_his_char_ani  4 get_his_char_ani2.  Writes ani_site.txt (60 at most). */
extern void *port_mk3_running;           /* mk3_update: the thread handler running now... */
extern unsigned port_mk3_running_token;  /* ...and its token (the state it resumed in) */
void vita_ani_site(const void *obj, int which, void *lr) {
  static unsigned n;
  if (n >= 60) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/ani_site.txt", "a");
  if (!f) return;
  const uint32_t *o = (const uint32_t *)obj;
  unsigned c = 0xffffffffu;
  if (which <= 2 && vita_ptr_ok(o[2])) c = ((const uint32_t *)(uintptr_t)o[2])[9];
  fprintf(f, "frame %u which=%d lr=0x%08x anchor(vita_ani_site)=0x%08x field40=0x%08x c=%u last_file=%s handler=0x%08x token=0x%x\n  obj:",
          g_frames, which, (unsigned)(uintptr_t)lr, (unsigned)(uintptr_t)&vita_ani_site, (unsigned)o[16], c, vita_last_open,
          (unsigned)(uintptr_t)port_mk3_running, (unsigned)port_mk3_running_token);
  for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)o[k]);
  fprintf(f, "\n");
  fclose(f);
}

/* ---- dumps the threads (threads.txt) on L+R+Select, or by itself if player one's animation cursor does not change in 150 frames ---- */
extern void vita_dump_threads(unsigned frame, const char *why);   /* in mk3.c */
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
    vita_dump_threads(g_frames, manual ? "manual (L+R+Select)" : "auto: player one's cursor unchanged for 150 frames");
  }
}

/* ---- a thread sleep shaped like a pointer (used by mk3_update in mk3.c) ---- */
/* Notes in ux0:data/umk3/sleep_bad.txt (40 at most): the thread routine that just ran, its token, the value and the object. */
void vita_bad_sleep(void *handler, unsigned token, uint32_t sleep, uint32_t proc) {
  static unsigned n;
  if (n >= 40) return;
  n++;
  FILE *f = fopen("ux0:data/umk3/sleep_bad.txt", "a");
  if (!f) return;
  fprintf(f, "frame %u handler=0x%08x anchor(vita_bad_sleep)=0x%08x token=0x%x sleep=0x%08x proc=0x%08x last_file=%s\n  obj:",
          g_frames, (unsigned)(uintptr_t)handler, (unsigned)(uintptr_t)&vita_bad_sleep, token, (unsigned)sleep, (unsigned)proc, vita_last_open);
  if (vita_ptr_ok(proc)) for (int k = 0; k < OW; k++) fprintf(f, " %08x", (unsigned)((const uint32_t *)(uintptr_t)proc)[k]);
  fprintf(f, "\n");
  fclose(f);
}

/* vita.cfg sgn=0/1: 1 (default) treats the Vita's addresses as positive in the logic's signed comparisons */
volatile int vita_sgn = 1;

/* vita.cfg facemax=N: most drops/puddles/spear pieces drawn per frame (0 = no limit) */
volatile int vita_face_max = 200;

/* vita.cfg skipdraw=0: draw every logic tick as before (1 = only the last of each pass) */
volatile int vita_skipdraw = 1;

/* vita.cfg chain=1: removes the punch-chain limit (0 = as the arcade) */
volatile int vita_chain = 0;
