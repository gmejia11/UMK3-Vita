# PS Vita port — build and settings

The Vita build reuses the repository's own build (`umk3-game` plus the tables
extracted from your `.ipa`) and changes only the platform layer and the
packaging. There is no separate CMakeLists.

## Building

Requires [VitaSDK](https://vitasdk.org/) with vitaGL, SDL2 and SDL2_mixer
(`vdpm`), Python 3 and, optionally, Pillow (for the icon and LiveArea).

1. `sudo apt install ninja-build` (or let `build.sh` use Makefiles)
2. `python3 vita/apply_cmake_patch.py` — four insertions in `CMakeLists.txt`
   (already applied in this repository; it is idempotent)
3. `./vita/build.sh path/to/UMK3_1.2.59.ipa` → `build-vita/umk3.vpk`,
   `vita/data/` (the game's files) and `vita/data/umk3.pak`
4. Install the VPK with VitaShell, then copy the data to `ux0:data/umk3/`:
   `./vita/deploy_data.sh VITA_IP --all` over VitaShell's FTP (press SELECT in
   VitaShell). Later, `./vita/deploy_data.sh VITA_IP` uploads only `umk3.pak`
   and leaves your saves and `vita.cfg` alone.

The icon and LiveArea images are made from your `.ipa` by
`vita/make_livearea.py`; the repository carries no game artwork. The VPK you
build contains data from your copy of the game: do not share it.

Files: `vita.cmake` (the backend), `src/vita_plat.c` (all of `platform.h`,
weak symbols), `src/vita_rgl.c` (replaces `rgl_front.c`), `src/vita_fs.c`
(relative paths → `ux0:data/umk3/`, the asset index and `umk3.pak`),
`src/vita_log.c` / `vita_prof.c` (log and profiler).

## Settings: `ux0:data/umk3/vita.cfg`

One `key=value` per line; anything missing takes its default.

| Key | Default | What it does |
|---|---|---|
| `clock` | 1 | CPU 444, bus 222, GPU 222, xbar 166 MHz (an app starts at CPU 333). 0 keeps the system's clocks. |
| `pak` | 1 | Serve `res/` from `umk3.pak`. 0 reads loose files. |
| `facebatch` | 1 | Draw blood drops batched by texture (~15 draw calls instead of thousands). 0 = one call per drop. |
| `facemax` | 0 (200 with `facebatch=0`) | Most drops drawn per frame; 0 = all. |
| `skipdraw` | 1 | When the Vita falls behind, draw only the last of the logic ticks run in a frame. |
| `tex16` | 0 | 1 = opaque textures as RGB565; 2 = also those with alpha as RGBA4444 (half the memory). |
| `kombo_slide` | 1 | 0 = a kombo no longer pushes the opponent away (offline only; also read from `umk3.ini`). 1 is the original game. |
| `chain` | 0 | 1 = no punch-chain limit (99 instead of the arcade's 4–6). Changes gameplay. |
| `sndcache`, `sndcache_mb` | 1, 16 | Keep converted sound effects (up to N MB) instead of converting on every play. |
| `sndfast` | 1 | Fast effect conversion (linear interpolation). |
| `buttons` | — | `5` or `6`: the button layout, over the one saved from OPTIONS → CONTROLS. Without it, the game's own setting is used. |
| `rear` | 1 | Rear touch pad, top half only. 1 = anywhere there is the special button (S). 2 = top-left corner Run, top-right Special. 0 = off. |
| `rearswap` | 0 | With `rear=2`, swaps the two corners. |
| `par` | 0 | 1 = decode textures on 3 cores (`vita_par.c`). |
| `circular_pool_mb`, `legacy_pool_mb`, `ram_threshold_mb` | 12, 4, 8 | vitaGL memory pools. |
| `log` | 1 | Periodic line in `vita_log.txt` every 2 s. The `LOAD` lines are always written. |
| `prof` | 0 | Profiler: draw calls, timings, hitches, `drops:` line. |
| `diag` | 0 | Diagnostics: `missing.txt`, `corrupt.txt`, `ani_bad.txt`, `cursor_bad.txt`, `ani_site.txt`, `sleep_bad.txt`, thread dump with L+R+Select. Writing to the card causes hitches. |
| `quiet` | 0 | 1 = drop everything the game prints. |
| `sgn` | 1 | Treat Vita addresses as positive in the logic's signed comparisons (`umk3_sgn.h`). 0 only for comparison. |
| `anikeep`, `zmalloc` | 0, 0 | Experiments for animation-cursor and uninitialised-memory issues. |

## The log: `ux0:data/umk3/vita_log.txt`

Opened once and kept open; past 2 MB at start-up it is renamed to
`vita_log_old.txt`. It holds:

- `LOAD fight: N s opens=… from_pack=… in … ms` — one line per load.
- The periodic line: heap, vitaGL's free memory (`vram_free`, `ram_free`,
  `phy_free`), files opened. A crash preceded by those near 0 KB points to
  exhausted graphics pools; that alone does not prove a CPU heap leak.
- `game: frame=… separate us: / corner trap: / kombo push: / death scream:` —
  the game's own notices about the rules that push fighters apart mid-kombo.
- With `prof=1`: `ticks:`, `times:`, `hitch:`, `slow:`, `who:`, `audio:`,
  `drops: draw_calls=… seen=… drawn=…`.

## History

**Faster loads (2026-11-04).** A fight took over 1 min 10 s to load: every
open of a file in `res/` costs ~100 ms on the card (~1900 files in one
folder) and a fight opens hundreds. `make_pak.py` gathers the 5050 read-only
files (333 MB) into `umk3.pak`, read with one `sceIoPread` each; clocks at
the maximum; `vita.cfg` read once. Measured on a Vita: 15.9 s per fight.
Saves, `.mp3` and mods are still read loose; re-run `make_pak.py` after
changing anything in `data/res` (the pack wins over a loose file of the same
name).

**Batched blood, lighter log, kombos (2026-11-14).** Blood drops batched by
texture (`facebatch`), so every drop is drawn again; the log kept open;
diagnostics only with `diag=1`; the rear pad read once per frame; the game's
kombo notices in the log; from MaryNCRT's later releases, KOMBO SLIDE (0.2.0)
and kombos that share a start (0.1.17: holding UP takes the second kombo on
the same button — Mileena's HK, HK, ↑+LK, ↑+HK; also Kabal, Sonya, Stryker and
Kano). Tried on a Vita: the game plays better with the default
`kombo_slide=1`.

**Known: Classic Sub-Zero's arm kombo.** The iOS release's own frame lists
lack the ninjas' `RPCOMBO6`–`RPCOMBO10` for him; the port borrows the four
that exist in Reptile's (the same poses bone for bone) and holds the last pose
for `RPCOMBO10`, which no fighter has.

If something looks wrong or runs worse, try `facebatch=0` and open an issue
with `vita_log.txt` from a run with `prof=1`.
