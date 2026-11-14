#!/usr/bin/env python3
"""Extracts from a UMK3 .ipa what the Vita build needs:
     work/UMK3.armv7     the armv7 binary (the slice of the universal Mach-O)
     data/res/...        the game's resources
     data/*.x            the bundle's loose tables (frames.x, moves_data.x)
Usage: python3 vita/extract_ipa.py UMK3.ipa [--data DIR] [--bin PATH]"""
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
        sys.exit("ERROR: the universal binary has no armv7 slice")
    if data[:4] == b"\xce\xfa\xed\xfe":                       # already 32-bit little-endian
        ct, st = struct.unpack("<II", data[4:12])
        if ct == CPU_ARM and st == SUB_V7:
            return data
    sys.exit("ERROR: unrecognised binary format")

def uuid_of(thin):
    ncmds = struct.unpack("<I", thin[16:20])[0]
    off = 28
    for _ in range(ncmds):
        cmd, size = struct.unpack("<II", thin[off:off + 8])
        if cmd == 0x1B:
            return thin[off + 8:off + 24].hex()
        off += size
    return None

def extract(ipa, data_dir=None, out_bin=None):
    """Writes the armv7 binary to out_bin and the game's files to data_dir; returns out_bin."""
    here = os.path.dirname(os.path.abspath(__file__))
    data_dir = data_dir or os.path.join(here, "data")
    out_bin = out_bin or os.path.join(here, "work", "UMK3.armv7")
    z = zipfile.ZipFile(ipa)
    apps = sorted({n.split("/")[1] for n in z.namelist()
                   if n.startswith("Payload/") and n.split("/")[1].endswith(".app")})
    if not apps:
        sys.exit("ERROR: no Payload/*.app in the .ipa")
    pre = f"Payload/{apps[0]}/"
    exe = "UMK3"
    try:
        exe = plistlib.loads(z.read(pre + "Info.plist")).get("CFBundleExecutable", exe)
    except Exception:
        pass
    thin = thin_armv7(z.read(pre + exe))
    os.makedirs(os.path.dirname(os.path.abspath(out_bin)), exist_ok=True)
    open(out_bin, "wb").write(thin)
    u = uuid_of(thin)
    print(f"armv7 binary: {out_bin} ({len(thin)} bytes)")
    print(f"UUID: {u}  ->", "OK, version 1.2.59" if u == EXPECT_UUID else "DIFFERENT from the expected one (1.2.59)!")

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
    # The port reads Info.plist (the language files) from inside res/; on the device it sits beside res/.
    os.makedirs(os.path.join(data_dir, "res"), exist_ok=True)
    with open(os.path.join(data_dir, "res", "Info.plist"), "wb") as f:
        f.write(z.read(pre + "Info.plist"))
    n += 1
    print(f"{n} data files -> {data_dir}")
    return out_bin


def main():
    args = sys.argv[1:]
    opts = {"--data": None, "--bin": None}
    pos = []
    while args:
        a = args.pop(0)
        if a in opts and args:
            opts[a] = args.pop(0)
        else:
            pos.append(a)
    if len(pos) != 1:
        sys.exit(__doc__)
    extract(pos[0], opts["--data"], opts["--bin"])


if __name__ == "__main__":
    main()
