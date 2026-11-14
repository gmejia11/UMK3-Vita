/* vita_zmalloc.c -- malloc() returns ZEROED memory (as a fresh process does on the PC).
 * Hypothesis: the decompiled code reads past the end of some buffer expecting zeros;
 * on the Vita reused memory carries leftovers (texture pixels, for instance). Hooked with
 * -Wl,--wrap=malloc (vita.cmake); enabled with zmalloc=1 in ux0:data/umk3/vita.cfg (off by default). */
#include <stddef.h>
#include <string.h>

volatile int vita_zmalloc = 0;

void *__real_malloc(size_t n);

void *__wrap_malloc(size_t n) {
  void *p = __real_malloc(n);
  if (p && vita_zmalloc) memset(p, 0, n);
  return p;
}
