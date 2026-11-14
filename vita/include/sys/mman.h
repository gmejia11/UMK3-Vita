/* Shim de sys/mman.h para PS Vita (newlib de vitasdk no lo trae).
   Solo cubre mmap ANONIMO (memoria nueva a cero), que es lo que hace falta aqui.
   MAP_FIXED / mmap de ficheros: devuelve MAP_FAILED para que el fallo sea visible. */
#ifndef VITA_SYS_MMAN_H
#define VITA_SYS_MMAN_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_32BIT     0          /* en 32-bit todo malloc ya es "bajo" */
#define MAP_NORESERVE 0
#define MAP_FAILED ((void *)-1)

static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
  (void)addr; (void)prot; (void)off;
  if ((flags & MAP_FIXED) || !(flags & MAP_ANONYMOUS) || fd != -1) return MAP_FAILED;
  void *p = memalign(4096, len);
  if (!p) return MAP_FAILED;
  memset(p, 0, len);
  return p;
}
static inline int munmap(void *addr, size_t len) { (void)len; free(addr); return 0; }
static inline int mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return 0; }
static inline int madvise(void *addr, size_t len, int adv) { (void)addr; (void)len; (void)adv; return 0; }
#endif
