#!/usr/bin/env bash
# On the Vita: VitaShell -> press SELECT to turn FTP on.
# Usage: ./deploy_data.sh VITA_IP          uploads ONLY umk3.pak and umk3_tables.bin (leaves saves, music and vita.cfg alone)
#        ./deploy_data.sh VITA_IP --all    full install: umk3.pak, every file in data/ and vita.cfg
#                                          (CAREFUL: overwrites the saves in res/ with the factory ones)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
IP="${1:?usage: $0 VITA_IP [--all]}"; MODE="${2:-}"
cd "$HERE/data"
up() { echo "$1"; curl -s --ftp-create-dirs -T "$1" "ftp://$IP:1337/ux0:/data/umk3/${1#./}"; }
[ -f umk3.pak ] || python3 "$HERE/make_pak.py" "$HERE/data"
up ./umk3.pak
[ -f umk3_tables.bin ] && up ./umk3_tables.bin
if [ "$MODE" = "--all" ] || [ "$MODE" = "--todo" ]; then
  find . -type f ! -name umk3.pak ! -name umk3_tables.bin ! -name '*.part' | while read -r f; do up "$f"; done
  echo "vita.cfg"
  curl -s --ftp-create-dirs -T "$HERE/vita.cfg" "ftp://$IP:1337/ux0:/data/umk3/vita.cfg"
fi
echo "Done."
