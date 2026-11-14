#!/usr/bin/env python3
"""make_pak.py -- gathers the read-only files of vita/data/res into vita/data/umk3.pak.

Why: on the Vita every open of a file in res/ costs on the order of 100 ms (the folder has ~1900
entries and the card walks the directory on every open) and a fight opens hundreds. With the pack, the game
(vita/src/vita_fs.c) reads the index once, keeps umk3.pak open and serves each file with a single sceIoPread.

Usage:  python3 vita/make_pak.py [data_folder]      (vita/data by default)

Left OUT (still loose in res/): the saves and settings the game writes (files with no extension,
.sqlite, .tmp) and the .mp3 music, which the decoder opens by itself.

Format (little endian), the same vita_fs.c reads:
    "UMK3PAK1"  u32 n  u32 names_bytes
    { u32 name_off, u32 data_off, u32 size } x n     sorted by name
    names: paths relative to ux0:data/umk3/, lower case, NUL-terminated
    data
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
        sys.exit("%s does not exist (run vita/build.sh with your .ipa first)" % res)

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
                print("warning: %s and %s differ only in case; keeping the first" % (files[key], rel))
                continue
            files[key] = full

    keys = sorted(files, key=lambda k: k.encode("utf-8"))   # the same order as strcmp on the Vita
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
                sys.exit("The pack would exceed 4 GB")
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
    print("%s: %d files, %.1f MB (%d left loose)" % (out, len(keys), total / 1e6, skipped))


if __name__ == "__main__":
    main()
