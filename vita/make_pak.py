#!/usr/bin/env python3
"""make_pak.py -- junta los archivos de solo lectura de vita/data/res en vita/data/umk3.pak.

Por que: en la Vita cada apertura de un archivo de res/ cuesta del orden de 100 ms (la carpeta tiene ~1900
entradas y la tarjeta recorre el directorio en cada apertura) y una pelea abre cientos. Con el paquete, el juego
(vita/src/vita_fs.c) lee el indice una vez, deja umk3.pak abierto y sirve cada archivo con un solo sceIoPread.

Uso:  python3 vita/make_pak.py [carpeta_data]      (por defecto vita/data)

Quedan FUERA (siguen sueltos en res/): las partidas y ajustes que el juego escribe (archivos sin extension,
.sqlite, .tmp) y la musica .mp3, que el decodificador abre por su cuenta.

Formato (little endian), el mismo que lee vita_fs.c:
    "UMK3PAK1"  u32 n  u32 names_bytes
    { u32 name_off, u32 data_off, u32 size } x n     ordenadas por nombre
    nombres: rutas relativas a ux0:data/umk3/, en minusculas, terminadas en NUL
    datos
"""
import os
import struct
import sys

SKIP_EXT = {"", ".mp3", ".sqlite", ".tmp"}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    data = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "data"))
    res = os.path.join(data, "res")
    if not os.path.isdir(res):
        sys.exit("No existe %s (ejecuta antes vita/build.sh con tu .ipa)" % res)

    files = {}
    skipped = 0
    for dirpath, _, names in os.walk(res):
        for n in names:
            full = os.path.join(dirpath, n)
            if os.path.splitext(n)[1].lower() in SKIP_EXT or n.endswith(":Zone.Identifier"):
                skipped += 1
                continue
            rel = os.path.relpath(full, data).replace(os.sep, "/")
            key = rel.lower()
            if key in files:
                print("aviso: %s y %s solo difieren en mayusculas; se queda el primero" % (files[key], rel))
                continue
            files[key] = full

    keys = sorted(files, key=lambda k: k.encode("utf-8"))   # el mismo orden que strcmp en la Vita
    names = bytearray()
    name_off = []
    for k in keys:
        name_off.append(len(names))
        names += k.encode("utf-8") + b"\0"

    head = 16 + 12 * len(keys) + len(names)
    data_start = (head + 63) & ~63
    out = os.path.join(data, "umk3.pak")
    tmp = out + ".part"
    entries = []
    with open(tmp, "wb") as f:
        f.write(b"\0" * data_start)
        for k in keys:
            with open(files[k], "rb") as src:
                blob = src.read()
            off = f.tell()
            if off + len(blob) >= 1 << 32:
                sys.exit("El paquete pasaria de 4 GB")
            f.write(blob)
            entries.append((off, len(blob)))
            pad = (-f.tell()) & 15
            if pad:
                f.write(b"\0" * pad)
        f.seek(0)
        f.write(b"UMK3PAK1" + struct.pack("<II", len(keys), len(names)))
        for no, (off, size) in zip(name_off, entries):
            f.write(struct.pack("<III", no, off, size))
        f.write(names)
    os.replace(tmp, out)
    total = sum(s for _, s in entries)
    print("%s: %d archivos, %.1f MB (%d quedan sueltos)" % (out, len(keys), total / 1e6, skipped))


if __name__ == "__main__":
    main()
