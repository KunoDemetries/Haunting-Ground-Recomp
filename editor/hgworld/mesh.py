"""Room geometry (room PAC slot 3).

Evidence (docs/WORLD_EDITOR.md, "Room geometry"): the room activation routine
0x11FFB0 hands slot 3 to 0x25D370. Slot 3 begins with eight u32 block offsets
(relative to the slot; zero = absent). Block 0 is a run of mesh records closed by
a 16-byte 0xFFFFFFFF terminator; in room 0x21 its 522 records end exactly there.
Each record is:

  u32 vertex_count, u32 texture, u32 flags, u32 0
  4x4 float matrix, rows (x, y, z, translation); last row (0, 0, 0, 1)
  vertex_count (u, v) float pairs, padded to 16 bytes
  vertex_count RGBA bytes (alpha 0..0x80), padded to 16 bytes
  vertex_count (x, y, z float, u32 strip) entries

`strip` is 0x8000 for the first two vertices of a triangle strip and 0x7FFF for
vertices that complete a triangle (the GS drawing-kick convention). The fourth
header word is 0 except in room 0x0D (0xFFFFFFFF); texture 0xFFFFFFFF means none.
When block 0 is the only block, zero padding may follow its terminator. `texture` is
1..10 in room 0x21, whose slot-9 bank holds 11 textures; treating it as the bank
index is a working hypothesis until checked against rendered frames. The meaning
of `flags` and of blocks 1..7 is not yet known.
"""
from dataclasses import dataclass
import struct

TERMINATOR = b'\xff' * 16
STRIP_RESTART = 0x8000
STRIP_DRAW = 0x7FFF


def _padded(size):
    return (size + 15) // 16 * 16


@dataclass
class Mesh:
    offset: int
    texture: int
    flags: int
    matrix: list
    uvs: list
    colours: list
    positions: list
    strip: list

    def world_positions(self):
        m = self.matrix
        return [(m[0] * x + m[1] * y + m[2] * z + m[3],
                 m[4] * x + m[5] * y + m[6] * z + m[7],
                 m[8] * x + m[9] * y + m[10] * z + m[11]) for x, y, z in self.positions]

    def triangles(self):
        """Vertex index triples with consistent winding (strip parity flips)."""
        result = []
        run = 0                     # position of this vertex within its strip
        for i, flag in enumerate(self.strip):
            if flag == STRIP_RESTART:
                restarting = i == 0 or self.strip[i - 1] == STRIP_DRAW
                run = 1 if restarting else run + 1
                continue
            if flag != STRIP_DRAW:
                raise ValueError(f'mesh 0x{self.offset:x}: unknown strip flag 0x{flag:x}')
            if run < 2:
                raise ValueError(f'mesh 0x{self.offset:x}: strip draws before two vertices')
            run += 1
            result.append((i - 2, i - 1, i) if run % 2 else (i - 1, i - 2, i))
        return result


def _record(data, offset, count, texture, flags):
    base = offset + 16
    matrix = list(struct.unpack_from('<16f', data, base))
    base += 64
    uvs = [struct.unpack_from('<2f', data, base + 8 * i) for i in range(count)]
    base += _padded(count * 8)
    colours = [tuple(data[base + 4 * i:base + 4 * i + 4]) for i in range(count)]
    base += _padded(count * 4)
    positions, strip = [], []
    for i in range(count):
        x, y, z, s = struct.unpack_from('<3fI', data, base + 16 * i)
        positions.append((x, y, z))
        strip.append(s)
    mesh = Mesh(offset, texture, flags, matrix, uvs, colours, positions, strip)
    mesh.end = base + count * 16
    return mesh


def block_offsets(slot3):
    if len(slot3) < 32:
        raise ValueError('room geometry shorter than its block table')
    return struct.unpack_from('<8I', slot3, 0)


def parse_block0(slot3):
    return parse_block(slot3, 0)


def parse_block(slot3, index):
    blocks = block_offsets(slot3)
    start = blocks[index]
    if not start:
        return []
    end = min([b for b in blocks if b > start] + [len(slot3)])
    meshes = []
    offset = start
    while True:
        if offset + 16 > end:
            raise ValueError('room geometry block 0 has no terminator')
        if slot3[offset:offset + 16] == TERMINATOR:
            break
        count, texture, flags, _extra = struct.unpack_from('<4I', slot3, offset)
        if not 0 < count < 0x10000:
            raise ValueError(f'mesh record at 0x{offset:x} has an implausible header')
        mesh = _record(slot3, offset, count, texture, flags)
        if mesh.end > end:
            raise ValueError(f'mesh record at 0x{offset:x} runs past block 0')
        meshes.append(mesh)
        offset = mesh.end
    last_block = not any(b > start for b in blocks)
    if offset + 16 != end and not (last_block and not any(slot3[offset + 16:end])):
        raise ValueError(f'room geometry block 0 ends at 0x{offset + 16:x}, table says 0x{end:x}')
    return meshes


def obj_text(meshes, mtl_name=None):
    lines = [f'mtllib {mtl_name}'] if mtl_name else []
    base = 1
    for n, mesh in enumerate(meshes):
        lines.append(f'o mesh_{n:04}_0x{mesh.offset:x}')
        if mtl_name:
            lines.append(f'usemtl tex_{mesh.texture:02}')
        for (x, y, z), (r, g, b, _a) in zip(mesh.world_positions(), mesh.colours):
            # Game space is Y-up (slot-5 cameras render upright); vertex colours are a
            # common informal OBJ extension, scaled from the PS2 0..0x80 range.
            lines.append(f'v {x:.5f} {y:.5f} {z:.5f} {r / 128:.4f} {g / 128:.4f} {b / 128:.4f}')
        for u, v in mesh.uvs:
            lines.append(f'vt {u:.6f} {1 - v:.6f}')
        for a, b, c in mesh.triangles():
            a, b, c = a + base, b + base, c + base
            lines.append(f'f {a}/{a} {b}/{b} {c}/{c}')
        base += len(mesh.positions)
    return '\n'.join(lines) + '\n'


def door_models(slot8):
    """Slot 8: one terminator-closed mesh chain per slot-7 portal, in door-local space.

    The editor places door k at portal k's point A rotated about Y by its angle (the only
    one of the A/B x +/-angle conventions that seats the door in its frame).
    """
    doors = []
    if not slot8 or len(slot8) < 32:
        return doors
    offset = struct.unpack_from('<I', slot8, 0)[0]
    while offset + 16 <= len(slot8):
        head = slot8[offset:offset + 16]
        if head == TERMINATOR or not any(head):
            break
        meshes = []
        while slot8[offset:offset + 16] != TERMINATOR:
            count, texture, flags, _extra = struct.unpack_from('<4I', slot8, offset)
            if not 0 < count < 0x10000:
                raise ValueError(f'door mesh at 0x{offset:x} has an implausible header')
            meshes.append(_record(slot8, offset, count, texture, flags))
            offset = meshes[-1].end
        doors.append(meshes)
        offset += 16
    return doors
