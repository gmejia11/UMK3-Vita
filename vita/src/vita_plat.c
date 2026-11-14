/* vita_plat.c -- implements platform.h for the PS Vita.
 *   window/GL: vitaGL     input: sceCtrl + sceTouch     time: sceKernel
 * Everything is WEAK: if ui_alert.c / sdl_audio.c already define something, their version wins with no
 * "multiple definition" error; whatever is missing is covered here (harmless stubs). */
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
#include <psp2/message_dialog.h>
#include "platform.h"

#define WEAK __attribute__((weak))
#define SCR_W 960
#define SCR_H 544

int _newlib_heap_size_user = 96 * 1024 * 1024;   /* user RAM for malloc */

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

/* Mapping: Square=HP Triangle=LP R=Block Cross=HK Circle=LK L=Run */
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

/* ------------------------------------------------------------ window / GL */
/* vitaGL memory sizes, editable WITHOUT recompiling: ux0:data/umk3/vita.cfg
   ("key=value" lines; values in MB).  Keys: circular_pool_mb (default 12; 0 = vitaGL's own, 32),
   legacy_pool_mb (default 4), ram_threshold_mb (default 8). */
unsigned vita_cfg_cp, vita_cfg_lp, vita_cfg_th;
int vita_rear_swap;             /* vita.cfg rearswap=1: if the rear pad's sides come out swapped */
int vita_rear_on = 1;           /* vita.cfg rear=: 1 the whole top strip is the special button, 2 left corner run + right corner
                                   special (the old layout), 0 off */
int vita_tex16;                /* 0 = RGBA8888 (default); 1 = opaque ones to RGB565; 2 = also those with alpha to RGBA4444 */
/* vita.cfg is read into memory ONCE: it used to be opened once per key (~20 opens on the card at start-up). */
static char g_cfg[4096];
static int g_cfg_state;                 /* 0 = not read, 1 = read, -1 = does not exist */
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

/* The game cannot run without its tables (vita_tables.c): show why on the Vita's own message dialog, then quit. */
static void tables_missing(void) {
  extern char vita_tables_err[];
  static char msg[512];
  SceMsgDialogUserMessageParam um;
  SceMsgDialogParam p;
  snprintf(msg, sizeof msg, "Ultimate Mortal Kombat 3 cannot start.\n\n%s", vita_tables_err);
  memset(&um, 0, sizeof um);
  um.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
  um.msg = (const SceChar8 *)msg;
  sceMsgDialogParamInit(&p);
  p.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
  p.userMsgParam = &um;
  if (sceMsgDialogInit(&p) >= 0) {
    while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      vglSwapBuffers(GL_TRUE);
    }
    sceMsgDialogTerm();
  }
  sceKernelExitProcess(0);
}

WEAK bool plat_open(const char *title, int width, int height) {
  (void)title; (void)width; (void)height;
  sceIoMkdir("ux0:data/umk3", 0777);
  sceIoMkdir("ux0:data/umk3/saves", 0777);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  /* Vita port: clocks at the most an app gets (CPU 444, bus 222, GPU 222, xbar 166 MHz). By default an app runs
   * at CPU 333 / GPU 111: loading a fight (texture decompression, assembling the fighters) is almost all CPU.
   * vita.cfg clock=0 keeps the system's clocks. */
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
  /* facebatch=1 (default): blood drops are drawn batched by texture (draw_gl.c), so there is no need to skip drops any
   * more: facemax becomes 0 (all of them). facebatch=0 goes back to one draw call per drop, with the old limit of 200. */
  { extern volatile int vita_face_batch; vita_face_batch = (int)cfg_mb("facebatch", 1); }
  { extern volatile int vita_face_max, vita_face_batch; vita_face_max = (int)cfg_mb("facemax", vita_face_batch ? 0 : 200); }
  /* kombo_slide=0: kombos no longer push the fighters apart (issue #114; offline). Only when the key is in vita.cfg, so as not
   * to override a kombo_slide= from umk3.ini, which is read earlier. */
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
  /* buttons=5 or 6: the layout, over the one saved from OPTIONS > CONTROLS (as umk3.ini buttons= does). Without the key, the
   * game's own setting is used, and the CONTROLS screen switches it. */
  { extern void port_buttons_set(int n); unsigned b = cfg_mb("buttons", 0); if (b == 5u || b == 6u) port_buttons_set((int)b); }
  { extern volatile int vita_prof; vita_prof = (int)cfg_mb("prof", 0); }
  { extern volatile int vita_par; vita_par = (int)cfg_mb("par", 0); }
  { extern int vita_ani_keep; vita_ani_keep = (int)cfg_mb("anikeep", 0); }
  { extern volatile int vita_zmalloc; vita_zmalloc = (int)cfg_mb("zmalloc", 0); }
  if (vita_cfg_cp) vglSetCircularPoolSize(vita_cfg_cp * 1024u * 1024u);   /* before vglInit */
  vglInitExtended(vita_cfg_lp * 1024u * 1024u, SCR_W, SCR_H, vita_cfg_th * 1024u * 1024u, SCE_GXM_MULTISAMPLE_NONE);
  { extern volatile int vita_gl_ready; vita_gl_ready = 1; }   /* the log thread may now query vitaGL */
  { extern int vita_tables_ok; if (!vita_tables_ok) tables_missing(); }
  g_t0 = sceKernelGetProcessTimeWide();
  g_open = 1;
  read_pad();
  return true;
}
WEAK void plat_close(void) { if (g_open) { g_open = 0; } }
WEAK void plat_quit(void) { g_quit = 1; }
/* Port enhancements (not in the original game) that are too heavy on the Vita.
   "weak" symbol: if the name does not exist in your version, this simply does nothing. */
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
WEAK void *plat_gl_proc(const char *name) { (void)name; return 0; }  /* no FBO for now */

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

/* ------------------------------------------------------------ text / mouse */
WEAK int  plat_text_take(char *out, int n) { (void)out; (void)n; return 0; }
WEAK void plat_text_clear(void) {}
WEAK int  plat_clipboard_text(char *out, int n) { (void)out; (void)n; return 0; }
WEAK int  plat_clipboard_set(const char *utf8) { (void)utf8; return 0; }
WEAK void plat_language(char *out, int n) { snprintf(out, n, "en"); }

WEAK int plat_mouse(int *x, int *y) {           /* front touch screen = pointer */
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
  (void)msg; (void)ok; (void)cancel; return 0;   /* always accepts */
}

/* ------------------------------------------------------------ keys / pads */
/* REAR touch pad. Only its TOP strip counts (the fingers rest lower down while holding the console).
 * rear=1 (default): a touch anywhere on the top strip = R2, the special button (S: specials, the "powers").
 * rear=2: the old layout -- left corner = L2 (run), right corner = R2 (special). The rear pad is mirrored relative to the
 * screen; if the corners come out swapped for you, rearswap=1 in vita.cfg. */
static int g_rear_l2, g_rear_r2;          /* read ONCE per frame in read_pad (it used to be read up to 4 times) */
static void rear_poll(void) {
  SceTouchData t;
  int *l2 = &g_rear_l2, *r2 = &g_rear_r2;
  *l2 = *r2 = 0;
  if (!vita_rear_on || !g_open) return;
  sceTouchPeek(SCE_TOUCH_PORT_BACK, &t, 1);
  for (unsigned i = 0; i < t.reportNum && i < 8; i++) {
    int x = t.report[i].x, y = t.report[i].y;
    if (y > 380) continue;                                   /* top strip (the pad is ~890 high) */
    if (vita_rear_on != 2) { *r2 = 1; continue; }            /* rear=1: the whole strip is the special button */
    int mine_right = x < 640, mine_left = x > 1279;          /* mirrored: small x = the player's right side */
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
    /* PC layout (umk3.ini: pad_hp=WEST pad_lp=SOUTH pad_hk=NORTH pad_lk=EAST pad_block=R1 pad_run=L1+L2) */
    case PK_HP:    return B(SCE_CTRL_SQUARE);                          /* square    = high punch */
    case PK_LP:    return B(SCE_CTRL_CROSS);                           /* cross     = low punch  */
    case PK_BL:    return B(SCE_CTRL_RTRIGGER);                        /* R         = block      */
    case PK_HK:    return B(SCE_CTRL_TRIANGLE);                        /* triangle  = high kick  */
    case PK_LK:    return B(SCE_CTRL_CIRCLE);                          /* circle    = low kick   */
    case PK_RUN:   return B(SCE_CTRL_LTRIGGER) || rear_l2();                        /* L         = run        */
    /* 5-button mode (pad5_p=WEST+NORTH pad5_k=SOUTH+EAST pad5_b=R1 pad5_r=L1+L2): these were missing and combos did not work */
    case PK_5_P:   return B(SCE_CTRL_SQUARE) || B(SCE_CTRL_TRIANGLE);  /* punch = square or triangle */
    case PK_5_B:   return B(SCE_CTRL_RTRIGGER);                        /* block = R */
    case PK_5_K:   return B(SCE_CTRL_CROSS) || B(SCE_CTRL_CIRCLE);     /* kick  = cross or circle */
    case PK_5_R:   return B(SCE_CTRL_LTRIGGER) || rear_l2();                        /* run   = L */
    case PK_SPECIAL: return rear_r2();                                  /* R2 = rear pad, right */
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
    case PK_SPECIAL: s = "REAR TOUCH"; break;
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
  o->kind = 4;                                   /* PlayStation-style icons */
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

/* ------------------------------------------------------------ UI drawn by the backend:
   NULL = "use your own font" (see platform.h). If ui_alert.c defines them, they win. */
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

/* ------------------------------------------------------------ audio: fallback stubs.
   Normally sdl_audio.c (SDL2 from vdpm) defines all of this and wins. */
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
