def dma_poll_loops(code):
    """Prove LW/SRL8/ANDI1/NOP*/BNE-zero/NOP polling of the loaded register."""
    found={}
    for pc,b in code.items():
        if b.name!='bne' or b.rt!=0 or b.target is None or not pc-64<=b.target<pc:continue
        body=[code.get(p) for p in range(b.target,pc,4)]
        slot=code.get(pc+4)
        if not slot or slot.word or any(i is None for i in body):continue
        ops=[i for i in body if i.word]
        if len(ops)!=3:continue
        load,shift,mask=ops
        if load.pc!=b.target or load.name!='lw' or not load.rt or load.rt==load.rs:continue
        r=load.rt
        if shift.name!='srl' or shift.rt!=r or shift.rd!=r or shift.sa!=8:continue
        if mask.name!='andi' or mask.rs!=r or mask.rt!=r or mask.immediate!=1 or b.rs!=r:continue
        found[b.target]=(load,pc+4)
    return found


def poll_loops(code):
    """Prove the narrow NOP*/RAM-load/zero-branch/NOP wait-loop shape."""
    found={}
    for pc,branch in code.items():
        if branch.name!='beq' or branch.rt!=0 or branch.target is None:continue
        start=branch.target
        if not pc-64<=start<=pc-4:continue
        load=code.get(pc-4);slot=code.get(pc+4)
        if not load or load.name not in ('lb','lbu','lw','lwu') or load.rt!=branch.rs:continue
        if not load.rt or load.rs==load.rt or not slot or slot.word!=0:continue
        if any(code.get(p) is None or code[p].word!=0 for p in range(start,pc-4,4)):continue
        found[start]=(load,pc+4)
    return found


def read_reg(index):
    """Resolve a decoded scalar register at translation time."""
    if not 0 <= index < 32:
        raise ValueError('invalid static EE register index')
    return f's.gpr[{index}].lo' if index else 'std::uint64_t(0)'


def write_reg(index, value):
    if not 0 <= index < 32:
        raise ValueError('invalid static EE register index')
    # A discarded destination must still evaluate loads/device reads and faults.
    # Scalar writes leave the upper 64 bits and the stored zero register intact.
    return f's.gpr[{index}].lo = {value};' if index else f'(void)std::uint64_t({value});'


def straight(i):
    a, b = read_reg(i.rs), read_reg(i.rt)
    immediate = f'{i.immediate & 0xffffffffffffffff}ull'
    uimm = i.word & 65535
    n = i.name
    if n == 'break':
        return f'throw hg::Fault(s.pc, "BREAK trap code 0x{(i.word >> 6) & 0xfffff:x}");'
    if n == 'syscall':
        return 'hg::kernel_call(s);'
    if n in ('div','divu','div1','divu1'):
        return f's.divide_word({i.rs},{i.rt},{str(n in ("divu","divu1")).lower()},{str(n.endswith("1")).lower()});'
    if n == 'addi':
        return f's.add_immediate_word({i.rt},{i.rs},{i.immediate});'
    if n in ('pmaxh','pminh'):
        return f's.half_minmax({i.rd},{i.rs},{i.rt},{str(n=="pmaxh").lower()});'
    if n == 'ppacb':
        return f's.pack_bytes({i.rd},{i.rs},{i.rt});'
    if n == 'pcgth':
        return f's.compare_halfwords({i.rd},{i.rs},{i.rt});'
    if n in ('pextlb','pextub'):
        return f's.extend_bytes({i.rd},{i.rs},{i.rt},{str(n=="pextub").lower()});'
    if n in ('pextlw','pextuw'):
        return f's.extend_words({i.rd},{i.rs},{i.rt},{str(n=="pextuw").lower()});'
    if n == 'qfsrv':
        return f's.funnel_quad({i.rd},{i.rs},{i.rt});'
    if n in ('psllh','psrlh','psrah'):
        return f's.shift_halfwords({i.rd},{i.rt},{i.sa},{["psllh","psrlh","psrah"].index(n)});'
    if n == 'sub':
        return f's.subtract_word({i.rd},{i.rs},{i.rt});'
    if n == 'pmfhl.lw':
        return f'if({i.rd}) s.gpr[{i.rd}]={{std::uint32_t(s.lo)|(std::uint64_t(std::uint32_t(s.hi))<<32),std::uint32_t(s.lo1)|(std::uint64_t(std::uint32_t(s.hi1))<<32)}};'
    if n in ('mfhi1','mflo1'):
        return write_reg(i.rd, f's.{"hi1" if n=="mfhi1" else "lo1"}')
    if n in ('mult','multu','mult1','multu1','madd','maddu','madd1','maddu1'):
        unsigned = n in ('multu','multu1','maddu','maddu1')
        return f's.multiply_word({i.rd},{i.rs},{i.rt},{str(unsigned).lower()},{str(n.endswith("1")).lower()},{str(n.startswith("madd")).lower()});'
    if n in ('movz','movn'):
        condition = '==' if n == 'movz' else '!='
        return f'if ({b} {condition} 0) ' + write_reg(i.rd, a)
    if n in ('pand','por','pxor','pnor'):
        return f's.plogic({i.rd},{i.rs},{i.rt},{["pand","por","pxor","pnor"].index(n)});'
    if n in ('paddh', 'psubb', 'psubh', 'psubw', 'psubsb', 'psubsh', 'psubsw', 'paddsb', 'paddsh', 'paddsw', 'padduw', 'padduh', 'paddub'):
        return f's.{n}({i.rd}, {i.rs}, {i.rt});'
    if n in ('pcpyh','pcpyld','pcpyud'):
        kind={'pcpyh':0,'pcpyld':1,'pcpyud':2}[n]
        return f's.pcpy({i.rd},{i.rs},{i.rt},{kind});'
    if n in ('mthi', 'mtlo', 'mthi1', 'mtlo1'):
        return f's.{n[2:]} = {a};'
    if n in ('mtsab', 'mtsah'):
        mask, shift = (15, 3) if n == 'mtsab' else (7, 4)
        return f's.sa = unsigned(({a} ^ {uimm}ull) & {mask}) << {shift};'
    if n == 'mtc1':
        return f's.fpr[{i.rd}] = std::uint32_t({b});'
    if n == 'mfc1':
        return write_reg(i.rt, f'hg::sx32(s.fpr[{i.rd}])')
    if n == 'qmfc2':
        return f's.qmfc2({i.rt}, {i.rd});'
    if n == 'qmtc2':
        return f's.qmtc2({i.rt}, {i.rd});'
    if n == 'cfc2':
        return write_reg(i.rt, f'hg::sx32(s.vu_control_read({i.rd}))')
    if n == 'ctc2':
        return f's.vu_control_write({i.rd}, std::uint32_t({b}));'
    if n == 'vabs':
        return f's.vabs({i.rt}, {i.rd}, {i.rs & 15});'
    if n == 'vmove':
        return f's.vmove({i.rt}, {i.rd}, {i.rs & 15});'
    if n == 'vmr32':
        return f's.vmr32({i.rt}, {i.rd}, {i.rs & 15});'
    if n in ('vftoi0','vftoi4','vftoi12','vftoi15','vitof0','vitof4','vitof12','vitof15'):
        return f's.vu_convert({i.rt}, {i.rd}, {i.rs & 15}, {int(n[5:])}, {str(n.startswith("vitof")).lower()});'
    if n in ('vmul','vaddbc','vsubbc'):
        broadcast=(8+(i.word&3)) if n=='vsubbc' else (i.word&3)
        return f's.vu_arithmetic({i.sa}, {i.rd}, {i.rt}, {i.rs & 15}, {str(n=="vmul").lower()}, {broadcast});'
    if n == 'vmulbc':
        return f's.vu_arithmetic({i.sa}, {i.rd}, {i.rt}, {i.rs & 15}, true, {8+(i.word & 3)});'
    if n in ('vmulabc', 'vmaddabc', 'vmaddbc'):
        operation = {'vmulabc': 'multiply_accumulator', 'vmaddabc': 'add_accumulator',
                     'vmaddbc': 'add_vector'}[n]
        destination = i.sa if n == 'vmaddbc' else 0
        return (f's.vu_broadcast_product({destination}, {i.rd}, {i.rt}, {i.rs & 15}, '
                f'{i.word & 3}, hg::VuBroadcastProduct::{operation});')
    if n == 'vaddq':
        return f's.vu_arithmetic({i.sa}, {i.rd}, 0, {i.rs & 15}, false, 4);'
    if n == 'vsqrt':
        return f's.vu_sqrt({i.rt}, {(i.rs >> 2) & 3});'
    if n == 'vwaitq':
        return 's.vu_waitq();'
    if n == 'vnop':
        return '; // Architecturally defined VNOP.'
    if n == 'pref':
        # EE Core Instruction Set Manual v6.0 p96: PREF is an advisory cache
        # hint, has no architectural result and raises no addressing exception.
        return '; // Architecturally defined cache-neutral PREF hint.'
    if n == 'vmulq':
        return f's.vu_arithmetic({i.sa}, {i.rd}, 0, {i.rs & 15}, true, 4);'
    if n == 'vsub':
        return f's.vu_arithmetic({i.sa}, {i.rd}, {i.rt}, {i.rs & 15}, false, 5);'
    if n == 'vadd':
        return f's.vu_arithmetic({i.sa}, {i.rd}, {i.rt}, {i.rs & 15}, false, 6);'
    if n in ('vopmula','vopmsub'):
        return f's.vu_outer({i.sa}, {i.rd}, {i.rt}, {str(n=="vopmsub").lower()});'
    if n == 'vdiv':
        return f's.vu_divide({i.rd}, {i.rt}, {i.rs & 3}, {(i.rs >> 2) & 3});'
    if n == 'ctc1':
        return f's.fpu.write_control(std::uint32_t({b}));'
    if n == 'cfc1':
        return write_reg(i.rt, 'hg::sx32(s.fpu.control)')
    if n in ('adda.s', 'add.s'):
        return f's.add_float({i.rd}, {i.rt}, {i.sa}, {str(n == "adda.s").lower()});'
    if n == 'sub.s':
        return f's.sub_float({i.rd}, {i.rt}, {i.sa});'
    if n == 'mul.s':
        return f's.mul_float({i.rd}, {i.rt}, {i.sa});'
    if n == 'div.s':
        return f's.div_float({i.rd}, {i.rt}, {i.sa});'
    if n == 'sqrt.s':
        return f's.sqrt_float({i.rt}, {i.sa});'
    if n == 'cvt.s.w':
        return f's.cvt_s_w({i.rd}, {i.sa});'
    if n == 'mov.s':
        return f's.fpr[{i.sa}]=s.fpr[{i.rd}];'
    if n == 'neg.s':
        return f's.fpr[{i.sa}]=s.fpr[{i.rd}]^0x80000000u;'
    if n == 'cvt.w.s':
        return f's.cvt_w_s({i.rd}, {i.sa});'
    if n == 'c.le.s':
        return f's.compare_float({i.rd}, {i.rt}, true, true);'
    if n in ('c.eq.s','c.olt.s'):
        return f's.compare_float({i.rd}, {i.rt}, {str(n=="c.olt.s").lower()});'
    if n == 'madd.s':
        return f's.madd_float({i.rd}, {i.rt}, {i.sa});'
    if n == 'mula.s':
        return f's.mula_float({i.rd}, {i.rt});'
    if n == 'madda.s':
        return f's.madda_float({i.rd}, {i.rt});'
    if n == 'msuba.s':
        return f's.msuba_float({i.rd}, {i.rt});'
    if n == 'msub.s':
        return f's.msub_float({i.rd}, {i.rt}, {i.sa});'
    if n == 'sync':
        return f's.sync({i.sa});'
    if n in ('cache_dhwbin','cache_dxwbin'):
        return f's.cache_writeback(std::uint32_t({a}+{immediate}));'
    if n == 'cache_dxltg':
        return f's.cache_load_tag(std::uint32_t({a}+{immediate}));'
    if n == 'cache_dxin':
        return f's.cache_index_invalidate(std::uint32_t({a}+{immediate}));'
    if n in ('ei','di'):
        return f's.set_interrupts({str(n=="ei").lower()});'
    if n.startswith('mfc0_'):
        return write_reg(i.rt, f'hg::sx32(s.cp0_{n[5:]})')
    if n.startswith('mtc0_'):
        return f's.cp0_{n[5:]}=std::uint32_t({b});'
    if n == 'lui':
        return write_reg(i.rt, f'hg::sx32({uimm}u << 16)')
    values = {
        'addiu': f'hg::sx32(std::uint32_t({a} + {immediate}))',
        'daddiu': f'{a} + {immediate}',
        'andi': f'{a} & {uimm}ull', 'ori': f'{a} | {uimm}ull', 'xori': f'{a} ^ {uimm}ull',
        'slti': f'hg::signed_less({a}, {immediate})', 'sltiu': f'{a} < {immediate}',
    }
    if n in values:
        return write_reg(i.rt, values[n])
    values = {
        'sll': f'hg::sx32(std::uint32_t({b}) << {i.sa})',
        'srl': f'hg::sx32(std::uint32_t({b}) >> {i.sa})',
        'sra': f'hg::sra32(std::uint32_t({b}), {i.sa})',
        'sllv': f'hg::sx32(std::uint32_t({b}) << ({a} & 31))',
        'srlv': f'hg::sx32(std::uint32_t({b}) >> ({a} & 31))',
        'srav': f'hg::sra32(std::uint32_t({b}), unsigned({a} & 31))',
        'dsll': f'{b} << {i.sa}', 'dsll32': f'{b} << {i.sa+32}',
        'dsrl': f'{b} >> {i.sa}', 'dsrl32': f'{b} >> {i.sa+32}',
        'dsra': f'hg::sra64({b}, {i.sa})', 'dsra32': f'hg::sra64({b}, {i.sa+32})',
        'dsllv': f'{b} << ({a} & 63)', 'dsrlv': f'{b} >> ({a} & 63)',
        'dsrav': f'hg::sra64({b}, unsigned({a} & 63))',
        'addu': f'hg::sx32(std::uint32_t({a} + {b}))',
        'subu': f'hg::sx32(std::uint32_t({a} - {b}))',
        'daddu': f'{a} + {b}', 'dsubu': f'{a} - {b}',
        'and': f'{a} & {b}', 'or': f'{a} | {b}', 'xor': f'{a} ^ {b}', 'nor': f'~({a} | {b})',
        'slt': f'hg::signed_less({a}, {b})', 'sltu': f'{a} < {b}', 'mfhi': 's.hi', 'mflo': 's.lo',
    }
    if n in values:
        return write_reg(i.rd, values[n])
    address = f'std::uint32_t({a} + {immediate})'
    if n == 'lwc1':
        return f's.fpr[{i.rt}]=std::uint32_t(s.load_scalar<4,false>({address}));'
    if n == 'lqc2':
        return f's.lqc2({address}, {i.rt});'
    if n == 'swc1':
        return f's.store_scalar<4>({address},s.fpr[{i.rt}]);'
    if n == 'sqc2':
        return f's.sqc2({address}, {i.rt});'
    if n in ('sq', 'lq'):
        method = 'store_quad' if n == 'sq' else 'load_quad'
        return f's.{method}({address}, {i.rt});'
    if n in ('ldl', 'ldr'):
        return f's.load_double_unaligned({i.rt}, {address}, {str(n == "ldl").lower()});'
    if n in ('lwl', 'lwr'):
        return f's.load_word_unaligned({i.rt}, {address}, {str(n == "lwl").lower()});'
    loads = {'lb': (1, True), 'lbu': (1, False), 'lh': (2, True), 'lhu': (2, False),
             'lw': (4, True), 'lwu': (4, False), 'ld': (8, False)}
    if n in loads:
        size, sign = loads[n]
        return write_reg(i.rt, f's.load_scalar<{size},{str(sign).lower()}>({address})')
    if n in {'sb', 'sh', 'sw', 'sd'}:
        size = {'sb': 1, 'sh': 2, 'sw': 4, 'sd': 8}[n]
        return f's.store_scalar<{size}>({address}, {b});'
    if n in ('sdl', 'sdr'):
        return f's.store_double_unaligned({address}, {b}, {str(n == "sdl").lower()});'
    if n in ('swl', 'swr'):
        return f's.store_word_unaligned({address}, std::uint32_t({b}), {str(n == "swl").lower()});'
    return f'throw hg::Fault(s.pc, "{n} instruction 0x{i.word:08x}");'


def emit(code, image_hash, overlays=(), vu1_image=None, vu1_programs=()):
    waits=poll_loops(code)
    dma_waits=dma_poll_loops(code)
    vu_lines=[]
    if vu1_programs:
        if vu1_image is None:raise ValueError('VU1 AOT emission requires the source ELF image')
        from .vu_emit import emit_vu1
        vu_lines=emit_vu1(vu1_image,vu1_programs)
    def overlay_at(pc):
        return any(o['address']<=pc<o['address']+o['size'] for o in overlays)
    ranges=[]
    for o in overlays:
        a=o['address']&0x1fffffff if 0x80000000<=o['address']<0xc0000000 else o['address']
        ranges.append(f'{{0x{a:x}u,0x{a+o["size"]:x}u}}')
    out = ['// Generated from the user-local ELF. Do not redistribute.',
           f'// SHA-256: {image_hash}', '#include "hg/image.hpp"',
           '// HG-DIAG-001: instruction history is optional at build time; watches require it.',
           '#ifndef HG_EE_INSTRUCTION_TRACE', '#define HG_EE_INSTRUCTION_TRACE 1', '#endif',
           'bool hg::compiled_ee_instruction_trace() {return HG_EE_INSTRUCTION_TRACE!=0;}',
           f'const char* hg::compiled_image_sha256() {{ return "{image_hash}"; }}']
    if vu_lines:
        out += ['#include "hg/vu_xyz.hpp"','#include "hg/vu_matrix.hpp"']
        out += ['#include "hg/vu1_spec.hpp"']
        out += ['namespace { void run_vu1_aot(hg::Vif1Path&,std::uint16_t,hg::GifPath&,hg::GsRegisterState&);',
                'hg::Vu1SpecInfo vu1_aot_spec_info(const hg::Vif1Path&,std::uint16_t); }']
    configure='void hg::configure_compiled_image(hg::State& s) { s.kernel_code_regions = {' + ','.join(ranges) + '};'
    if vu_lines:configure+=' s.vif1.vu1_executor=&run_vu1_aot; hg::vu1_spec_info_query=&vu1_aot_spec_info;'
    configure+=' }'
    out += [configure,
           '#if defined(_MSC_VER)', '#define HG_AOT_NOINLINE __declspec(noinline)',
           '#elif defined(__GNUC__)', '#define HG_AOT_NOINLINE __attribute__((noinline))',
           '#else', '#define HG_AOT_NOINLINE', '#endif', 'namespace {']
    out += vu_lines
    page=None;pages=[]
    fallback='default: --budget; if constexpr(HG_EE_INSTRUCTION_TRACE)s.trace_pc(); if (hg::return_from_kernel(s)) break; throw hg::Fault(s.pc, "address has no static translation");'
    for pc, i in sorted(code.items()):
        if pc>>12!=page:
            if page is not None:out += [fallback,'} } return true; }']
            page=pc>>12;pages.append(page)
            out += [f'HG_AOT_NOINLINE bool page_{page:x}(hg::State& s, std::uint64_t& budget, bool cooperative) {{',
                    f'while(budget && (s.pc >> 12)==0x{page:x}u) {{ switch(s.pc) {{']
        out += [f'case 0x{pc:08x}u: step_{pc:x}: {{']
        out += ['if(!budget)return true; --budget; if constexpr(HG_EE_INSTRUCTION_TRACE)s.trace_pc();']
        if pc in dma_waits and not any(overlay_at(p) for p in range(pc,dma_waits[pc][1]+4,4)):
            load,end=dma_waits[pc]
            out += [f'if(cooperative && {read_reg(load.rt)}==1) {{',
                    f'const auto address=std::uint32_t({read_reg(load.rs)}+{load.immediate&0xffffffffffffffff}ull);',
                    f'bool watched=false;for(const auto& w:s.trace_watches)watched|=w.pc>=0x{pc:x}u && w.pc<=0x{end:x}u;',
                    'if(!watched && ((address==0x1000b000u && (s.dmac.channels[3].chcr&0x100)) || (address==0x1000b400u && (s.dmac.channels[4].chcr&0x100))))return false;',
                    '}']
        if pc in waits and not any(overlay_at(p) for p in range(pc,waits[pc][1]+4,4)):
            load,end=waits[pc];size=1 if load.name in ('lb','lbu') else 4
            out += [f'if(cooperative && {read_reg(load.rt)}==0) {{',
                    f'const auto address=std::uint32_t({read_reg(load.rs)}+{load.immediate&0xffffffffffffffff}ull);',
                    f'bool watched=false;for(const auto& w:s.trace_watches)watched|=w.pc>=0x{pc:x}u && w.pc<=0x{end:x}u;',
                    f'if(!watched && address%{size}==0 && std::uint64_t(address)+{size}<=s.ram.size() && s.load(address,{size},false)==0) return false;',
                    '}']
        if overlay_at(pc):
            out += [f's.require_instruction(0x{pc:08x}u,0x{i.word:08x}u);']
        if i.name == 'syscall':
            out += ['hg::kernel_call(s); if(cooperative)return false; break;', '}']
            continue
        if i.name == 'break':
            out += [straight(i), '}']
            continue
        if i.name == 'eret':
            out += ['s.exception_return(); break;', '}']
            continue
        if not i.control:
            continuation=f'goto step_{pc+4:x};' if pc+4 in code and (pc+4)>>12==page else 'break;'
            out += [straight(i), f's.pc = 0x{pc + 4:08x}u; {continuation}', '}']
            continue
        slot = code.get(pc + 4)
        invalid_slot = slot is None or slot.control or slot.name in ('sync','syscall')
        a, b = read_reg(i.rs), read_reg(i.rt)
        if i.name in ('jr', 'jalr'):
            out += [f'const auto target = std::uint32_t({a});']
        elif i.name in ('j', 'jal'):
            out += [f'const auto target = 0x{i.target:08x}u;']
        else:
            base = {'beql':'beq', 'bnel':'bne', 'blezl':'blez', 'bgtzl':'bgtz', 'bltzl':'bltz', 'bgezl':'bgez',
                    'bc1fl':'bc1f','bc1tl':'bc1t'}.get(i.name, i.name)
            condition = {'beq': f'{a} == {b}', 'bne': f'{a} != {b}',
                         'blez': f'({a} == 0 || hg::signed_less({a}, 0))',
                         'bgtz': f'({a} != 0 && !hg::signed_less({a}, 0))',
                         'bltz': f'hg::signed_less({a}, 0)', 'bgez': f'!hg::signed_less({a}, 0)',
                         'bc1f':'(s.fpu.control&0x00800000u)==0', 'bc1t':'(s.fpu.control&0x00800000u)!=0'}[base]
            out += [f'const bool taken = {condition};',
                    f'const auto target = taken ? 0x{i.target:08x}u : 0x{pc + 8:08x}u;']
        if i.name in ('jal', 'jalr'):
            rd = 31 if i.name == 'jal' else i.rd
            out += [write_reg(rd, f'hg::sx32(0x{pc + 8:08x}u)')]
        if i.likely:
            out += ['if (taken) {']
        out += [f's.pc = 0x{pc + 4:08x}u;']
        if slot is not None and overlay_at(pc+4):
            out += [f's.require_instruction(0x{pc+4:08x}u,0x{slot.word:08x}u);']
        out += ['throw hg::Fault(s.pc, "unsupported delay slot");' if invalid_slot else straight(slot)]
        if i.likely:
            out += ['}']
        out += [f's.last_transfer_pc=0x{pc:08x}u;', 's.pc = target;']
        if i.name=='jr' and i.rs==31:out += ['if(cooperative && target==0xff003000u)return false;']
        # Static intra-page branches already name compiled labels. Re-enter
        # their normal budget/trace prologue directly instead of redispatching
        # through the page switch. Indirect and cross-page transfers stay on
        # the checked dispatcher, with delay slots and guest state unchanged.
        if i.name not in ('jr','jalr'):
            if i.target in code and i.target>>12==page:
                prefix='' if i.name in ('j','jal') else 'if(taken)'
                out += [f'{prefix}goto step_{i.target:x};']
            if i.name not in ('j','jal') and pc+8 in code and (pc+8)>>12==page:
                out += [f'if(!taken)goto step_{pc+8:x};']
        out += ['break;', '}']
    if page is not None:out += [fallback,'} } return true; }']
    out += ['void run_pages(hg::State& s,std::uint64_t block_budget,bool cooperative) {',
            'if constexpr(!HG_EE_INSTRUCTION_TRACE)if(!s.trace_watches.empty())throw hg::Fault(s.pc,"EE instruction history is disabled in this AOT build");',
            'while (block_budget) { switch (s.pc >> 12) {']
    out += [f'case 0x{page:x}u: if(!page_{page:x}(s,block_budget,cooperative))return; break;' for page in pages]
    out += ['default: --block_budget;if constexpr(HG_EE_INSTRUCTION_TRACE)s.trace_pc();if(hg::return_from_kernel(s))break;throw hg::Fault(s.pc,"address has no static translation");',
            '} }','}','}',
            'void hg::run(hg::State& s,std::uint64_t budget){run_pages(s,budget,false);}',
            'void hg::run_burst(hg::State& s,std::uint64_t budget){run_pages(s,budget,true);}']
    return '\n'.join(out) + '\n'


def emit_shards(code, image_hash, overlays=(), vu1_image=None, vu1_programs=(),
                pages_per_shard=32):
    """Split the existing EE output into stable native translation units.

    emit() remains the single-source correctness baseline used by synthetic
    tests. This preserves those generated page bodies byte-for-byte, but moves
    groups of guest 4 KiB pages into separate C++ files. The small main source
    keeps image metadata, VU1 code and the checked page dispatcher. Stable
    address-based grouping means adding a root normally rewrites only shards
    whose generated code actually changed.
    """
    if not isinstance(pages_per_shard, int) or pages_per_shard < 1:
        raise ValueError('pages_per_shard must be a positive integer')
    generated=emit(code,image_hash,overlays,vu1_image,vu1_programs)
    lines=generated.splitlines()
    starts=[n for n,line in enumerate(lines)
            if line.startswith('HG_AOT_NOINLINE bool page_')]
    if not starts:
        return generated,{}
    run_index=next((n for n,line in enumerate(lines)
                    if line.startswith('void run_pages(')),None)
    if run_index is None or run_index<=starts[-1]:
        raise ValueError('generated EE page layout is malformed')
    pages=[]
    for n,start in enumerate(starts):
        end=starts[n+1] if n+1<len(starts) else run_index
        name=lines[start].split('page_',1)[1].split('(',1)[0]
        pages.append((int(name,16),lines[start:end]))
    prefix=lines[:starts[0]]
    namespace_index=max((n for n,line in enumerate(prefix) if line=='namespace {'),
                        default=-1)
    if namespace_index<0:
        raise ValueError('generated EE anonymous namespace is missing')
    declarations=[f'bool page_{page:x}(hg::State&,std::uint64_t&,bool);'
                  for page,_ in pages]
    main_lines=(prefix[:namespace_index]+declarations+
                prefix[namespace_index:]+lines[run_index:])
    shard_header=[
        '// Generated from the user-local ELF. Do not redistribute.',
        f'// SHA-256: {image_hash}',
        '#include "hg/image.hpp"',
        '// HG-DIAG-001: instruction history is optional at build time; watches require it.',
        '#ifndef HG_EE_INSTRUCTION_TRACE',
        '#define HG_EE_INSTRUCTION_TRACE 1',
        '#endif',
        '#if defined(_MSC_VER)',
        '#define HG_AOT_NOINLINE __declspec(noinline)',
        '#elif defined(__GNUC__)',
        '#define HG_AOT_NOINLINE __attribute__((noinline))',
        '#else',
        '#define HG_AOT_NOINLINE',
        '#endif',
    ]
    grouped={}
    for page,body in pages:
        group=page//pages_per_shard
        grouped.setdefault(group,[]).extend(body)
    shards={
        f'translated-shard-{group:04x}.cpp':'\n'.join(shard_header+body)+'\n'
        for group,body in sorted(grouped.items())
    }
    return '\n'.join(main_lines)+'\n',shards
