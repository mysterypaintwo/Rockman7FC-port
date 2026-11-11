#!/usr/bin/env python3
"""Build the port's icons from the icon inside the game executable.

    make-icons.py <rm7fc.exe or mm7fc.exe> <out_dir>

Writes  out_dir/rockman7fc.png  (256x256, Linux)
        out_dir/rockman7fc.ico  (16-256, Windows)
        out_dir/rockman7fc.iconset/  (macOS; run `iconutil -c icns` on it)

The game's icon is a 32x32 4-bit Windows icon. Windows DIB palettes are stored
as B,G,R — reading them as R,G,B is what turned the blue helmet orange in the
first version of these icons. Standard library only.
"""
import struct
import sys
import zlib
import os


def pe_icon(path):
    """Returns (width, height, rgba rows) of the first icon in a PE file."""
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    rsrc_rva = struct.unpack_from("<I", d, pe + 24 + 96 + 16)[0]
    secs = []
    for i in range(nsec):
        s = pe + 24 + optsz + 40 * i
        vs, va, rs, ra = struct.unpack_from("<IIII", d, s + 8)
        secs.append((va, max(vs, rs), ra))

    def off(rva):
        for va, sz, ra in secs:
            if va <= rva < va + sz:
                return rva - va + ra
        raise ValueError("bad rva %x" % rva)

    base = off(rsrc_rva)

    def entries(o):
        nn, ni = struct.unpack_from("<HH", d, base + o + 12)
        for i in range(nn + ni):
            yield struct.unpack_from("<II", d, base + o + 16 + 8 * i)

    def leaf(ptr):
        while ptr & 0x80000000:
            ptr = next(entries(ptr & 0x7FFFFFFF))[1]
        rva, size = struct.unpack_from("<II", d, base + ptr)
        return d[off(rva):off(rva) + size]

    types = dict(entries(0))
    icons = {name: leaf(ptr) for name, ptr in entries(types[3] & 0x7FFFFFFF)}
    grp = leaf(next(entries(types[14] & 0x7FFFFFFF))[1])
    iid = struct.unpack_from("<H", grp, 6 + 12)[0]
    img = icons[iid]

    hs, w, h2, _planes, bpp = struct.unpack_from("<IiiHH", img, 0)
    h = h2 // 2
    if bpp != 4:
        raise SystemExit("expected a 4-bit icon, got %d-bit" % bpp)
    pal = []
    for i in range(16):
        b, g, r = img[hs + 4 * i:hs + 4 * i + 3]          # B,G,R order
        pal.append((r, g, b))
    xor = hs + 64
    xstride = ((w * 4 + 31) // 32) * 4
    and_ = xor + xstride * h
    astride = ((w + 31) // 32) * 4
    rows = []
    for y in range(h):
        sy = h - 1 - y                                   # DIBs are bottom-up
        row = []
        for x in range(w):
            byte = img[xor + sy * xstride + x // 2]
            idx = (byte >> 4) if x % 2 == 0 else byte & 15
            transparent = (img[and_ + sy * astride + x // 8] >> (7 - x % 8)) & 1
            r, g, b = pal[idx]
            row.append((r, g, b, 0 if transparent else 255))
        rows.append(row)
    # The art is 16x16 drawn at 2x; collapse it so every icon size scales
    # from true pixels.
    while w % 2 == 0 and h % 2 == 0 and all(
            rows[y][x] == rows[y | 1][x] == rows[y][x | 1] == rows[y | 1][x | 1]
            for y in range(0, h, 2) for x in range(0, w, 2)):
        rows = [row[::2] for row in rows[::2]]
        w //= 2
        h //= 2
    return w, h, rows


def render(sprite, size):
    """Sprite centred on a rounded dark tile, nearest-neighbour scaled."""
    w, h, rows = sprite
    px = [[(0, 0, 0, 0)] * size for _ in range(size)]
    radius = size * 0.2
    inset = size * 0.04
    bg, edge = (24, 28, 52), (70, 82, 140)

    def inside(x, y, shrink):
        lo, hi = inset + shrink, size - inset - shrink
        cx = min(max(x, lo + radius), hi - radius)
        cy = min(max(y, lo + radius), hi - radius)
        return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2 and lo <= x <= hi and lo <= y <= hi

    border = max(1.0, size / 128)
    for y in range(size):
        for x in range(size):
            # 4x supersampling for smooth tile corners
            cov_out = cov_in = 0
            for sy in (0.125, 0.375, 0.625, 0.875):
                for sx in (0.125, 0.375, 0.625, 0.875):
                    if inside(x + sx, y + sy, 0):
                        cov_out += 1
                        if inside(x + sx, y + sy, border):
                            cov_in += 1
            if cov_out:
                t = cov_in / cov_out
                c = tuple(int(edge[i] * (1 - t) + bg[i] * t) for i in range(3))
                px[y][x] = c + (int(255 * cov_out / 16),)

    scale = max(1, int(size * 0.72) // w)
    ox = (size - w * scale) // 2
    oy = (size - h * scale) // 2
    for y in range(h):
        for x in range(w):
            c = rows[y][x]
            if c[3] == 0:
                continue
            for yy in range(scale):
                for xx in range(scale):
                    px[oy + y * scale + yy][ox + x * scale + xx] = c
    return px


def png(px):
    h, w = len(px), len(px[0])
    raw = b"".join(b"\0" + bytes(v for p in row for v in p) for row in px)

    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    sprite = pe_icon(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    cache = {}

    def get(n):
        if n not in cache:
            cache[n] = png(render(sprite, n))
        return cache[n]

    open(os.path.join(out, "rockman7fc.png"), "wb").write(get(256))

    sizes = [16, 32, 48, 256]
    head = struct.pack("<HHH", 0, 1, len(sizes))
    body = b""
    pos = 6 + 16 * len(sizes)
    for n in sizes:
        data = get(n)
        head += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(data), pos + len(body))
        body += data
    open(os.path.join(out, "rockman7fc.ico"), "wb").write(head + body)

    iconset = os.path.join(out, "rockman7fc.iconset")
    os.makedirs(iconset, exist_ok=True)
    for n in (16, 32, 128, 256, 512):
        open(os.path.join(iconset, "icon_%dx%d.png" % (n, n)), "wb").write(get(n))
        open(os.path.join(iconset, "icon_%dx%d@2x.png" % (n, n)), "wb").write(get(n * 2))
    print("icons written to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
