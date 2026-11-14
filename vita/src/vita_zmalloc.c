/* vita_zmalloc.c -- malloc() devuelve memoria a CERO (como lo hace un proceso nuevo en el PC).
 * Hipotesis: el codigo decompilado lee mas alla del final de algun buffer esperando ceros;
 * en la Vita la memoria reutilizada trae restos (por ejemplo pixeles de texturas). Se activa con
 * -Wl,--wrap=malloc (vita.cmake). Se puede apagar con zmalloc=0 en ux0:data/umk3/vita.cfg. */
#include <stddef.h>
#include <string.h>

volatile int vita_zmalloc = 0;

void *__real_malloc(size_t n);

void *__wrap_malloc(size_t n) {
  void *p = __real_malloc(n);
  if (p && vita_zmalloc) memset(p, 0, n);
  return p;
}
