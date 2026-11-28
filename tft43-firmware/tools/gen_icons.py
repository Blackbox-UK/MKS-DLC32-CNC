#!/usr/bin/env python3
"""Generate src/icons.c: 24x24 anti-aliased (4 bits per pixel) line icons.

Icons are drawn with simple shapes at 8x size and scaled down, so they stay
crisp and need no external artwork. Usage: python3 tools/gen_icons.py
"""
import math
import os

from PIL import Image, ImageDraw

S = 24          # icon size
K = 8           # supersampling factor
W = 2.4 * K     # stroke width
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "icons.c")


def P(x, y):
    return (x * K, y * K)


def line(d, pts, w=W):
    d.line([P(*p) for p in pts], fill=255, width=int(w), joint="curve")
    for p in (pts[0], pts[-1]):
        r = w / 2
        d.ellipse([p[0] * K - r, p[1] * K - r, p[0] * K + r, p[1] * K + r], fill=255)


def poly(d, pts):
    d.polygon([P(*p) for p in pts], fill=255)


def circle(d, cx, cy, r, w=W, fill=False):
    box = [(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K]
    if fill:
        d.ellipse(box, fill=255)
    else:
        d.ellipse(box, outline=255, width=int(w))


def rect(d, x0, y0, x1, y1, w=W, fill=False, r=1.5):
    box = [x0 * K, y0 * K, x1 * K, y1 * K]
    if fill:
        d.rounded_rectangle(box, radius=r * K, fill=255)
    else:
        d.rounded_rectangle(box, radius=r * K, outline=255, width=int(w))


def arrow(d, ang):
    """Solid arrow pointing at angle (0 = right), centred."""
    c, s = math.cos(ang), math.sin(ang)

    def rot(x, y):
        return (12 + x * c - y * s, 12 + x * s + y * c)

    poly(d, [rot(9, 0), rot(0, -8), rot(0, -3.5), rot(-8, -3.5), rot(-8, 3.5), rot(0, 3.5), rot(0, 8)])


def icon_dro(d):
    circle(d, 12, 12, 8)
    for a, b in (((12, 1), (12, 7)), ((12, 17), (12, 23)), ((1, 12), (7, 12)), ((17, 12), (23, 12))):
        line(d, [a, b])
    circle(d, 12, 12, 2.2, fill=True)


def icon_move(d):
    line(d, [(12, 3), (12, 21)])
    line(d, [(3, 12), (21, 12)])
    for tip, l1, l2 in (((12, 1.5), (8.5, 5), (15.5, 5)), ((12, 22.5), (8.5, 19), (15.5, 19)),
                        ((1.5, 12), (5, 8.5), (5, 15.5)), ((22.5, 12), (19, 8.5), (19, 15.5))):
        poly(d, [tip, l1, l2])


def icon_files(d):
    poly(d, [(2, 5), (9, 5), (11, 7.5), (22, 7.5), (22, 20), (2, 20)])


def icon_spindle(d):
    rect(d, 7, 2, 17, 9, fill=True)
    rect(d, 10, 9, 14, 13, fill=True, r=0.5)
    poly(d, [(10.5, 13), (13.5, 13), (12, 22.5)])
    line(d, [(4, 15), (7, 14)], w=1.8 * K)
    line(d, [(20, 15), (17, 14)], w=1.8 * K)


def icon_console(d):
    rect(d, 1.5, 3.5, 22.5, 20.5)
    line(d, [(6, 9), (10, 12), (6, 15)])
    line(d, [(12, 16), (18, 16)])


def icon_settings(d):
    for i in range(8):
        a = i * math.pi / 4
        c, s = math.cos(a), math.sin(a)
        pts = []
        for x, y in ((-2.2, 7), (2.2, 7), (2.2, 11), (-2.2, 11)):
            pts.append((12 + x * c - y * s, 12 + x * s + y * c))
        poly(d, pts)
    circle(d, 12, 12, 7.5, fill=True)
    d.ellipse([(12 - 3) * K, (12 - 3) * K, (12 + 3) * K, (12 + 3) * K], fill=0)


def icon_home(d):
    poly(d, [(12, 2.5), (22.5, 12), (19.5, 12), (19.5, 21), (14.5, 21), (14.5, 15), (9.5, 15), (9.5, 21),
             (4.5, 21), (4.5, 12), (1.5, 12)])


def icon_unlock(d):
    rect(d, 4, 11, 20, 22, fill=True)
    d.arc([7 * K, 2 * K, 17 * K, 14 * K], 180, 330, fill=255, width=int(W))
    d.ellipse([10.5 * K, 14.5 * K, 13.5 * K, 17.5 * K], fill=0)


def icon_play(d):
    poly(d, [(6, 3), (20, 12), (6, 21)])


def icon_pause(d):
    rect(d, 5, 3, 10, 21, fill=True, r=1)
    rect(d, 14, 3, 19, 21, fill=True, r=1)


def icon_stop(d):
    rect(d, 4, 4, 20, 20, fill=True, r=2)


def icon_zero(d):
    d.ellipse([6 * K, 3 * K, 18 * K, 21 * K], outline=255, width=int(W))
    line(d, [(4, 21), (20, 3)])


def icon_probe(d):
    poly(d, [(12, 17), (6, 9), (10, 9), (10, 1.5), (14, 1.5), (14, 9), (18, 9)])
    rect(d, 2, 19, 22, 22.5, fill=True, r=0.8)


def icon_refresh(d):
    d.arc([4 * K, 4 * K, 20 * K, 20 * K], 40, 330, fill=255, width=int(W))
    poly(d, [(15, 2), (22.5, 4.5), (17, 10.5)])


def icon_wifi(d):
    for r in (10, 6.5):
        d.arc([(12 - r) * K, (19 - r) * K, (12 + r) * K, (19 + r) * K], 225, 315, fill=255, width=int(W))
    circle(d, 12, 19, 2.2, fill=True)


def icon_keyboard(d):
    rect(d, 1.5, 5, 22.5, 19)
    for y in (8.5, 12):
        for x in (5, 9, 13, 17):
            rect(d, x, y, x + 2.2, y + 2.2, fill=True, r=0.3)
    rect(d, 7, 15.2, 17, 16.8, fill=True, r=0.3)


def icon_coolant(d):
    poly(d, [(12, 2), (18, 12), (6, 12)])
    circle(d, 12, 15, 6.2, fill=True)


def icon_back(d):
    arrow(d, math.pi)


def icon_power(d):
    d.arc([3.5 * K, 4 * K, 20.5 * K, 21 * K], 300, 240, fill=255, width=int(W))
    line(d, [(12, 2), (12, 11)])


def icon_trash(d):
    rect(d, 5, 7, 19, 22, fill=True)
    rect(d, 3, 4, 21, 6, fill=True, r=0.5)
    rect(d, 9, 1.5, 15, 4, fill=True, r=0.5)


ICONS = [
    ("DRO", icon_dro), ("MOVE", icon_move), ("FILES", icon_files), ("SPINDLE", icon_spindle),
    ("CONSOLE", icon_console), ("SETTINGS", icon_settings), ("HOME", icon_home), ("UNLOCK", icon_unlock),
    ("PLAY", icon_play), ("PAUSE", icon_pause), ("STOP", icon_stop), ("ZERO", icon_zero),
    ("PROBE", icon_probe), ("REFRESH", icon_refresh), ("WIFI", icon_wifi), ("KEYBOARD", icon_keyboard),
    ("COOLANT", icon_coolant), ("BACK", icon_back), ("POWER", icon_power), ("TRASH", icon_trash),
    ("UP", lambda d: arrow(d, -math.pi / 2)), ("DOWN", lambda d: arrow(d, math.pi / 2)),
    ("LEFT", lambda d: arrow(d, math.pi)), ("RIGHT", lambda d: arrow(d, 0)),
]


def render(fn):
    img = Image.new("L", (S * K, S * K), 0)
    fn(ImageDraw.Draw(img))
    img = img.resize((S, S), Image.LANCZOS)
    px = img.tobytes()
    out = bytearray()
    for i in range(0, len(px), 2):
        out.append(((px[i] >> 4) << 4) | (px[i + 1] >> 4))
    return out


def main():
    lines = ["// Generated by tools/gen_icons.py. Do not edit.", '#include "icons.h"', "",
             f"const uint8_t icon_data[ICON_COUNT][{S * S // 2}] = {{"]
    for name, fn in ICONS:
        data = render(fn)
        lines.append(f"  [ICON_{name}] = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("  },")
    lines.append("};")
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    hdr = ["// Generated by tools/gen_icons.py. Do not edit.", "#ifndef ICONS_H", "#define ICONS_H",
           "#include <stdint.h>", f"#define ICON_SIZE {S}", "enum {", "  ICON_NONE = -1,"]
    hdr += [f"  ICON_{n}," for n, _ in ICONS]
    hdr += ["  ICON_COUNT", "};", f"extern const uint8_t icon_data[ICON_COUNT][{S * S // 2}];", "#endif"]
    with open(os.path.join(HERE, "..", "src", "icons.h"), "w") as f:
        f.write("\n".join(hdr) + "\n")
    # preview sheet for checking by eye
    sheet = Image.new("L", (len(ICONS) * (S + 4) * 3, (S + 4) * 3), 30)
    for i, (n, fn) in enumerate(ICONS):
        img = Image.new("L", (S * K, S * K), 0)
        fn(ImageDraw.Draw(img))
        img = img.resize((S, S), Image.LANCZOS).resize((S * 3, S * 3), Image.NEAREST)
        sheet.paste(img, (i * (S + 4) * 3 + 6, 6))
    sheet.save(os.path.join(HERE, "icons_preview.png"))
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
