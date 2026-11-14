#!/usr/bin/env python3
"""Extrae de un .ipa de UMK3 lo que necesita el build de Vita:
     work/UMK3.armv7     binario armv7 (rebanada del Mach-O universal)
     data/res/...        recursos del juego
     data/*.x            tablas sueltas del bundle (frames.x, moves_data.x)
Uso: python3 vita/extract_ipa.py UMK3.ipa"""
import os, sys, struct, zipfile, plistlib

EXPECT_UUID = "90d6f56a18e2303f8f2053b02a42742e"
CPU_ARM, SUB_V7 = 12, 9

def thin_armv7(data):
    if data[:4] == b"\xca\xfe\xba\xbe":                       # universal (big-endian)
        n = struct.unpack(">I", data[4:8])[0]
        for i in range(n):
            ct, st, off, size, _ = struct.unpack(">5I", data[8 + 20 * i: 28 + 20 * i])
            if ct == CPU_ARM and st == SUB_V7:
                return data[off:off + size]
        sys.exit("ERROR: el binario universal no trae rebanada armv7")
    if data[:4] == b"\xce\xfa\xed\xfe":                       # ya es 32-bit little-endian
        ct, st = struct.unpack("<II", data[4:12])
        if ct == CPU_ARM and st == SUB_V7:
            return data
    sys.exit("ERROR: formato de binario no reconocido")

def uuid_of(thin):
    ncmds = struct.unpack("<I", thin[16:20])[0]
    off = 28
    for _ in range(ncmds):
        cmd, size = struct.unpack("<II", thin[off:off + 8])
        if cmd == 0x1B:
            return thin[off + 8:off + 24].hex()
        off += size
    return None

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ipa = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    work, data_dir = os.path.join(here, "work"), os.path.join(here, "data")
    z = zipfile.ZipFile(ipa)
    apps = sorted({n.split("/")[1] for n in z.namelist()
                   if n.startswith("Payload/") and n.split("/")[1].endswith(".app")})
    if not apps:
        sys.exit("ERROR: no hay Payload/*.app en el .ipa")
    pre = f"Payload/{apps[0]}/"
    exe = "UMK3"
    try:
        exe = plistlib.loads(z.read(pre + "Info.plist")).get("CFBundleExecutable", exe)
    except Exception:
        pass
    thin = thin_armv7(z.read(pre + exe))
    os.makedirs(work, exist_ok=True)
    out_bin = os.path.join(work, "UMK3.armv7")
    open(out_bin, "wb").write(thin)
    u = uuid_of(thin)
    print(f"binario armv7: {out_bin} ({len(thin)} bytes)")
    print(f"UUID: {u}  ->", "OK, version 1.2.59" if u == EXPECT_UUID else "DISTINTO del esperado (1.2.59)!")

    n = 0
    for name in z.namelist():
        if not name.startswith(pre) or name.endswith("/"):
            continue
        rel = name[len(pre):]
        if rel.startswith("res/") or ("/" not in rel and rel.endswith(".x")):
            dest = os.path.join(data_dir, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(z.read(name))
            n += 1
    print(f"{n} archivos de datos -> {data_dir}")
    print(f"\nSiguiente: ./vita/build.sh {out_bin}")

main()
