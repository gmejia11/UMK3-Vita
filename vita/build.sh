#!/usr/bin/env bash
# Usage: ./build.sh <UMK3.ipa | path/to/armv7_binary> [--public]
#   --public: generic icon and LiveArea (no game artwork), for a VPK to share
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(dirname "$HERE")"
[ -z "$VITASDK" ] && for d in "$HOME/vitasdk" /usr/local/vitasdk; do [ -d "$d" ] && VITASDK="$d" && break; done
[ -d "$VITASDK" ] || { echo "VITASDK not found"; exit 1; }
export VITASDK PATH="$VITASDK/bin:$PATH"
IN="${1:?usage: $0 <UMK3.ipa | binary> [--public]}"; [ -f "$IN" ] || { echo "Not found: $IN"; exit 1; }
PUBLIC=0; [ "${2:-}" = "--public" ] && PUBLIC=1
# With a .ipa: its binary to vita/work/ and the game's files to vita/data/ (extract_ipa.py), then build from the binary
case "$IN" in *.ipa) python3 "$HERE/extract_ipa.py" "$IN"; BIN="$HERE/work/UMK3.armv7" ;; *) BIN="$IN" ;; esac
SRC="-DUMK3_BINARY=$BIN"
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"
B="$ROOT/build-vita"
cmake -S "$ROOT" -B "$B" -G "$GEN" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DUMK3_BACKEND=vita \
  -DPython3_EXECUTABLE="$(command -v python3)" $SRC
cmake --build "$B" --target umk3-game -j"$(nproc)"
# icon and LiveArea: generic for a VPK to share, otherwise from YOUR .ipa (the repository has no game images)
if [ "$PUBLIC" = 1 ]; then python3 "$HERE/make_livearea.py" --generic
else case "$IN" in *.ipa) python3 "$HERE/make_livearea.py" "$IN" ;;
  *) [ -f "$HERE/sce_sys/icon0.png" ] || python3 "$HERE/make_livearea.py" --generic ;; esac
fi
cmake --build "$B" --target umk3.vpk
# the tables' values go to the Vita with the game's files (the VPK has none of the game's data)
mkdir -p "$HERE/data" && cp "$B/umk3_tables.bin" "$HERE/data/umk3_tables.bin"
[ -d "$HERE/data/res" ] && python3 "$HERE/make_pak.py" "$HERE/data"
echo "VPK: $B/umk3.vpk"
