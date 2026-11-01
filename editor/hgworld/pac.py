"""Room container (`ST_xxx\\ST_yyy.PAC`) parser.

Layout evidence (docs/WORLD_EDITOR.md, "Room PAC"): the game's loader 0x120720
formats `ST_%03X\\ST_%03X.PAC` from room id `r` (directory `r & ~7`) and reads the
whole file into one of two 0x2A0000-byte room buffers. All 168 room files on the
disc start with a 0x800-byte little-endian table of section offsets. Offsets are
relative to the file start, 0x20-aligned and nondecreasing; zero means the slot is
absent; an offset equal to the file size is a present but empty section. Slot
meanings are recorded only where game code or data proves them (SLOT_NOTES).
"""
from dataclasses import dataclass
import struct

HEADER_SIZE = 0x800
SLOT_COUNT = 17          # highest slot used by any disc room file is 16
ROOM_LIMIT = 0x110       # loader rejects room ids >= 0x110 (sltiu at 0x120740)

# Slot meanings with their evidence. Unproven slots are deliberately unnamed.
SLOT_NOTES = {
    0: 'navigation mesh (layout.py)',
    1: 'navigation border edges',
    2: 'trigger areas and trip lines (exact use unproven)',
    3: 'geometry (renders as the in-game room; mesh.py)',
    4: 'lights (probable; markers sit on the lamps)',
    5: 'fixed cameras (eye/fov, target pairs)',
    9: 'room texture bank (.TEX format; tex.py)',
    6: 'camera paths (layout.camera_paths)',
    7: 'portals = doors (layout.py)',
    8: 'door models, one mesh chain per portal (mesh.door_models)',
    10: 'room message text (text.py)',
    14: 'switchable navigation regions (events open/block floor)',
    11: 'texture-manager input, bank 0x15 (activation 0x11FFB0)',
    16: 'per-navigation-triangle flag bytes',
}


def room_path(room):
    if not 0 <= room < ROOM_LIMIT:
        raise ValueError(f'room id 0x{room:x} outside the loader range')
    return f'/ST_{room & ~7:03X}/ST_{room:03X}.PAC;1'


@dataclass(frozen=True)
class Section:
    slot: int
    offset: int
    size: int

    @property
    def present(self):
        return self.offset != 0


class RoomPac:
    def __init__(self, data):
        if len(data) < HEADER_SIZE:
            raise ValueError('room PAC shorter than its 0x800-byte header')
        self.data = bytes(data)
        offsets = struct.unpack_from(f'<{HEADER_SIZE // 4}I', self.data, 0)
        if any(offsets[SLOT_COUNT:]):
            raise ValueError('room PAC uses a slot beyond the 17 known slots')
        present = [o for o in offsets[:SLOT_COUNT] if o]
        if present != sorted(present):
            raise ValueError('room PAC section offsets are not nondecreasing')
        for o in present:
            if o % 0x20 or o < HEADER_SIZE or o > len(self.data):
                raise ValueError(f'room PAC section offset 0x{o:x} is misaligned or outside the file')
        ends = sorted(set(present) | {len(self.data)})
        self.sections = []
        for slot, o in enumerate(offsets[:SLOT_COUNT]):
            size = 0 if not o else min(e for e in ends if e > o) - o if o < len(self.data) else 0
            self.sections.append(Section(slot, o, size))

    def section(self, slot):
        s = self.sections[slot]
        return self.data[s.offset:s.offset + s.size] if s.present else None

    def aliases(self, slot):
        """Other slots whose offset equals this slot's (the file stores one copy)."""
        s = self.sections[slot]
        return [t.slot for t in self.sections if t.slot != slot and t.present and t.offset == s.offset]
