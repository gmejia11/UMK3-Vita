#!/usr/bin/env bash
# Usage: ./build.sh <UMK3.ipa | path/to/armv7_binary>
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(dirname "$HERE")"
[ -z "$VITASDK" ] && for d in "$HOME/vitasdk" /usr/local/vitasdk; do [ -d "$d" ] && VITASDK="$d" && break; done
[ -d "$VITASDK" ] || { echo "VITASDK not found"; exit 1; }
export VITASDK PATH="$VITASDK/bin:$PATH"
IN="${1:?usage: $0 <UMK3.ipa | binary>}"; [ -f "$IN" ] || { echo "Not found: $IN"; exit 1; }
case "$IN" in *.ipa) SRC="-DUMK3_IPA=$IN -DUMK3_ASSET_DEST=$HERE/data" ;; *) SRC="-DUMK3_BINARY=$IN" ;; esac
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"
B="$ROOT/build-vita"
cmake -S "$ROOT" -B "$B" -G "$GEN" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DUMK3_BACKEND=vita \
  -DPython3_EXECUTABLE="$(command -v python3)" $SRC
cmake --build "$B" --target umk3-game -j"$(nproc)"
# icon and LiveArea from YOUR .ipa (the repository has no game images)
case "$IN" in *.ipa) python3 "$HERE/make_livearea.py" "$IN" ;;
  *) [ -f "$HERE/sce_sys/icon0.png" ] || python3 "$HERE/make_livearea.py" ;; esac
cmake --build "$B" --target umk3.vpk
[ -d "$HERE/data/res" ] && python3 "$HERE/make_pak.py" "$HERE/data"
echo "VPK: $B/umk3.vpk"
