#!/usr/bin/env python3
"""Generate the SBS launcher icons (Cardboard goggles, two identical lens views).

usage: make_icons.py <res dir> <label> <accent hex> [--round-only-in <dir>]

Writes drawable-nodpi/ic_launcher_foreground.png (adaptive foreground, 432 px)
and mipmap-*/ic_launcher.png + ic_launcher_round.png (legacy icons).
The art is original: no game logo or Rockstar artwork is used.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"
SS = 4  # supersampling factor


def hex2rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def font(size):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def goggles(size, label, accent):
    """Transparent square image with the goggles centred in the adaptive safe zone."""
    s = size * SS
    k = s / 432.0
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    acc = hex2rgb(accent)

    def R(v):
        return v * k

    # body
    bx0, by0, bx1, by1 = 86, 150, 346, 262
    d.rounded_rectangle([R(bx0), R(by0), R(bx1), R(by1)], radius=R(46),
                        fill=(24, 31, 46, 255), outline=acc + (255,), width=int(R(7)))
    # nose notch
    d.pieslice([R(190), R(232), R(242), R(292)], 180, 360, fill=(0, 0, 0, 0))
    d.arc([R(190), R(232), R(242), R(292)], 180, 360, fill=acc + (255,), width=int(R(6)))
    # lenses
    for cx, off in ((152, 3), (280, -3)):
        cy = 206
        r = 44
        d.ellipse([R(cx - r - 5), R(cy - r - 5), R(cx + r + 5), R(cy + r + 5)], fill=acc + (255,))
        d.ellipse([R(cx - r), R(cy - r), R(cx + r), R(cy + r)], fill=(10, 14, 24, 255))
        # same label in both eyes with a small opposite offset = the stereo hint
        f = font(max(1, int((34 if len(label) > 2 else 40) * k)))
        tw = d.textlength(label, font=f)
        bb = f.getbbox(label)
        th = bb[3] - bb[1]
        d.text((R(cx + off) - tw / 2, R(cy) - th / 2 - bb[1]), label, font=f, fill=(255, 255, 255, 255))
        # lens highlight
        d.arc([R(cx - r + 8), R(cy - r + 8), R(cx + r - 8), R(cy + r - 8)], 200, 250,
              fill=(255, 255, 255, 140), width=int(R(4)))
    # caption
    f = font(max(1, int(34 * k)))
    t = "SBS"
    tw = d.textlength(t, font=f)
    bb = f.getbbox(t)
    d.text((R(216) - tw / 2, R(296) - bb[1]), t, font=f, fill=acc + (255,))
    return im.resize((size, size), Image.LANCZOS)


def legacy(size, label, accent, circle):
    bg = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(bg)
    col = (11, 15, 26, 255)
    if circle:
        d.ellipse([0, 0, size - 1, size - 1], fill=col)
    else:
        d.rounded_rectangle([0, 0, size - 1, size - 1], radius=int(size * 0.22), fill=col)
    fg = goggles(int(size * 1.2), label, accent)  # foreground art spans the safe zone
    off = (size - fg.width) // 2
    bg.alpha_composite(fg, (off, off)) if off >= 0 else bg.alpha_composite(
        fg.crop((-off, -off, -off + size, -off + size)), (0, 0))
    return bg


def main():
    res, label, accent = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(os.path.join(res, "drawable-nodpi"), exist_ok=True)
    goggles(432, label, accent).save(os.path.join(res, "drawable-nodpi", "ic_launcher_foreground.png"))
    for dpi, px in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        d = os.path.join(res, "mipmap-" + dpi)
        os.makedirs(d, exist_ok=True)
        legacy(px, label, accent, False).save(os.path.join(d, "ic_launcher.png"))
        legacy(px, label, accent, True).save(os.path.join(d, "ic_launcher_round.png"))
    if "--preview" in sys.argv:  # 512 px preview for the README
        legacy(512, label, accent, True).save("icon_preview_%s.png" % label)


if __name__ == "__main__":
    main()
