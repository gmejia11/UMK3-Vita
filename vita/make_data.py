#!/usr/bin/env python3
"""Makes everything the Vita port needs from YOUR copy of the game, ready to copy to ux0:data/umk3/.

The VPK carries no game data. Run this once with your own .ipa of Ultimate
Mortal Kombat 3 for iOS (v1.2.59) and copy the folder it fills to the Vita:

    python3 vita/make_data.py UMK3_1.2.59.ipa            (fills vita/data/)
    python3 vita/make_data.py UMK3_1.2.59.ipa --out DIR

It writes:
    res/, frames.x, moves_data.x   the game's files, from the .ipa
    umk3_tables.bin                the fight engine's tables, from the game's binary
    umk3.pak                       res/ packed for fast loading (make_pak.py)

Then copy the whole folder to ux0:data/umk3/ (VitaShell's FTP, or
./vita/deploy_data.sh VITA_IP --all). Use the repository at the same version
as the VPK: umk3_tables.bin has to match the VPK's layout, and the game says
so on start-up if it does not.

Needs Python 3 only.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOLS = os.path.join(ROOT, "tools", "build")


def run(*args):
    r = subprocess.run([sys.executable, *args])
    if r.returncode != 0:
        sys.exit(f"make_data: failed: {os.path.basename(args[0])}")


def main():
    ap = argparse.ArgumentParser(description="Make the Vita port's data from your own .ipa.")
    ap.add_argument("ipa", help="your Ultimate Mortal Kombat 3 1.2.59 .ipa")
    ap.add_argument("--out", default=os.path.join(HERE, "data"), help="output folder (default: vita/data)")
    a = ap.parse_args()
    if not os.path.isfile(a.ipa):
        sys.exit(f"make_data: not found: {a.ipa}")
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="umk3_vita_") as tmp:
        binary = os.path.join(tmp, "UMK3.bin")
        print("1/4  unpacking the .ipa (game files and binary)")
        run(os.path.join(HERE, "extract_ipa.py"), a.ipa, "--data", out, "--bin", binary)

        print("2/4  reading the fight engine's tables from the binary")
        logic, level, seq = (os.path.join(tmp, n) for n in ("logic_tables.c", "level_info.c", "seq_data.c"))
        run(os.path.join(TOOLS, "logic_tables.py"), binary, logic)
        run(os.path.join(TOOLS, "level_info.py"), binary, level)
        run(os.path.join(TOOLS, "seq_data.py"), binary, seq)

        print("3/4  writing umk3_tables.bin")
        run(os.path.join(HERE, "tables_split.py"), "--bin", os.path.join(out, "umk3_tables.bin"),
            f"logic={logic}", f"level={level}", f"seq={seq}")

    print("4/4  packing res/ into umk3.pak")
    run(os.path.join(HERE, "make_pak.py"), out)

    print(f"\nDone: {out}\nCopy everything in it to ux0:data/umk3/ on the Vita "
          f"(or ./vita/deploy_data.sh VITA_IP --all).")


if __name__ == "__main__":
    main()
