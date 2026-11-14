#!/usr/bin/env bash
# En la Vita: VitaShell -> pulsa SELECT para activar FTP.
# Uso: ./deploy_data.sh IP_VITA           sube SOLO umk3.pak (no toca partidas, musica ni vita.cfg de la consola)
#      ./deploy_data.sh IP_VITA --todo    instalacion completa: umk3.pak, todos los archivos de data/ y vita.cfg
#                                         (OJO: sobrescribe las partidas guardadas de res/ con las de fabrica)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
IP="${1:?uso: $0 IP_VITA [--todo]}"; MODE="${2:-}"
cd "$HERE/data"
up() { echo "$1"; curl -s --ftp-create-dirs -T "$1" "ftp://$IP:1337/ux0:/data/umk3/${1#./}"; }
[ -f umk3.pak ] || python3 "$HERE/make_pak.py" "$HERE/data"
up ./umk3.pak
if [ "$MODE" = "--todo" ]; then
  find . -type f ! -name umk3.pak ! -name '*.part' | while read -r f; do up "$f"; done
  echo "vita.cfg"
  curl -s --ftp-create-dirs -T "$HERE/vita.cfg" "ftp://$IP:1337/ux0:/data/umk3/vita.cfg"
fi
echo "Listo."
