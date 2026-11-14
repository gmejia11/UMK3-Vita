/* umk3_sgn.h -- Port Vita. Las direcciones de la Vita empiezan en 0x81000000 y, leidas como int32_t, son NEGATIVAS.
 * El codigo original corria con direcciones < 2 GB y comparaba CON SIGNO un campo contra una constante pequena para
 * distinguir "numero" de "puntero" (p. ej. `siguiente > 0x100`); con las direcciones de la Vita eso sale al reves.
 * UMK3_SGN(x) trata los valores del rango de memoria de la aplicacion como positivos enormes (como eran en el original)
 * y deja intactos todos los demas, negativos reales incluidos. Fuera de la Vita es exactamente `(int32_t)(x)`. */
#ifndef UMK3_SGN_H
#define UMK3_SGN_H
#include <stdint.h>
#ifdef UMK3_VITA
extern volatile int vita_sgn;          /* vita.cfg sgn=0 apaga el arreglo (para comparar con el comportamiento antiguo) */
static inline int32_t umk3_sgn_f(uint32_t v) { return (vita_sgn && v >= 0x81000000u && v < 0x90000000u) ? 0x7fffffff : (int32_t)v; }
#define UMK3_SGN(x) umk3_sgn_f((uint32_t)(x))
#else
#define UMK3_SGN(x) ((int32_t)(x))
#endif
#endif
