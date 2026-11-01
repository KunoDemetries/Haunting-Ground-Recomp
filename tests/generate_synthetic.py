"""Small hand-assembled programs; contains no game-derived instructions."""
import sys
from pathlib import Path
from hgtool.decode import decode
from hgtool.emit import emit

def r(rs, rt, rd, fn, sa=0):
    return rs << 21 | rt << 16 | rd << 11 | sa << 6 | fn

def imm(op, rs, rt, n):
    return op << 26 | rs << 21 | rt << 16 | (n & 65535)

# Independent entry PCs isolate semantic tests; run() budget is dispatch count.
words = {
    0x19000:imm(35,4,2,0),0x19004:r(0,2,2,2,8),
    0x19008:imm(12,2,2,1),0x1900c:imm(5,2,0,-4),0x19010:0,
    0x18000:r(31,0,0,8),0x18004:imm(9,0,6,9),
    0x1000: imm(4, 1, 2, 3),  # taken beq; slot changes condition register
    0x1004: imm(9, 0, 1, 9),
    0x1008: imm(9, 0, 3, 11),
    0x1010: imm(9, 0, 3, 22),
    0x2000: imm(20, 1, 2, 3), # not-taken beql annuls slot
    0x2004: imm(9, 0, 3, 99),
    0x3000: r(4, 0, 31, 9),  # jalr snapshots source before slot mutates it
    0x3004: imm(9, 0, 4, 0),
    0x4000: imm(9, 1, 2, 1), # 32-bit addition sign extends, preserves upper 64
    0x5000: imm(43, 4, 2, 0),
    0x5004: imm(35, 4, 3, 0),
    0x6000: imm(4, 0, 0, 1),
    0x6004: imm(35, 4, 3, 0), # delay-slot alignment fault must identify slot PC
    0x7000: r(4, 0, 4, 9),  # jalr rd==rs snapshots before writing link
    0x7004: 0,
    0x8000: imm(20, 1, 2, 3),
    0x8004: 0xffffffff, # unsupported delay slot is annulled on not taken
    0x9000: 28 << 26 | r(1, 2, 3, 40, 16),
    0x9004: r(3, 0, 0, 17),
    0x9008: 28 << 26 | r(3, 0, 0, 19),
    0x900c: imm(1, 3, 25, 5),
    0x9010: 17 << 26 | 4 << 21 | 3 << 16 | 7 << 11,
    0xa000: r(1,2,3,22),
    0xa004: r(0,2,4,63,31),
    0xa008: r(0,2,5,60,0),
    0xb000: imm(1,1,3,3),
    0xb004: imm(9,0,6,99),
    0xc000: r(1,2,3,10),
    0xc004: r(1,2,4,11),
    0xd000: imm(9,0,5,9),
    0xe000: 16<<26 | 4<<21 | 1<<16 | 30<<11,
    0xe004: 16<<26 | 2<<16 | 30<<11,
    0xe008: 0x42000018,
    0xe00c: imm(9,0,5,99), # ERET has no delay slot
    0xf000: r(1,2,0,27),
    0xf004: r(1,2,0,26),
    0xf008: 28<<26 | r(1,2,0,26),
    0xf00c: 28<<26 | r(1,2,0,27),
    0xf100: r(1,2,1,24),
    0xf104: 28<<26 | r(1,2,3,25),
    0xf200: 28<<26 | r(1,2,1,0),
    0xf204: 28<<26 | r(1,2,3,33),
    0xf300: 28<<26 | r(0,0,3,48),
    0xf400: imm(49,1,2,4),
    0xf404: imm(57,1,2,8),
    0xf500: 28<<26 | r(1,2,1,8,9),
    0xf504: 28<<26 | r(0,1,2,41,19),
    0xf508: 28<<26 | r(1,2,3,9,18),
    0xf600: imm(47,1,24,63),
    0xf700: 28<<26 | r(0,0,4,16),
    0xf704: 28<<26 | r(0,0,5,18),
    0xf708: 28<<26 | r(1,2,6,8,1),
    0xf724: 28<<26 | r(1,2,9,8,5),
    0xf728: 28<<26 | r(1,2,10,8,25),
    0xf72c: 28<<26 | r(1,2,11,8,21),
    0xf730: 28<<26 | r(1,2,12,8,17),
    0xf734: 28<<26 | r(1,2,13,40,20),
    0xf73c: 28<<26 | r(1,2,14,8,24),
    0xf740: 28<<26 | r(1,2,15,8,20),
    0xf744: 28<<26 | r(1,2,16,8,16),
    0xf750: 18<<26 | 0x1a<<21 | 9<<16 | 7<<11 | 0x1fd,
    0xf748: 18<<26 | 0x1a<<21 | 9<<16 | 7<<11 | 0x33c,
    0xf74c: 18<<26 | 0x1f<<21 | 10<<16 | 7<<11 | 0x33d,
    0xf70c: 28<<26 | r(1,2,7,40,24),
    0xf710: r(1,2,8,34),
    0xf714: 18<<26 | 1<<21 | 4<<16 | 28<<11,
    0xf718: 18<<26 | 5<<21 | 4<<16 | 27<<11,
    0xf71c: imm(62,6,16,3),
    0xf720: imm(54,6,17,3),
    0xf760: imm(51,1,0,0),
    0xf764: imm(51,1,1,0x100),
    0xf768: imm(51,1,17,-0x8000),
    0xf76c: imm(51,1,31,-1),
    0xf770: 0x0000000f,
    0xf774: imm(47,1,22,0),
    0xf800: 17<<26 | 16<<21 | 2<<16 | 1<<11 | 3<<6 | 1,
    0xf804: 17<<26 | 16<<21 | 3<<16 | 2<<11 | 7<<6 | 28,
    0xf808: 17<<26 | 16<<21 | 2<<16 | 1<<11 | 4<<6 | 2,
    0xf80c: 17<<26 | 16<<21 | 2<<16 | 1<<11 | 5<<6 | 3,
    0xf810: 17<<26 | 20<<21 | 0<<16 | 1<<11 | 6<<6 | 32,
    0xf814: 17<<26 | 16<<21 | 0<<16 | 1<<11 | 7<<6 | 6,
    0xf818: 17<<26 | 16<<21 | 0<<16 | 1<<11 | 8<<6 | 7,
    0xf81c: 17<<26 | 16<<21 | 0<<16 | 1<<11 | 9<<6 | 36,
    0xf820: 17<<26 | 16<<21 | 2<<16 | 1<<11 | 30,
    0xf824: 17<<26 | 16<<21 | 2<<16 | 1<<11 | 10<<6 | 29,
    0xf828: 17<<26 | 16<<21 | 2<<16 | 1<<11 | 50,
    0xf82c: 17<<26 | 8<<21 | 1<<16 | 1,
    0xf830: 0,
    0xf834: 0,
    0xf838: 17<<26 | 16<<21 | 4<<16 | 11<<6 | 4,
    0xf900: imm(8,1,2,-4),
    0xf904: imm(8,1,2,1),
    0xf908: 28<<26 | r(1,2,1,8,7),
    0xf90c: 28<<26 | r(1,2,2,40,7),
    0xf910: 28<<26 | r(1,2,1,8,27),
    0xf914: 28<<26 | r(1,2,1,8,4),
    0xf918: 28<<26 | r(1,2,1,40,27),
    0xf91c: 28<<26 | r(1,2,1,8,26),
    0xf920: 28<<26 | r(1,2,2,40,26),
    0xf924: 28<<26 | r(1,2,1,8,6),
    0xf928: 28<<26 | r(0,1,1,52,1),
    0xf92c: 28<<26 | r(0,1,1,54,15),
    0xf930: 28<<26 | r(0,1,1,55,15),
    0xe010: 16<<26 | 4<<21 | 1<<16 | 6<<11,
    0xe014: 16<<26 | 2<<16 | 6<<11,
    0xe018: 16<<26 | 2<<16 | 28<<11,
}
words.update({
    0x1b000: imm(35,4,0,0), # Load to zero still faults on invalid access.
    0x1b004: r(4,0,0,9),0x1b008: imm(9,0,5,7), # JALR with discarded link.
    0x1b010: imm(4,0,0,2),0x1b014: imm(35,4,0,0),
    0x1b020: imm(20,1,0,2),0x1b024: imm(35,4,0,0),
    0x1a000: imm(9,1,1,-1),0x1a004: imm(7,1,0,-2),0x1a008: imm(9,2,2,1),
    0x1a00c: imm(20,1,0,2),0x1a010: imm(9,3,3,1),
    0x1a014: (3<<26)|(0x1a000>>2),0x1a018: imm(9,4,4,1),
    0x1a01c: (2<<26)|(0x1a000>>2),0x1a020: 0,
    0x10ffc: imm(4,1,2,0x400), 0x11000: imm(9,3,3,5),
    0x12000: imm(9,4,4,7),
    0x12ffc: imm(9,1,1,1), 0x13000: imm(9,2,2,2),
    0x14ffc: imm(20,1,2,0x400), 0x15000: 13,
    0x17000: 0,0x17004: imm(36,4,3,0),0x17008: imm(4,3,0,-3),
    0x1700c: 0,0x17010: imm(9,5,5,1),
})
vu_words = {
    # Independent arithmetic sequence. The final VF destination aliases the
    # broadcast source in a JR delay slot, after three ACC writes.
    0x21000: 18<<26 | 31<<21 | 2<<16 | 1<<11 | 0x1bc,
    0x21004: 18<<26 | 31<<21 | 2<<16 | 1<<11 | 0x0bd,
    0x21008: 18<<26 | 31<<21 | 2<<16 | 1<<11 | 0x0be,
    0x2100c: r(4,0,0,8),
    0x21010: 18<<26 | 31<<21 | 2<<16 | 1<<11 | 2<<6 | 0x0b,
}
assert not words.keys() & vu_words.keys()
words.update(vu_words)
packed_words = {
    # Independent word shuffle with both source aliases and a JR target changed
    # in its delay slot. This fixture contains no original game instructions.
    0x22000: 28<<26 | r(1,2,1,8,18),
    0x22004: 28<<26 | r(1,2,2,40,18),
    0x22008: r(4,0,0,8),
    0x2200c: 28<<26 | r(0,2,4,8,18),
}
assert not words.keys() & packed_words.keys()
words.update(packed_words)
conversion_words = {
    0x23000:18<<26 | 31<<21 | 2<<16 | 1<<11 | 0x17d,
    0x23004:r(4,0,0,8),
    0x23008:18<<26 | 31<<21 | 2<<16 | 2<<11 | 0x13d,
}
assert not words.keys() & conversion_words.keys()
words.update(conversion_words)
for register in range(32):
    pc=0x20000+register*0x20
    words.update({
        pc:r(register,31,register,45), # DADDU, including both operands aliasing.
        pc+4:imm(9,register,register,-1),
        pc+8:imm(13,0,register,0x8001), # Reads architectural zero, not its storage.
        pc+12:r(0,register,register,0,3),
        pc+16:r(register,register,register,10), # MOVZ and MOVN with all operands aliasing.
        pc+20:r(register,register,register,11),
    })
Path(sys.argv[1]).write_text(emit({pc: decode(pc, w) for pc, w in words.items()}, 'synthetic',
    [{'address':0xd000,'size':4}]), encoding='utf-8')
