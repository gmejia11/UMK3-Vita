#!/usr/bin/env python3
"""Makes the VPK's images (icon and LiveArea) from YOUR .ipa.

The repository carries no game images (they are copyrighted): this script
creates them in vita/sce_sys/ from the iTunesArtwork in each user's .ipa.

    vita/sce_sys/icon0.png                       128x128
    vita/sce_sys/livearea/contents/bg.png        840x500
    vita/sce_sys/livearea/contents/startup.png   280x158

All 8-bit palette PNGs, as the Vita wants. Without Pillow, or without
iTunesArtwork in the .ipa, it makes plain single-colour images so the VPK can
still be packed.

Usage: python3 vita/make_livearea.py UMK3.ipa
       python3 vita/make_livearea.py --generic      (no game artwork: for a VPK to share)
"""
import io
import os
import struct
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = {
    "icon0": (os.path.join(HERE, "sce_sys", "icon0.png"), (128, 128)),
    "bg": (os.path.join(HERE, "sce_sys", "livearea", "contents", "bg.png"), (840, 500)),
    "startup": (os.path.join(HERE, "sce_sys", "livearea", "contents", "startup.png"), (280, 158)),
}
BACKGROUND = (12, 12, 20)


def artwork_from_ipa(path):
    """The bytes of the .ipa's iTunesArtwork (a 512x512 JPEG), or None."""
    if not path or not os.path.isfile(path):
        return None
    try:
        with zipfile.ZipFile(path) as z:
            for name in z.namelist():
                if os.path.basename(name) == "iTunesArtwork":
                    return z.read(name)
    except zipfile.BadZipFile:
        pass
    return None


def solid_png(path, size, rgb):
    """A single-colour palette PNG, without Pillow."""
    w, h = size

    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + b"\x00" * w for _ in range(h))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
           + chunk(b"PLTE", bytes(rgb))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def generic(Image):
    """A plain icon and LiveArea with the port's name only -- nothing from the game."""
    from PIL import ImageDraw, ImageFont
    for key, (path, (w, h)) in OUT.items():
        img = Image.new("RGB", (w, h), BACKGROUND)
        d = ImageDraw.Draw(img)
        for y in range(h):                       # a dark red gradient
            t = y / max(1, h - 1)
            d.line([(0, y), (w, y)], fill=(int(90 - 60 * t), 8, 12))
        lines = ["UMK3", "VITA"] if key == "icon0" else ["UMK3", "PS VITA PORT"]
        size = int(h * (0.30 if key == "icon0" else 0.22))
        try:
            font = ImageFont.load_default(size=size)
        except TypeError:                        # Pillow older than 10.1
            font = ImageFont.load_default()
        boxes = [d.textbbox((0, 0), s, font=font) for s in lines]
        total = sum(b[3] - b[1] for b in boxes) + size // 4
        y = (h - total) // 2
        for s, b in zip(lines, boxes):
            d.text(((w - (b[2] - b[0])) // 2 - b[0], y - b[1]), s, font=font, fill=(235, 225, 210))
            y += (b[3] - b[1]) + size // 4
        img.quantize(colors=256, method=Image.MEDIANCUT).save(path, optimize=True)
    print("make_livearea: generic icon and LiveArea in vita/sce_sys/")


def main():
    if "--generic" in sys.argv[1:]:
        for path, _ in OUT.values():
            os.makedirs(os.path.dirname(path), exist_ok=True)
        try:
            from PIL import Image
        except ImportError:
            for path, size in OUT.values():
                solid_png(path, size, BACKGROUND)
            print("make_livearea: no Pillow; plain images in vita/sce_sys/")
            return
        generic(Image)
        return
    ipa = sys.argv[1] if len(sys.argv) > 1 else None
    for path, _ in OUT.values():
        os.makedirs(os.path.dirname(path), exist_ok=True)

    art = artwork_from_ipa(ipa)
    try:
        from PIL import Image
    except ImportError:
        Image = None

    if art is None or Image is None:
        why = "no Pillow (pip install pillow)" if Image is None else "the .ipa has no iTunesArtwork"
        print(f"make_livearea: {why}; plain images in vita/sce_sys/")
        for path, size in OUT.values():
            solid_png(path, size, BACKGROUND)
        return

    src = Image.open(io.BytesIO(art)).convert("RGB")
    for key, (path, (w, h)) in OUT.items():
        if key == "icon0":
            img = src.resize((w, h), Image.LANCZOS)
        else:
            # the icon centred on a dark background, with a margin
            side = int(min(w, h) * (0.8 if key == "bg" else 0.9))
            img = Image.new("RGB", (w, h), BACKGROUND)
            img.paste(src.resize((side, side), Image.LANCZOS), ((w - side) // 2, (h - side) // 2))
        img.quantize(colors=256, method=Image.MEDIANCUT).save(path, optimize=True)
    print("make_livearea: icon and LiveArea made from the .ipa in vita/sce_sys/")


if __name__ == "__main__":
    main()
