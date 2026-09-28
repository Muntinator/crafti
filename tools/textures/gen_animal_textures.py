#!/usr/bin/env python3
"""Emit textures/{cow,pig,sheep,horse}.h quadruped skins (64x64 RGB565 for nGL).

The skins are drawn procedurally so the repository needs no binary art assets.
The UV layout is the one shared with the renderer and the model table in
livestockspecies.cpp, so a generated skin and the drawn boxes always line up:

    head : uv ( 0,  0), box  8 x  8 x  8   (model units, 1/16 block)
    body : uv ( 0, 16), box 12 x 10 x 18
    leg  : uv ( 0, 48), box  4 x 12 x  4

Each box occupies its 6-face unwrap (width 2*(w+d), height d+h), and the three
boxes do not overlap. tests/livestock_test.cc asserts that this holds.

Run from the repository root:  python3 tools/textures/gen_animal_textures.py
"""

import os

TEX = 64

# (u, v, w, h, d) per box, in model units.
HEAD = (0, 0, 8, 8, 8)
BODY = (0, 16, 12, 10, 18)
LEG = (0, 48, 4, 12, 4)
# Small solid patch used for ears and horns (no face detail on those boxes).
DETAIL = (32, 0, 4, 4, 4)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def faces(box):
    """Return the six face regions of a box, matching drawChickBox/drawQuadBox."""
    u, v, w, h, d = box
    return {
        "top": (u + d, v, w, d),
        "bottom": (u + d + w, v, w, d),
        "right": (u, v + d, d, h),
        "front": (u + d, v + d, w, h),
        "left": (u + d + w, v + d, d, h),
        "back": (u + d + w + d, v + d, w, h),
    }


class Sheet:
    def __init__(self):
        self.px = [0] * (TEX * TEX)
        self.rng = 0x1234567

    def rand(self):
        # Deterministic LCG so regeneration is byte-for-byte reproducible.
        self.rng = (1103515245 * self.rng + 12345) & 0x7FFFFFFF
        return self.rng

    def put(self, x, y, color):
        if 0 <= x < TEX and 0 <= y < TEX:
            self.px[y * TEX + x] = color

    def fill(self, region, color):
        x, y, w, h = region
        for yy in range(h):
            for xx in range(w):
                self.put(x + xx, y + yy, color)

    def sub(self, region, ox, oy, w, h, color):
        x, y, _, _ = region
        for yy in range(h):
            for xx in range(w):
                self.put(x + ox + xx, y + oy + yy, color)

    def blob(self, region, color, count, max_w, max_h):
        """Scatter roughly rectangular patches inside a face region."""
        x, y, w, h = region
        for _ in range(count):
            bw = 1 + self.rand() % max_w
            bh = 1 + self.rand() % max_h
            bx = x + self.rand() % w
            by = y + self.rand() % h
            for yy in range(bh):
                for xx in range(bw):
                    if bx + xx < x + w and by + yy < y + h:
                        self.put(bx + xx, by + yy, color)


def box_faces(sheet, box, base, side_shade=None, top_shade=None):
    f = faces(box)
    for name, region in f.items():
        color = base
        if name == "top" and top_shade is not None:
            color = top_shade
        elif name == "bottom":
            color = base
        elif side_shade is not None and name in ("right", "left", "back"):
            color = side_shade
        sheet.fill(region, color)
    return f


def draw_eyes(sheet, head_front, color, y_off=2):
    # Two small eyes on the front face of the head.
    sheet.sub(head_front, 1, y_off, 1, 2, color)
    sheet.sub(head_front, 5, y_off, 1, 2, color)


def detail(s, color):
    box_faces(s, DETAIL, color)


def cow():
    s = Sheet()
    white = rgb565(238, 238, 238)
    shade = rgb565(214, 214, 214)
    black = rgb565(38, 38, 38)
    muzzle = rgb565(238, 176, 176)
    eye = rgb565(24, 24, 24)

    hf = box_faces(s, HEAD, white, shade, rgb565(250, 250, 250))
    bf = box_faces(s, BODY, white, shade, rgb565(250, 250, 250))
    box_faces(s, LEG, shade, rgb565(200, 200, 200))
    detail(s, rgb565(216, 210, 194))

    s.blob(bf["top"], black, 3, 6, 6)
    s.blob(bf["right"], black, 2, 5, 4)
    s.blob(bf["left"], black, 2, 5, 4)
    s.blob(bf["back"], black, 1, 4, 4)

    s.sub(hf["front"], 1, 5, 6, 3, muzzle)
    draw_eyes(s, hf["front"], eye, 2)
    return s.px


def pig():
    s = Sheet()
    base = rgb565(238, 160, 160)
    shade = rgb565(214, 138, 138)
    snout = rgb565(206, 110, 110)
    eye = rgb565(40, 24, 24)

    hf = box_faces(s, HEAD, base, shade, rgb565(248, 178, 178))
    box_faces(s, BODY, base, shade, rgb565(248, 178, 178))
    box_faces(s, LEG, shade, rgb565(206, 130, 130))
    detail(s, rgb565(206, 110, 110))

    s.sub(hf["front"], 2, 5, 4, 3, snout)
    draw_eyes(s, hf["front"], eye, 2)
    return s.px


def sheep():
    s = Sheet()
    base = rgb565(234, 234, 234)
    fluff = rgb565(250, 250, 250)
    shade = rgb565(206, 206, 206)
    face = rgb565(224, 214, 202)
    eye = rgb565(30, 30, 30)

    hf = box_faces(s, HEAD, face, rgb565(206, 196, 184), rgb565(232, 224, 212))
    bf = box_faces(s, BODY, base, shade, fluff)
    box_faces(s, LEG, rgb565(200, 200, 200), rgb565(180, 180, 180))
    detail(s, rgb565(196, 186, 174))

    # Fluffy speckle over the wool so it does not read as a flat box.
    for region in (bf["top"], bf["right"], bf["left"], bf["back"], bf["front"]):
        s.blob(region, fluff, 4, 3, 3)
        s.blob(region, shade, 3, 2, 2)

    draw_eyes(s, hf["front"], eye, 2)
    return s.px


def horse():
    s = Sheet()
    base = rgb565(132, 84, 50)
    shade = rgb565(108, 66, 38)
    mane = rgb565(58, 36, 20)
    muzzle = rgb565(88, 54, 30)
    eye = rgb565(20, 20, 20)

    hf = box_faces(s, HEAD, base, shade, rgb565(150, 98, 60))
    bf = box_faces(s, BODY, base, shade, rgb565(150, 98, 60))
    box_faces(s, LEG, shade, rgb565(88, 54, 30))
    detail(s, rgb565(108, 66, 38))

    # Mane runs along the top of the head and the back of the body.
    s.fill(hf["top"], mane)
    s.sub(hf["front"], 1, 5, 6, 3, muzzle)
    draw_eyes(s, hf["front"], eye, 2)
    s.sub(bf["top"], 0, 0, 12, 4, mane)
    s.fill(bf["back"], mane)

    # A couple of lighter patches to break up the silhouette.
    s.blob(bf["right"], rgb565(150, 98, 60), 2, 4, 3)
    s.blob(bf["left"], rgb565(150, 98, 60), 2, 4, 3)
    return s.px


SPECIES = {
    "cow": cow,
    "pig": pig,
    "sheep": sheep,
    "horse": horse,
}


def write_header(name, pixels, out_dir):
    path = os.path.join(out_dir, name + ".h")
    lines = [
        "// Generated by tools/textures/gen_animal_textures.py -- do not edit by hand.",
        "// 64x64 RGB565 quadruped skin; UVs match Livestock::quadrupedModel().",
        "static uint16_t %s_data[] = {" % name,
    ]
    per_line = 12
    for i in range(0, len(pixels), per_line):
        chunk = pixels[i:i + per_line]
        sep = "," if i + per_line < len(pixels) else ""
        lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + sep)
    lines += [
        "};",
        "",
        "static TEXTURE %s_tex = {" % name,
        "    %d, %d," % (TEX, TEX),
        "    true, 0,",
        "    %s_data" % name,
        "};",
        "",
    ]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return path


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out_dir = os.path.join(root, "textures")
    for name, fn in SPECIES.items():
        pixels = fn()
        assert len(pixels) == TEX * TEX
        path = write_header(name, pixels, out_dir)
        print("wrote %s" % os.path.relpath(path, root))


if __name__ == "__main__":
    main()
