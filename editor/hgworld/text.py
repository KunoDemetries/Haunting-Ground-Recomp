"""Room message text (room PAC slot 10): read, display, and re-encode edits.

Evidence (docs/WORLD_EDITOR.md, "Room text"): u16 `last` (index of the last message),
then last + 1 u16 byte offsets from the slot start (the first offset is the header size).
Offsets need not be ascending and may repeat: unused entries share another message's
bytes, while each empty message has its own 0x00. A message runs from its offset to the
next larger distinct offset (the final one to its terminator); messages can contain 0x00
inside control-code arguments (e.g. the YES/NO prompt in room 0x02: 0x0E 00 00 ... 0x0F
FF FF "YES"). Control bytes seen: 0x01 line break, 0x02 page break, 0x0E/0x0F choice
codes with arguments, 0x00 end; others are shown as {XX}. Re-encoding keeps the original
sharing and order, so unedited text reproduces the slot byte for byte (checked on every
room); edited text must still fit in the slot's existing size.
"""
import re
import struct


def table(data):
    last = struct.unpack_from('<H', data, 0)[0]
    if 2 + (last + 1) * 2 > len(data):
        raise ValueError('room text table runs past its slot')
    return list(struct.unpack_from(f'<{last + 1}H', data, 2))


def messages(data):
    if not data or len(data) < 4:
        return []
    offsets = table(data)
    starts = sorted(set(offsets))
    result = []
    for start in offsets:
        if start >= len(data):
            raise ValueError(f'room text offset 0x{start:x} outside its slot')
        later = [s for s in starts if s > start]
        if later:
            end = later[0]
        else:
            end = len(data)
            while end > start + 1 and data[end - 1] == 0 and data[end - 2] == 0:
                end -= 1   # trailing slot padding after the final terminator
        result.append(bytes(data[start:end]))
    return result


def readable(raw):
    out = []
    for b in raw:
        if b == 1:
            out.append('\\n')
        elif b == 2:
            out.append('\\p')
        elif 32 <= b < 127 and b not in (ord('\\'), ord('{'), ord('}')):
            out.append(chr(b))
        else:
            out.append(f'{{{b:02X}}}')
    text = ''.join(out)
    return text[:-4] if text.endswith('{00}') else text   # the terminator is implied


def from_readable(text):
    """Inverse of readable(): \\n line break, \\p page break, {XX} any byte; adds the 0x00 end."""
    out = bytearray()
    for token in re.findall(r'\\n|\\p|\{[0-9A-Fa-f]{2}\}|.', text, flags=re.S):
        if token == '\\n':
            out.append(1)
        elif token == '\\p':
            out.append(2)
        elif token.startswith('{') and len(token) == 4:
            out.append(int(token[1:3], 16))
        else:
            if not 32 <= ord(token) < 127:
                raise ValueError(f'character {token!r} is not in the game\'s ASCII text set')
            out.append(ord(token))
    out.append(0)
    return bytes(out)


def encode(original, new_messages):
    """Rebuild slot 10 with new message bytes, keeping the original sharing and order."""
    offsets = table(original)
    if len(new_messages) != len(offsets):
        raise ValueError(f'expected {len(offsets)} messages, got {len(new_messages)}')
    header = 2 + 2 * len(offsets)
    group_text = {}
    for i, o in enumerate(offsets):
        group_text.setdefault(o, new_messages[i])
    blob = bytearray()
    position = {}
    for o in sorted(set(offsets)):
        position[o] = header + len(blob)
        blob += group_text[o]
    new_offsets = []
    for i, o in enumerate(offsets):
        if new_messages[i] == group_text[o]:
            new_offsets.append(position[o])
        else:   # an edit split a shared entry
            new_offsets.append(header + len(blob))
            blob += new_messages[i]
    out = struct.pack('<H', len(offsets) - 1) + struct.pack(f'<{len(offsets)}H', *new_offsets) + bytes(blob)
    if len(out) > len(original):
        raise ValueError(f'edited text is {len(out) - len(original)} byte(s) too long for this room '
                         f'({len(out)} needed, {len(original)} available); shorten it or other messages')
    return out + bytes(len(original) - len(out))


def spare(original):
    """Bytes of slack after the text, i.e. how much longer the room's messages may get."""
    offsets = table(original)
    groups = {}
    for o, raw in zip(offsets, messages(original)):
        groups.setdefault(o, raw)
    return len(original) - (2 + 2 * len(offsets) + sum(len(raw) for raw in groups.values()))
