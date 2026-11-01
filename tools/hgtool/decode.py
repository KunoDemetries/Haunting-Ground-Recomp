from dataclasses import dataclass


@dataclass(frozen=True)
class Instruction:
    pc: int
    word: int
    name: str
    rs: int
    rt: int
    rd: int
    sa: int
    immediate: int
    target: int | None = None

    @property
    def control(self):
        return self.name in {'eret', 'j', 'jal', 'jr', 'jalr', 'beq', 'bne', 'blez', 'bgtz',
                             'beql', 'bnel', 'blezl', 'bgtzl', 'bltz', 'bgez', 'bltzl', 'bgezl',
                             'bc1f', 'bc1t', 'bc1fl', 'bc1tl'}

    @property
    def likely(self):
        return self.name in {'beql', 'bnel', 'blezl', 'bgtzl', 'bltzl', 'bgezl', 'bc1fl', 'bc1tl'}


def decode(pc, word):
    op, rs, rt, rd, sa, fn = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31, word >> 6 & 31, word & 63
    imm = word & 65535
    signed = imm - 65536 if imm & 32768 else imm
    target = None
    name = 'unsupported'
    if op == 0:
        name = {0:'sll', 2:'srl', 3:'sra', 4:'sllv', 6:'srlv', 7:'srav', 8:'jr', 9:'jalr', 10:'movz', 11:'movn', 12:'syscall',
                13:'break', 15:'sync', 16:'mfhi', 17:'mthi', 18:'mflo', 19:'mtlo', 33:'addu', 35:'subu',
                36:'and', 37:'or', 38:'xor', 39:'nor', 42:'slt', 43:'sltu',
                20:'dsllv', 22:'dsrlv', 23:'dsrav', 24:'mult', 25:'multu', 26:'div', 27:'divu', 34:'sub', 45:'daddu', 47:'dsubu',
                56:'dsll', 58:'dsrl', 59:'dsra', 60:'dsll32', 62:'dsrl32', 63:'dsra32'}.get(fn, name)
    elif op in (2, 3):
        name = 'j' if op == 2 else 'jal'
        target = ((pc + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
    elif op in (4, 5, 6, 7, 20, 21, 22, 23):
        name = {4:'beq', 5:'bne', 6:'blez', 7:'bgtz', 20:'beql', 21:'bnel', 22:'blezl', 23:'bgtzl'}[op]
        target = (pc + 4 + signed * 4) & 0xffffffff
    elif op == 1 and rt in (0, 1, 2, 3):
        name = {0:'bltz', 1:'bgez', 2:'bltzl', 3:'bgezl'}[rt]
        target = (pc + 4 + signed * 4) & 0xffffffff
    elif op == 28:
        if fn==48 and rs==rt==sa==0:
            name='pmfhl.lw'
        elif fn in (0,1,32,33) and sa==0:
            name={0:'madd',1:'maddu',32:'madd1',33:'maddu1'}[fn]
        elif fn in (24,25) and sa==0:
            name = 'mult1' if fn==24 else 'multu1'
        elif fn in (26,27) and rd==sa==0:
            name = 'div1' if fn==26 else 'divu1'
        elif fn == 40 and sa in (16,20,24):
            name = {16:'padduw',20:'padduh',24:'paddub'}[sa]
        elif fn in (9,41) and sa in (18,19):
            name={(9,18):'pand',(41,18):'por',(9,19):'pxor',(41,19):'pnor'}[fn,sa]
        elif fn in (8,40) and sa==7:
            name='pmaxh' if fn==8 else 'pminh'
        elif fn==8 and sa==27:
            name='ppacb'
        elif fn==8 and sa==4:
            name='paddh'
        elif fn==8 and sa==6:
            name='pcgth'
        elif fn in (8,40) and sa==26:
            name='pextlb' if fn==8 else 'pextub'
        elif fn in (8,40) and sa==18:
            name='pextlw' if fn==8 else 'pextuw'
        elif fn==40 and sa==27:
            name='qfsrv'
        elif fn in (52,54,55) and rs==0 and sa<16:
            name={52:'psllh',54:'psrlh',55:'psrah'}[fn]
        elif fn==8 and sa in (1,5,9):
            name={1:'psubw',5:'psubh',9:'psubb'}[sa]
        elif fn==8 and sa in (17,21,25):
            name={17:'psubsw',21:'psubsh',25:'psubsb'}[sa]
        elif fn==8 and sa in (16,20,24):
            name={16:'paddsw',20:'paddsh',24:'paddsb'}[sa]
        elif fn == 41 and sa == 27 and rs == 0:
            name = 'pcpyh'
        elif sa == 14 and fn in (9,41):
            name = 'pcpyld' if fn == 9 else 'pcpyud'
        elif fn in (16, 18) and rs == rt == sa == 0:
            name = 'mfhi1' if fn == 16 else 'mflo1'
        elif fn in (17, 19) and rt == rd == sa == 0:
            name = 'mthi1' if fn == 17 else 'mtlo1'
    elif op == 1 and rt in (24, 25):
        name = 'mtsab' if rt == 24 else 'mtsah'
    elif op == 16:
        if word == 0x42000018:
            name = 'eret'
        elif word in (0x42000038,0x42000039):
            name = 'ei' if word==0x42000038 else 'di'
        elif rs in (0,4) and rd in (6,12,14,28,30) and (word&2047)==0:
            name = ('mfc0_' if rs==0 else 'mtc0_') + {6:'wired',12:'status',14:'epc',28:'tag_lo',30:'error_epc'}[rd]
    elif op == 47:
        if rt == 24:
            name = 'cache_dhwbin'
        elif rt == 16:
            name = 'cache_dxltg'
        elif rt == 20:
            name = 'cache_dxwbin'
        elif rt == 22:
            name = 'cache_dxin'
    elif op == 62:
        name = 'sqc2'
    elif op == 17:
        if rs==8 and rt in (0,1,2,3):
            name={0:'bc1f',1:'bc1t',2:'bc1fl',3:'bc1tl'}[rt]
            target=(pc+4+signed*4)&0xffffffff
        elif rs in (0, 4) and (word & 2047) == 0:
            name = 'mfc1' if rs == 0 else 'mtc1'
        elif rs in (2, 6) and rd == 31 and (word & 2047) == 0:
            name = 'cfc1' if rs == 2 else 'ctc1'
        elif rs == 16 and fn == 24 and sa == 0:
            name = 'adda.s'
        elif rs == 16 and fn == 0:
            name = 'add.s'
        elif rs == 16 and fn == 1:
            name = 'sub.s'
        elif rs == 16 and fn == 2:
            name = 'mul.s'
        elif rs == 16 and fn == 3:
            name = 'div.s'
        elif rs == 16 and fn == 4 and rd == 0:
            # SQRT.S reserves fs as zero.  Keep that check here so another
            # COP1 encoding cannot be mistaken for square root.
            name = 'sqrt.s'
        elif rs == 16 and fn == 6 and rt == 0:
            name = 'mov.s'
        elif rs == 16 and fn == 7 and rt == 0:
            name = 'neg.s'
        elif rs == 16 and fn in (50,52,54) and sa == 0:
            name={50:'c.eq.s',52:'c.olt.s',54:'c.le.s'}[fn]
        elif rs == 16 and fn == 28:
            name = 'madd.s'
        elif rs == 16 and fn == 26 and sa == 0:
            name = 'mula.s'
        elif rs == 16 and fn == 29:
            name = 'msub.s'
        elif rs == 16 and fn == 30 and sa == 0:
            name = 'madda.s'
        elif rs == 16 and fn == 31 and sa == 0:
            name = 'msuba.s'
        elif rs == 20 and fn == 32 and rt == 0:
            name = 'cvt.s.w'
        elif rs == 16 and fn == 36 and rt == 0:
            name = 'cvt.w.s'
    elif op == 18:
        # COP2 register transfers have an all-zero low field.  The VU macro
        # instruction space is handled separately once its execution state is
        # modeled; never decode arbitrary macro words as transfers.
        if rs == 1 and (word & 2046) == 0:
            name = 'qmfc2'
        elif rs == 5 and (word & 2046) == 0:
            name = 'qmtc2'
        elif rs in (2,6) and (word & 2046) == 0:
            name = 'cfc2' if rs == 2 else 'ctc2'
        # COP2 macro-mode lower operations.  The four-bit component mask is
        # in bits 24..21 (the low four bits of rs); VMOVE has fixed low bits.
        # Keep this exact match separate from register transfers so unknown
        # macro encodings remain explicit unsupported translations.
        elif rs & 16 and (word & 0x7fc) in (0x13c, 0x17c):
            # Sony VU manual pp253-256/264-267: fixed-point conversions.
            prefix = 'vitof' if (word & 0x7fc) == 0x13c else 'vftoi'
            name = prefix + str((0, 4, 12, 15)[word & 3])
        elif rs & 16 and (word & 2047) in (0x1fd, 0x33c, 0x33d):
            name = {0x1fd: 'vabs', 0x33c: 'vmove', 0x33d: 'vmr32'}[word & 2047]
        # Sony VU manual pp273/277/303: ACC forms have nine fixed low
        # bits above bc; the VF destination form has a four-bit function.
        elif rs & 16 and (word & 0x7fc) == 0x1bc:
            name = 'vmulabc'
        elif rs & 16 and (word & 0x7fc) == 0x0bc:
            name = 'vmaddabc'
        elif rs & 16 and 0x08 <= fn <= 0x0b:
            name = 'vmaddbc'
        elif rs & 16 and 0x04 <= fn <= 0x07:
            name = 'vsubbc'
        elif rs & 16 and fn == 0x2a:
            name = 'vmul'
        elif rs & 16 and 0x18 <= fn <= 0x1b:
            name = 'vmulbc'
        elif rs & 16 and fn <= 3:
            name = 'vaddbc'
        elif rs & 16 and fn == 0x20 and rt == 0:
            name = 'vaddq'
        elif rs & 16 and fn == 0x1c and rt == 0:
            name = 'vmulq'
        elif rs & 16 and fn == 0x2c:
            name = 'vsub'
        elif rs & 16 and fn == 0x28:
            name = 'vadd'
        elif rs == 30 and (word & 2047) == 0x2fe:
            name = 'vopmula'
        elif rs == 30 and fn == 0x2e:
            name = 'vopmsub'
        elif rs & 16 and (word & 2047) == 0x3bc:
            name = 'vdiv'
        elif rs & 16 and (word & 2047) == 0x3bd and rd == 0 and (rs & 3) == 0:
            name = 'vsqrt'
        elif word == 0x4a0003bf:
            name = 'vwaitq'
        elif word == 0x4a0002ff:
            name = 'vnop'
    else:
        name = {8:'addi', 9:'addiu', 10:'slti', 11:'sltiu', 12:'andi', 13:'ori', 14:'xori',
                15:'lui', 25:'daddiu', 26:'ldl', 27:'ldr', 30:'lq', 31:'sq', 32:'lb', 33:'lh', 34:'lwl', 35:'lw', 36:'lbu',
                37:'lhu', 38:'lwr', 39:'lwu', 40:'sb', 41:'sh', 42:'swl', 43:'sw', 44:'sdl', 45:'sdr', 46:'swr', 49:'lwc1', 51:'pref', 54:'lqc2', 55:'ld', 57:'swc1', 63:'sd'}.get(op, name)
    # Reserved fields must not turn arbitrary data into plausible instructions.
    if op == 0:
        # Original movie-copy mask word executes as DSRA32. Exact encoding
        # independently memory-probed; see docs/ORACLE.md. Keep other reserved
        # encodings rejected rather than relaxing discovery globally.
        if name in ('sll', 'srl', 'sra', 'dsll', 'dsrl', 'dsra', 'dsll32', 'dsrl32', 'dsra32') and rs != 0 and word != 0x00ff00ff:
            name = 'unsupported'
        elif name in ('sllv','srlv','srav','dsllv','dsrlv','dsrav') and sa:
            name = 'unsupported'
        elif name == 'jr' and (rt or rd or sa):
            name = 'unsupported'
        elif name == 'jalr' and (rt or sa):
            name = 'unsupported'
        elif name in ('mfhi', 'mflo') and (rs or rt or sa):
            name = 'unsupported'
        elif name in ('mthi', 'mtlo') and (rt or rd or sa):
            name = 'unsupported'
        elif name in ('div','divu') and (rd or sa):
            name = 'unsupported'
        elif name in ('mult','multu') and sa:
            name = 'unsupported'
        elif name == 'sync' and (rs or rt or rd):
            name = 'unsupported'
        elif name in ('addu', 'sub', 'subu', 'daddu', 'dsubu', 'and', 'or', 'xor', 'nor', 'slt', 'sltu', 'movz', 'movn') and sa:
            name = 'unsupported'
    if (op in (6, 7, 22, 23) and rt) or (op == 15 and rs):
        name = 'unsupported'
    return Instruction(pc, word, name, rs, rt, rd, sa, signed, target)
