"""Texture bank format (`*.TEX` files and room PAC slot 9).

Evidence (docs/WORLD_EDITOR.md, "Texture bank"): every disc `.TEX` file and room
slot 9 begin with u32 `count`, 12 zero bytes, then `count` 16-byte entries
`{u32 psm, u16 width, u16 height, u32 flags, u32 offset}` where `offset` is
relative to the entry itself. The image data at that offset is `width*height`
indices (8-bit for PSM 0x13 PSMT8, 4-bit for 0x14 PSMT4) followed by a 256-entry
RGBA palette whose alpha uses the PS2 0..0x80 range. The file sizes match this
layout exactly (e.g. O_FIN/FIN_000.TEX: 0x50 + 4 * 0x10400 = 0x41050). The second
u32 is `(palette_qwords << 16) | index_qwords` in 16-byte units: all 1,811 entries on
the disc agree (0x00401000 = 64 KiB indices + 1 KiB palette, 0x00400200 for
128x128 PSMT4, 0x00404000 for 512x512 PSMT8). Palette order is GS CSM1; images
decoded with that order are consistently smoother than the linear order.
"""
from dataclasses import dataclass
import struct
import zlib

PSMT8 = 0x13
PSMT4 = 0x14
PALETTE_BYTES = 256 * 4


@dataclass(frozen=True)
class Texture:
    index: int
    psm: int
    width: int
    height: int
    flags: int
    data_offset: int

    @property
    def index_bytes(self):
        bits = {PSMT8: 8, PSMT4: 4}[self.psm]
        return self.width * self.height * bits // 8


def parse(data):
    if len(data) < 16:
        raise ValueError('texture bank shorter than its header')
    count = struct.unpack_from('<I', data, 0)[0]
    if not 0 < count <= 4096 or 16 + count * 16 > len(data):
        raise ValueError(f'implausible texture count {count}')
    textures = []
    for i in range(count):
        entry = 16 + i * 16
        psm, width, height, flags, offset = struct.unpack_from('<IHHII', data, entry)
        if psm not in (PSMT8, PSMT4):
            raise ValueError(f'texture {i}: unsupported pixel format 0x{psm:x}')
        texture = Texture(i, psm, width, height, flags, entry + offset)
        if flags != (PALETTE_BYTES // 16) << 16 | texture.index_bytes // 16:
            raise ValueError(f'texture {i}: size word 0x{flags:08x} disagrees with {width}x{height}')
        if texture.data_offset + texture.index_bytes + PALETTE_BYTES > len(data):
            raise ValueError(f'texture {i}: data extends past the bank')
        textures.append(texture)
    return textures


def palette(data, texture, swizzled=True):
    """RGBA palette with alpha expanded from 0..0x80 to 0..0xff.

    PSMT8 palettes are stored in the GS CSM1 order, where each 32-entry block
    swaps entries 8..15 with 16..23 (GS User's Manual, CLUT storage mode CSM1).
    """
    base = texture.data_offset + texture.index_bytes
    raw = [data[base + 4 * n:base + 4 * n + 4] for n in range(256)]
    if swizzled and texture.psm == PSMT8:
        raw = [raw[(n & ~0x18) | ((n & 8) << 1) | ((n & 16) >> 1)] for n in range(256)]
    return [(r, g, b, min(255, a * 255 // 0x80)) for r, g, b, a in raw]


def rgba(data, texture, swizzled=True):
    colours = palette(data, texture, swizzled)
    start = texture.data_offset
    if texture.psm == PSMT8:
        indices = data[start:start + texture.index_bytes]
    else:
        packed = data[start:start + texture.index_bytes]
        indices = bytes(v for b in packed for v in (b & 15, b >> 4))
    return [colours[i] for i in indices]


def png(width, height, pixels):
    rows = b''.join(b'\0' + bytes(c for p in pixels[y * width:(y + 1) * width] for c in p)
                    for y in range(height))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))
