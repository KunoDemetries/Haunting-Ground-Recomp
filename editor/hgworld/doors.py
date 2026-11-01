"""Door connections: static table in the executable (SLUS_210.75) at 0x3DCFC0.

Evidence (docs/WORLD_EDITOR.md, "Door table"): the door-graph object's methods 0x21C610
(other side's room), 0x21C6A0 (other side's door number) and 0x21C550 (entry -> room,
entries < 0x190) read 16-byte entries: two sides {u16 room, u8 door, u8 0, u16 value}
then u32 flags. The table ends at room 0xFFFF (303 entries). Door number k of a room is
slot-7 portal k when k is below the room's portal count; higher numbers are exits
without a door model. `value` and `flags` are not yet interpreted.
"""
import struct

TABLE_ADDRESS = 0x3DCFC0
TABLE_LIMIT = 0x190


def elf_bytes(elf, address, size):
    if elf[:4] != b'\x7fELF' or elf[4] != 1 or elf[5] != 1:
        raise ValueError('not a little-endian ELF32 executable')
    phoff, = struct.unpack_from('<I', elf, 28)
    phentsize, phnum = struct.unpack_from('<HH', elf, 42)
    for i in range(phnum):
        kind, offset, vaddr, _paddr, filesz = struct.unpack_from('<5I', elf, phoff + i * phentsize)
        if kind == 1 and vaddr <= address and address + size <= vaddr + filesz:
            return elf[offset + address - vaddr:offset + address - vaddr + size]
    raise ValueError(f'0x{address:x}+0x{size:x} is not in a loaded segment')


def door_table(elf):
    links = []
    for i in range(TABLE_LIMIT):
        entry = elf_bytes(elf, TABLE_ADDRESS + i * 16, 16)
        if struct.unpack_from('<H', entry, 0)[0] == 0xFFFF:
            break
        ra, da, _za, va, rb, db, _zb, vb, flags = struct.unpack('<HBBHHBBHI', entry)
        links.append({'sides': ({'room': ra, 'door': da, 'value': va}, {'room': rb, 'door': db, 'value': vb}),
                      'flags': flags})
    return links


def destination(links, room, door):
    for link in links:
        for k in (0, 1):
            side = link['sides'][k]
            if side['room'] == room and side['door'] == door:
                return link['sides'][k ^ 1], link
    return None, None
