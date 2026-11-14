/* Replaces render/rgl_front.c on the Vita (rgl_front.c DEFINES the glX functions, which would clash
   with vitaGL's). Only the "rgl_*" part is left here and it is always OpenGL: the game's gl*
   calls go straight to vitaGL. */
#include "rgl.h"

int rgl_renderer = RGL_OPENGL;
int rgl_parse(const char *name) { (void)name; return RGL_OPENGL; }
const char *rgl_label(int renderer) { (void)renderer; return "OpenGL (vitaGL)"; }

int  rgl_open(void *window, int width, int height) { (void)window; (void)width; (void)height; rgl_renderer = RGL_OPENGL; return 0; }
void rgl_present(void) {}
void rgl_close(void) {}
void *rgl_proc(const char *name) { (void)name; return 0; }

const char *const *(*rgl_vk_instance_exts)(unsigned *count) = 0;
int (*rgl_vk_make_surface)(void *window, void *instance, void *surface) = 0;

/* rgl_ff.c is not built on the Vita; in case anyone calls these */
__attribute__((weak)) void rgl_ff_start(const rgl_backend *b, int width, int height) { (void)b; (void)width; (void)height; }
__attribute__((weak)) void rgl_ff_stop(void) {}
__attribute__((weak)) void rgl_ff_frame(void) {}
