/* umk3_sgn.h -- Vita port. The Vita's addresses start at 0x81000000 and, read as int32_t, they are NEGATIVE.
 * The original code ran with addresses below 2 GB and compared a field WITH SIGN against a small constant to
 * tell a "number" from a "pointer" (e.g. `next > 0x100`); with the Vita's addresses that comes out backwards.
 * UMK3_SGN(x) treats values in the application's memory range as huge positives (as they were in the original)
 * and leaves all others alone, real negatives included. Outside the Vita it is exactly `(int32_t)(x)`. */
#ifndef UMK3_SGN_H
#define UMK3_SGN_H
#include <stdint.h>
#ifdef UMK3_VITA
extern volatile int vita_sgn;          /* vita.cfg sgn=0 turns the fix off (to compare with the old behaviour) */
static inline int32_t umk3_sgn_f(uint32_t v) { return (vita_sgn && v >= 0x81000000u && v < 0x90000000u) ? 0x7fffffff : (int32_t)v; }
#define UMK3_SGN(x) umk3_sgn_f((uint32_t)(x))
#else
#define UMK3_SGN(x) ((int32_t)(x))
#endif
#endif
