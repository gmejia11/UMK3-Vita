/* vita_tables.c -- loads the fight engine's tables from ux0:data/umk3/umk3_tables.bin.
 *
 * The VPK carries no game data. The build compiles the tables extracted from the game's binary as empty
 * stubs (vita/tables_split.py): the same names and sizes, zero-filled. Their values -- and their address
 * words, which point at our own code and tables -- are in umk3_tables.bin, which every player makes from
 * their own .ipa (vita/make_data.py). This fills the tables at start-up, before main().
 *
 * The file and the VPK carry a hash of the layout (table names and sizes, symbol lists); a file that does
 * not match is refused. If the tables cannot be loaded, plat_open (vita_plat.c) shows a message and quits:
 * the game cannot run without them.
 *
 * Format (little endian):
 *   "UMK3TBL1"  u32 layout_hash  u32 nfiles
 *   per file:   u32 taglen  tag  u32 nobjs
 *     per table:  u32 size  u32 nrels  bytes[size]  (padding to 4)  { u32 offset, u32 symbol, i32 addend } x nrels
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

struct umk3_tbl_obj { void *p; unsigned size; };

/* written into each stub by vita/tables_split.py */
#define TBL_DECL(tag) \
  extern const struct umk3_tbl_obj umk3_tbl_##tag##_objs[]; extern const unsigned umk3_tbl_##tag##_nobjs; \
  extern void *const umk3_tbl_##tag##_syms[]; extern const unsigned umk3_tbl_##tag##_nsyms; \
  extern const unsigned umk3_tbl_layout_hash_##tag;
TBL_DECL(logic)
TBL_DECL(level)
TBL_DECL(seq)

static const struct {
  const char *tag;
  const struct umk3_tbl_obj *objs; const unsigned *nobjs;
  void *const *syms; const unsigned *nsyms;
} g_files[] = {
  { "logic", umk3_tbl_logic_objs, &umk3_tbl_logic_nobjs, umk3_tbl_logic_syms, &umk3_tbl_logic_nsyms },
  { "level", umk3_tbl_level_objs, &umk3_tbl_level_nobjs, umk3_tbl_level_syms, &umk3_tbl_level_nsyms },
  { "seq",   umk3_tbl_seq_objs,   &umk3_tbl_seq_nobjs,   umk3_tbl_seq_syms,   &umk3_tbl_seq_nsyms },
};
#define NFILES (sizeof g_files / sizeof g_files[0])

#define TABLES_PATH "ux0:data/umk3/umk3_tables.bin"

int vita_tables_ok;                 /* 1 once every table is filled */
char vita_tables_err[256];          /* why not, for the message in plat_open */

static uint32_t rd32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static int fail(const char *why) {
  snprintf(vita_tables_err, sizeof vita_tables_err, "%s", why);
  return 0;
}

static int load(const unsigned char *b, size_t n) {
  size_t at = 0;
#define NEED(k) do { if (n - at < (size_t)(k)) return fail("umk3_tables.bin is truncated or damaged."); } while (0)
  NEED(16);
  if (memcmp(b, "UMK3TBL1", 8) != 0) return fail("umk3_tables.bin is not a tables file.");
  if (rd32(b + 8) != umk3_tbl_layout_hash_logic)
    return fail("umk3_tables.bin was made for a different build of the game. Make it again with "
                "vita/make_data.py from the same version as this VPK.");
  if (rd32(b + 12) != NFILES) return fail("umk3_tables.bin does not match this VPK.");
  at = 16;
  for (unsigned f = 0; f < NFILES; f++) {
    NEED(4);
    uint32_t tl = rd32(b + at); at += 4;
    NEED(tl);
    if (tl != strlen(g_files[f].tag) || memcmp(b + at, g_files[f].tag, tl) != 0)
      return fail("umk3_tables.bin does not match this VPK.");
    at += tl;
    NEED(4);
    if (rd32(b + at) != *g_files[f].nobjs) return fail("umk3_tables.bin does not match this VPK.");
    at += 4;
    for (unsigned o = 0; o < *g_files[f].nobjs; o++) {
      const struct umk3_tbl_obj *t = &g_files[f].objs[o];
      NEED(8);
      uint32_t size = rd32(b + at), nrel = rd32(b + at + 4);
      at += 8;
      if (size != t->size) return fail("umk3_tables.bin does not match this VPK.");
      NEED(size);
      memcpy(t->p, b + at, size);
      at += (size + 3u) & ~3u;
      if (at > n) at = n;
      NEED((size_t)nrel * 12u);
      for (uint32_t r = 0; r < nrel; r++, at += 12) {
        uint32_t off = rd32(b + at), sym = rd32(b + at + 4);
        int32_t add = (int32_t)rd32(b + at + 8);
        if (off > size - 4u || size < 4u || sym >= *g_files[f].nsyms)
          return fail("umk3_tables.bin is damaged (a relocation is out of range).");
        uint32_t v = (uint32_t)(uintptr_t)g_files[f].syms[sym] + (uint32_t)add;
        memcpy((char *)t->p + off, &v, 4);
      }
    }
  }
#undef NEED
  return 1;
}

__attribute__((constructor)) static void vita_tables_start(void) {
  SceIoStat st;
  if (sceIoGetstat(TABLES_PATH, &st) < 0) {
    fail("ux0:data/umk3/umk3_tables.bin is missing.\n\nMake it from your own copy of the game with "
         "vita/make_data.py (see the README) and copy it to ux0:data/umk3/ with umk3.pak.");
    return;
  }
  size_t n = (size_t)st.st_size;
  unsigned char *b = (unsigned char *)malloc(n ? n : 1);
  if (!b) { fail("Not enough memory to read umk3_tables.bin."); return; }
  SceUID fd = sceIoOpen(TABLES_PATH, SCE_O_RDONLY, 0);
  size_t got = 0;
  if (fd >= 0) {
    while (got < n) {
      int r = sceIoRead(fd, b + got, (SceSize)(n - got));
      if (r <= 0) break;
      got += (size_t)r;
    }
    sceIoClose(fd);
  }
  if (got != n) fail("ux0:data/umk3/umk3_tables.bin could not be read.");
  else vita_tables_ok = load(b, n);
  free(b);
}
