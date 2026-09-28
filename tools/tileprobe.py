import subprocess, sys

# usage:
#   tileprobe.py <atlas.png> <tile_px>              -> near-white / light tile summary
#   tileprobe.py <atlas.png> <tile_px> tx,ty ...    -> ASCII detail for those tiles
#   tileprobe.py compare                            -> tiles that are near-white in every pack
PACKS = [("textures/terrain.png", 32), ("textures/terrain2.png", 16), ("textures/terrain3.png", 16)]

def load(path, tile_px):
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', path, '-f', 'rawvideo',
                                   '-pix_fmt', 'rgba', '-'])
    W = tile_px * 16
    return raw, W

def pixel(raw, W, x, y):
    i = (y * W + x) * 4
    return raw[i], raw[i + 1], raw[i + 2], raw[i + 3]

def stats(raw, W, tile_px, tx, ty):
    total = clear = 0
    rs = gs = bs = white = 0
    for y in range(ty * tile_px, (ty + 1) * tile_px):
        for x in range(tx * tile_px, (tx + 1) * tile_px):
            r, g, b, a = pixel(raw, W, x, y)
            total += 1
            if a < 128:
                clear += 1
                continue
            rs += r; gs += g; bs += b
            if r > 200 and g > 200 and b > 200:
                white += 1
    opaque = total - clear
    if not opaque:
        return None
    return (rs / opaque, gs / opaque, bs / opaque, white / opaque, clear / total)

# Tiles the shared atlas table (terrain.cpp's texture_atlas + the special index)
# already maps to a block, plus the two painted rows (chest, bed).
USED = set()
for _y, _xs in {
    0: [0, 1, 2, 3, 4, 7, 8, 9, 10], 1: [0, 1, 2, 4, 5, 6, 7, 8],
    2: [0, 1, 2, 3, 11, 12, 13], 3: [0, 1, 2, 3, 5, 11, 12, 14],
    4: [0, 1, 2], 5: [0, 1, 2], 6: [6, 7, 9], 7: [1, 2, 5, 6, 7],
    8: [1, 2], 9: [1, 2], 10: [1, 2], 11: [1, 2], 12: [1, 2, 6, 7],
    13: [1, 2, 6], 14: [1],
}.items():
    for _x in _xs:
        USED.add((_x, _y))
for _t in [(4, 0), (4, 3), (11, 0), (9, 7), (14, 8), (1, 6), (13, 12),
           (13, 14), (15, 5), (3, 13), (1, 0), (4, 10)]:
    USED.add(_t)

if sys.argv[1] == "free":
    # Unused tiles that actually have art in EVERY pack: these are the only tiles
    # a new block can use without one of the packs showing a placeholder.
    tables = []
    for path, px in PACKS:
        raw, W = load(path, px)
        tables.append((path, [[stats(raw, W, px, tx, ty) for tx in range(16)]
                              for ty in range(16)]))
    print("unused, with art in every pack:")
    for ty in range(16):
        row = []
        for tx in range(16):
            if (tx, ty) in USED:
                continue
            vals = [t[1][ty][tx] for t in tables]
            if any(v is None for v in vals) or any(v[3] > 0.9 and v[4] > 0.5 for v in vals):
                continue
            row.append("(%2d,%2d) " % (tx, ty) + "/".join("%3d.%3d.%3d" % (v[0], v[1], v[2]) for v in vals))
        if row:
            print("row %2d: %s" % (ty, "  ".join(row)))
    sys.exit(0)

if sys.argv[1] == "palette":
    # Mean colour of every tile, one row per atlas row, so an unused tile can be
    # recognised by what it looks like (e.g. obsidian is near-black with purple).
    for path, px in PACKS:
        raw, W = load(path, px)
        print("=== %s (tile %dpx) ===" % (path, px))
        for ty in range(16):
            cells = []
            for tx in range(16):
                s = stats(raw, W, px, tx, ty)
                cells.append("        " if not s else "%3d,%3d,%3d" % (s[0], s[1], s[2]))
            print("row %2d: %s" % (ty, " | ".join(cells)))
    sys.exit(0)

if sys.argv[1] == "compare":
    tables = []
    for path, px in PACKS:
        raw, W = load(path, px)
        tables.append((path, px, raw, W,
                       [[stats(raw, W, px, tx, ty) for tx in range(16)] for ty in range(16)]))
    print("tiles near-white (mean > 190, little transparency) in EVERY pack:")
    for ty in range(16):
        for tx in range(16):
            s = [t[4][ty][tx] for t in tables]
            if all(v and v[0] > 190 and v[4] < 0.3 for v in s):
                print("  (%2d,%2d)  " % (tx, ty) +
                      "  ".join("%s rgb %5.1f %5.1f %5.1f clear %.2f"
                                % (t[0].split('/')[-1][:9], v[0], v[1], v[2], v[4])
                                for t, v in zip(tables, s)))
    sys.exit(0)

path = sys.argv[1]
tile_px = int(sys.argv[2])
tiles = [(int(a), int(b)) for a, b in (t.split(',') for t in sys.argv[3:])]
raw, W = load(path, tile_px)
table = [[stats(raw, W, tile_px, tx, ty) for tx in range(16)] for ty in range(16)]

RAMP = " .:-=+*#%@"

if not tiles:
    print("%s (%dx%d, tile %dpx)" % (path, W, W, tile_px))
    print("near-white, mostly opaque:")
    for ty in range(16):
        for tx in range(16):
            s = table[ty][tx]
            if s and s[3] > 0.5 and s[4] < 0.5 and s[0] > 170:
                print("  (%2d,%2d)  mean rgb %6.1f %6.1f %6.1f  white %.2f  clear %.2f"
                      % (tx, ty, s[0], s[1], s[2], s[3], s[4]))
    print("mean red channel per tile:")
    for ty in range(16):
        print("row %2d: %s" % (ty, " ".join(" --" if not table[ty][tx] else "%3d" % table[ty][tx][0]
                                            for tx in range(16))))
else:
    for (tx, ty) in tiles:
        s = table[ty][tx]
        print("=== (%d,%d) %s ===" % (tx, ty, "transparent" if not s else
              "mean rgb %.1f %.1f %.1f  white %.2f clear %.2f" % s))
        for dy in range(16):
            line = ""
            for dx in range(16):
                r, g, b, a = pixel(raw, W, tx * tile_px + dx * tile_px // 16,
                                   ty * tile_px + dy * tile_px // 16)
                line += " " if a < 128 else RAMP[min(9, (r + g + b) // 3 * 10 // 256)]
            print("   |" + line + "|")
