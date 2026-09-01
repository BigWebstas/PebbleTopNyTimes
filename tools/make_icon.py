"""Regenerate the launcher icon from the NYT dev-portal wordmark.

Source: developer.nytimes.com header logo ({T} Developers), saved next to this
file as nyt-devportal-logo.jpg. Produces a 25x25 white-on-transparent PNG for
the Pebble menu icon.

    python3 tools/make_icon.py            # default: the {T} braces mark, black ink
    python3 tools/make_icon.py T          # just the blackletter T (the favicon)
    python3 tools/make_icon.py white      # white ink instead of black
"""
import os
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "nyt-devportal-logo.jpg")
DEST = os.path.join(HERE, "..", "resources", "images", "menu_icon.png")
SIZE = 25


def to_ink_alpha(img, rgb, thresh=128):
    """Dark source pixels -> opaque `rgb` ink, light background -> transparent."""
    g = img.convert("L")
    out = Image.new("RGBA", g.size, rgb + (0,))
    op, gp = out.load(), g.load()
    for y in range(g.size[1]):
        for x in range(g.size[0]):
            if gp[x, y] < thresh:
                a = int((thresh - gp[x, y]) / thresh * 255)
                op[x, y] = rgb + (min(255, a + 90),)
    return out


def fit_center(img, size):
    w, h = img.size
    s = min(size / w, size / h)
    nw, nh = max(1, round(w * s)), max(1, round(h * s))
    r = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    canvas.paste(r, ((size - nw) // 2, (size - nh) // 2), r)
    return canvas


def harden(img, rgb, cut=90):
    p = img.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            p[x, y] = rgb + (255 if p[x, y][3] >= cut else 0,)
    return img


args = sys.argv[1:]
ink = (255, 255, 255) if "white" in args else (0, 0, 0)
logo = Image.open(SRC)
# glyph bounds measured from the 330x64 wordmark: { = x1..17, T = x22..60, } = x68..82
box = (20, 4, 62, 60) if "T" in args else (0, 4, 84, 60)
icon = harden(fit_center(to_ink_alpha(logo.crop(box), ink), SIZE), ink)
icon.save(os.path.normpath(DEST))
print("wrote", os.path.normpath(DEST))
