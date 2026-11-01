"""Room layout slots: navigation (0, 1, 16), shapes (2), lights (4), cameras (5), portals (7).

Reference parsers mirroring editor/src/room.hpp; the evidence for each layout is in
docs/WORLD_EDITOR.md ("Room layout slots"). Names marked probable are unproven.
"""
import struct


def navigation(s0, s1, s16):
    triangles, edges = [], []
    if s0:
        count = struct.unpack_from('<I', s0, 0)[0]
        if 16 + count * 0x50 > len(s0):
            raise ValueError('navigation mesh runs past its slot')
        for i in range(count):
            o = 16 + i * 0x50
            triangles.append({
                'v': [struct.unpack_from('<3f', s0, o + 16 * k) for k in range(3)],
                'neighbour': struct.unpack_from('<3i', s0, o + 48),
                'cost': struct.unpack_from('<3f', s0, o + 64),
                'attribute': struct.unpack_from('<I', s0, o + 76)[0],
                # Some rooms' slot 16 is shorter than the triangle count; the game reads past it.
                'flags': s16[i] if s16 and i < len(s16) else 0,
            })
    if s1:
        count = struct.unpack_from('<I', s1, 0)[0]
        if 16 + count * 16 > len(s1):
            raise ValueError('navigation edge list runs past its slot')
        edges = [struct.unpack_from('<4I', s1, 16 + i * 16) for i in range(count)]
    return triangles, edges


def _table(data, unit):
    end, i, table = len(data), 0, []
    while i * 4 < end:
        value = struct.unpack_from('<I', data, i * 4)[0] * unit
        table.append(value)
        if value:
            end = min(end, value)
        i += 1
    return table


def shapes(data):
    if not data:
        return []
    table = _table(data, 4)
    starts = sorted(o for o in table if o)
    result = []
    for index, o in enumerate(table):
        if not o:
            continue
        stop = min([s for s in starts if s > o] + [len(data)])
        if o % 16 or stop < o + 16 or (stop - o) % 16:
            raise ValueError('slot 2 record misaligned')
        kind, _, value, _ = struct.unpack_from('<IIfI', data, o)
        points = [struct.unpack_from('<3f', data, p) for p in range(o + 16, stop, 16)]
        result.append({'index': index, 'kind': kind, 'value': value, 'points': points})
    return result


def lights(data):
    if not data:
        return (0.0, 0.0, 0.0), []
    count = struct.unpack_from('<I', data, 0)[0]
    if 16 + count * 0x30 > len(data):
        raise ValueError('light list runs past its slot')
    ambient = struct.unpack_from('<3f', data, 4)
    result = []
    for i in range(count):
        o = 16 + i * 0x30
        result.append({'position': struct.unpack_from('<3f', data, o),
                       'colour': struct.unpack_from('<3f', data, o + 16),
                       'intensity': struct.unpack_from('<f', data, o + 28)[0],
                       'range': struct.unpack_from('<f', data, o + 32)[0]})
    return ambient, result


def cameras(data):
    result = []
    for o in range(0, len(data or b'') - 31, 32):
        if struct.unpack_from('<I', data, o)[0] == 0xFFFFFFFF:
            break
        result.append({'eye': struct.unpack_from('<3f', data, o), 'fov': struct.unpack_from('<f', data, o + 12)[0],
                       'target': struct.unpack_from('<3f', data, o + 16)})
    return result


def camera_paths(data):
    """Slot 6: {count, 0, count x (channels, keys)} padded to 16, then 32-byte keys
    (time, value, in dt, in dvalue, out dt, out dvalue, u32 mode, u32) per channel."""
    if not data:
        return []
    count = struct.unpack_from('<I', data, 0)[0]
    pairs = [struct.unpack_from('<2I', data, 8 + 8 * i) for i in range(count)]
    o = (8 + 8 * count + 15) // 16 * 16
    result = []
    for channels, keys in pairs:
        if o + channels * keys * 32 > len(data):
            raise ValueError('camera path runs past its slot')
        chans = []
        for _ in range(channels):
            chans.append([struct.unpack_from('<6fI', data, o + 32 * k) for k in range(keys)])
            o += keys * 32
        result.append(chans)
    return result


def portals(data):
    if not data:
        return []
    result = []
    for o in (o for o in _table(data, 1) if o):
        count = struct.unpack_from('<I', data, o + 40)[0]
        if o + 44 + count * 4 > len(data):
            raise ValueError('slot 7 record runs past its slot')
        result.append({'triangle': struct.unpack_from('<I', data, o)[0],
                       'a': struct.unpack_from('<3f', data, o + 4), 'b': struct.unpack_from('<3f', data, o + 16),
                       'angle': struct.unpack_from('<f', data, o + 32)[0],
                       'area': list(struct.unpack_from(f'<{count}I', data, o + 44))})
    return result
