/* Reemplaza render/rgl_front.c en Vita (rgl_front.c DEFINE las funciones glX, que chocarian
   con las de vitaGL). Aqui solo queda la parte "rgl_*" y siempre es OpenGL: las llamadas gl*
   del juego van directo a vitaGL. */
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

/* rgl_ff.c no se compila en Vita; por si alguien llama a estas */
__attribute__((weak)) void rgl_ff_start(const rgl_backend *b, int width, int height) { (void)b; (void)width; (void)height; }
__attribute__((weak)) void rgl_ff_stop(void) {}
__attribute__((weak)) void rgl_ff_frame(void) {}
