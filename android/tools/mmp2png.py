#!/usr/bin/env python3
"""
mmp2png.py -- decode a Silent Storm texture ("MMP" container) to PNG.

Layout, verified against the Textures table in game.db:

    0  'MMP\0'
    4  uint32  version (1)
    8  uint32  average colour, BGRA -- the same value the DB stores, so it
               doubles as a checksum for the decode
   12  uint32  width
   16  uint32  height
   20  uint32  pixel format (5 = DXT1)
   24  DXT1 blocks, top mip first, then the mip chain

Only the top mip is written.  Pure stdlib: no Pillow, no external decoder.
"""
import struct, sys, zlib

def rgb565(c):
    return (((c >> 11) & 31) * 255 // 31,
            ((c >> 5) & 63) * 255 // 63,
            (c & 31) * 255 // 31)

def decode_dxt1(data, w, h):
    px = bytearray(w * h * 4)
    bw, bh = (w + 3) // 4, (h + 3) // 4
    off = 0
    for by in range(bh):
        for bx in range(bw):
            if off + 8 > len(data):
                return px
            c0, c1, bits = struct.unpack_from('<HHI', data, off)
            off += 8
            r0, g0, b0 = rgb565(c0)
            r1, g1, b1 = rgb565(c1)
            if c0 > c1:
                pal = [(r0, g0, b0, 255), (r1, g1, b1, 255),
                       ((2*r0+r1)//3, (2*g0+g1)//3, (2*b0+b1)//3, 255),
                       ((r0+2*r1)//3, (g0+2*g1)//3, (b0+2*b1)//3, 255)]
            else:
                pal = [(r0, g0, b0, 255), (r1, g1, b1, 255),
                       ((r0+r1)//2, (g0+g1)//2, (b0+b1)//2, 255), (0, 0, 0, 0)]
            for py in range(4):
                for pxi in range(4):
                    x, y = bx*4 + pxi, by*4 + py
                    if x >= w or y >= h:
                        continue
                    r, g, b, a = pal[(bits >> (2*(py*4 + pxi))) & 3]
                    i = (y*w + x) * 4
                    px[i:i+4] = bytes((r, g, b, a))
    return px

def decode_565(data, w, h):
    px = bytearray(w * h * 4)
    for i in range(min(w * h, len(data) // 2)):
        r, g, b = rgb565(struct.unpack_from('<H', data, i * 2)[0])
        px[i*4:i*4+4] = bytes((r, g, b, 255))
    return px

def decode_8888(data, w, h):
    """D3DFMT_A8R8G8B8: little-endian, so the bytes run B, G, R, A."""
    px = bytearray(w * h * 4)
    for i in range(min(w * h, len(data) // 4)):
        b, g, r, a = data[i*4:i*4+4]
        px[i*4:i*4+4] = bytes((r, g, b, a))
    return px

def mip_chain(w, h, level_bytes):
    """Total bytes of a full mip chain down to 1x1."""
    total = 0
    while True:
        total += level_bytes(w, h)
        if w == 1 and h == 1:
            return total
        w = max(1, w // 2); h = max(1, h // 2)

CODECS = (
    ('dxt1', lambda w, h: ((w + 3) // 4) * ((h + 3) // 4) * 8, decode_dxt1),
    ('565',  lambda w, h: w * h * 2,                           decode_565),
    ('8888', lambda w, h: w * h * 4,                           decode_8888),
)

def pick_codec(nBytes, w, h):
    """The format code in the header does not select the compression -- 5, 6
    and 7 all turn up on textures game.db calls dxt1 -- but the file length
    does, because the three layouts differ by a factor of four."""
    best = None
    for name, level, decode in CODECS:
        d = abs(mip_chain(w, h, level) - nBytes)
        if best is None or d < best[0]:
            best = (d, name, decode)
    return best[1], best[2]

def write_png(path, w, h, rgba):
    raw = b''.join(b'\x00' + bytes(rgba[y*w*4:(y+1)*w*4]) for y in range(h))
    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))

def convert(src, dst):
    blob = open(src, 'rb').read()
    if blob[:4] != b'MMP\0':
        raise SystemExit(f'{src}: not an MMP texture')
    _ver, avg, w, h, fmt = struct.unpack_from('<IIIII', blob, 4)
    name, decode = pick_codec(len(blob) - 24, w, h)
    rgba = decode(blob[24:], w, h)
    write_png(dst, w, h, rgba)
    n = w * h
    ar = sum(rgba[0::4]) // n; ag = sum(rgba[1::4]) // n; ab = sum(rgba[2::4]) // n
    er, eg, eb = (avg >> 16) & 255, (avg >> 8) & 255, avg & 255
    d = max(abs(ar-er), abs(ag-eg), abs(ab-eb))
    #  A texture this unpack does not actually ship is a file of repeating
    #  filler that decodes to nothing; the database's average colour is 0 too,
    #  so the check below would pass it.  Say so instead.
    blank = not any(rgba)
    print(f'  {dst}  {w}x{h} {name}  avg decoded RGB({ar},{ag},{ab}) vs DB({er},{eg},{eb})  '
          f'{"BLANK - this texture is filler, not image data" if blank else "OK" if d <= 16 else "MISMATCH %d" % d}')
    return not blank

if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: mmp2png.py <texture-file> <out.png>')
    convert(sys.argv[1], sys.argv[2])
