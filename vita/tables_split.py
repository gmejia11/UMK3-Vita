#!/usr/bin/env python3
"""Splits the tables generated from the game's binary into code and data, so
the Vita build can be shared without the game's data inside.

The build generates three C files from the player's own binary
(tools/build/logic_tables.py, level_info.py, seq_data.py): every table of the
fight engine, with its values. Compiled as they are, the game's data ends up
inside eboot.bin. This script turns each of them into:

  * a stub .c file -- the same tables, the same names and sizes, but no
    values (zero-filled, writable), plus a list of the tables and of the
    symbols their address words point at. That is what goes into the VPK.
  * the values, in umk3_tables.bin: each table's bytes and its relocations
    (word -> symbol + addend). Every player makes this file from their own
    .ipa (vita/make_data.py); vita/src/vita_tables.c loads it at start-up.

Both sides carry a hash of the layout (file tags, table names and sizes, and
the symbol lists), so a umk3_tables.bin that does not match the VPK is
refused instead of loaded wrong.

Usage:
  tables_split.py --stub-dir DIR  TAG=file.c ...    write DIR/<TAG>_stub.c
  tables_split.py --bin OUT.bin   TAG=file.c ...    write the data file
  (both options may be given at once)
"""
import argparse
import hashlib
import os
import re
import struct
import sys

ELEM = {
    "uintptr_t": 4, "uint32_t": 4, "int32_t": 4,
    "uint16_t": 2, "unsigned short": 2, "int16_t": 2, "short": 2,
    "unsigned char": 1, "char": 1, "uint8_t": 1, "signed char": 1,
}
HEAD = re.compile(
    r"^(?P<static>static\s+)?(?P<const>const\s+)?(?P<type>unsigned char|unsigned short|signed char|"
    r"uintptr_t|uint32_t|int32_t|uint16_t|int16_t|uint8_t|short|char)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z_0-9]*)\[(?P<n>[^\]]+)\](?P<attrs>[^=;]*)=\s*\{(?P<rest>.*)$")
REL = re.compile(r"UMK3_REL\(\s*([A-Za-z_][A-Za-z_0-9]*)\s*,\s*([^)]+)\)")


def parse_int(tok):
    tok = tok.strip()
    neg = tok.startswith("-")
    if neg:
        tok = tok[1:].strip()
    tok = tok.rstrip("uUlL")
    v = int(tok, 16) if tok.lower().startswith("0x") else int(tok, 10)
    return -v if neg else v


def parse_file(path):
    """Returns (lines, objects). Each object: dict(name, type, size, start, end,
    head, data(bytearray), rels[(byte_off, sym, addend)])."""
    lines = open(path, encoding="utf-8").read().split("\n")
    objs = []
    i = 0
    while i < len(lines):
        m = HEAD.match(lines[i])
        if not m or m.group("rest").strip().startswith("0}"):
            i += 1
            continue
        start = i
        body = [m.group("rest")]
        while not body[-1].rstrip().endswith("};"):
            i += 1
            if i >= len(lines):
                sys.exit(f"{path}: unterminated table {m.group('name')}")
            body.append(lines[i])
        end = i
        text = " ".join(body)
        text = text[:text.rindex("}")]
        esize = ELEM[m.group("type")]
        count = int(m.group("n"), 0)
        data = bytearray(esize * count)
        rels = []
        toks = [t.strip() for t in REL.sub(lambda r: f"@{r.group(1)}@{r.group(2)}", text).split(",")]
        toks = [t for t in toks if t]
        if len(toks) > count:
            sys.exit(f"{path}: {m.group('name')} has {len(toks)} values for {count} slots")
        for k, t in enumerate(toks):
            off = k * esize
            if t.startswith("@"):
                if esize != 4:
                    sys.exit(f"{path}: address in a {esize}-byte table {m.group('name')}")
                _, sym, add = t.split("@")
                rels.append((off, sym, parse_int(add)))
            else:
                v = parse_int(t) & ((1 << (8 * esize)) - 1)
                data[off:off + esize] = v.to_bytes(esize, "little")
        objs.append(dict(name=m.group("name"), type=m.group("type"), size=len(data),
                         start=start, end=end, m=m, data=data, rels=rels))
        i += 1
    return lines, objs


def layout_hash(files):
    h = hashlib.sha1()
    for tag, (_, objs, syms) in files:
        h.update(f"[{tag}]".encode())
        for o in objs:
            h.update(f"{o['name']}:{o['size']};".encode())
        h.update(("|" + ",".join(syms)).encode())
    return int.from_bytes(h.digest()[:4], "little")


def symbols(objs):
    seen = []
    for o in objs:
        for _, s, _ in o["rels"]:
            if s not in seen:
                seen.append(s)
    return sorted(seen)


def write_stub(path, tag, lines, objs, syms, lhash):
    out = list(lines)
    for o in reversed(objs):
        m = o["m"]
        static = "static " if m.group("static") else ""
        attrs = m.group("attrs").rstrip()
        decl = f"{static}{m.group('type')} {o['name']}[{m.group('n')}]{attrs};" \
               f"  /* values loaded from umk3_tables.bin (vita_tables.c) */"
        out[o["start"]:o["end"] + 1] = [decl]
    out.append("")
    out.append(f"/* ---- added by vita/tables_split.py: this file's tables for vita_tables.c ---- */")
    out.append("struct umk3_tbl_obj { void *p; unsigned size; };")
    out.append(f"const struct umk3_tbl_obj umk3_tbl_{tag}_objs[] = {{")
    for o in objs:
        out.append(f"    {{ (void *){o['name']}, {o['size']}u }},")
    out.append("    { 0, 0 }")
    out.append("};")
    out.append(f"const unsigned umk3_tbl_{tag}_nobjs = {len(objs)};")
    out.append(f"void *const umk3_tbl_{tag}_syms[] = {{")
    for s in syms:
        out.append(f"    (void *){s},")
    out.append("    0")
    out.append("};")
    out.append(f"const unsigned umk3_tbl_{tag}_nsyms = {len(syms)};")
    out.append(f"const unsigned umk3_tbl_layout_hash_{tag} = 0x{lhash:08x}u;")
    open(path, "w", encoding="utf-8").write("\n".join(out) + "\n")


def write_bin(path, files, lhash):
    b = bytearray(b"UMK3TBL1")
    b += struct.pack("<II", lhash, len(files))
    for tag, (_, objs, syms) in files:
        tb = tag.encode()
        b += struct.pack("<I", len(tb)) + tb
        b += struct.pack("<I", len(objs))
        index = {s: k for k, s in enumerate(syms)}
        for o in objs:
            b += struct.pack("<II", o["size"], len(o["rels"]))
            b += o["data"]
            b += b"\0" * ((-o["size"]) % 4)      # padded by the table's own size, as vita_tables.c reads it
            for off, sym, add in o["rels"]:
                b += struct.pack("<IIi", off, index[sym], add)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    open(path, "wb").write(b)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stub-dir")
    ap.add_argument("--bin")
    ap.add_argument("files", nargs="+", help="TAG=path.c")
    a = ap.parse_args()
    files = []
    for spec in a.files:
        tag, _, path = spec.partition("=")
        if not path or not re.fullmatch(r"[a-z_]+", tag):
            sys.exit(f"bad argument {spec!r}: expected TAG=file.c")
        lines, objs = parse_file(path)
        files.append((tag, (lines, objs, symbols(objs))))
    lhash = layout_hash(files)
    if a.stub_dir:
        os.makedirs(a.stub_dir, exist_ok=True)
        for tag, (lines, objs, syms) in files:
            write_stub(os.path.join(a.stub_dir, f"{tag}_stub.c"), tag, lines, objs, syms, lhash)
    if a.bin:
        write_bin(a.bin, files, lhash)
    total = sum(len(o) for _, (_, o, _) in files)
    print(f"tables_split: {total} tables in {len(files)} files, layout 0x{lhash:08x}")


if __name__ == "__main__":
    main()
