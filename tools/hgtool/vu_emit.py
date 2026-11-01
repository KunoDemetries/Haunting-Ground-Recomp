"""Build-time VU1 AOT emission from user-local ELF data.

The generated C++ is a static control-flow graph. It contains no runtime
microinstruction decoder, PC loop, interpreter, or fallback execution path.
"""

from .vu_decode import decode_program


_BRANCHES={'b','ibeq','ibne'}
_UNSUPPORTED_CONTROL={'bal','jr','jalr','ibltz','ibgtz','iblez','ibgez'}
_BROADCAST={'x':0,'y':1,'z':2,'w':3}


def _file_bytes(image,address,size):
    if size<=0:
        raise ValueError('VU1 source size must be positive')
    for segment in image.segments:
        if segment.address<=address and address+size<=segment.address+segment.file_size:
            start=segment.offset+address-segment.address
            return image.data[start:start+size]
    raise ValueError(f'VU1 source 0x{address:08x}+0x{size:x} is not wholly file-backed')


def _program_bytes(image,program):
    """Assemble the exact final MicroMem range from one or more proven MPG uploads."""
    uploads=program.get('uploads')
    if uploads is None:
        micro=program['micro_address'];count=program['pair_count']
        return micro,_file_bytes(image,program['source'],count*8)
    start=min(upload['micro_address'] for upload in uploads)
    end=max(upload['micro_address']+upload['pair_count']*8 for upload in uploads)
    words=[None]*((end-start)//8)
    for upload in uploads:
        data=_file_bytes(image,upload['source'],upload['pair_count']*8)
        first=(upload['micro_address']-start)//8
        for n in range(upload['pair_count']):
            words[first+n]=int.from_bytes(data[n*8:n*8+8],'little')
    if any(word is None for word in words):
        raise ValueError('VU1 composite uploads leave an unproven MicroMem gap')
    return start,b''.join(word.to_bytes(8,'little') for word in words)


def _lower(pair):
    name,fields=pair.lower_name,pair.lower_fields
    if name in {'nop','b','ibeq','ibne','waitq'}:
        return []
    if name=='xtop':
        return [f'v.vu1.write_vi({fields["it"]},std::uint16_t(v.top));']
    if name=='fcset':
        return [f'v.vu1.fcset(0x{fields["imm24"]:06x}u);']
    if name=='fcand':
        return [f'v.vu1.fcand(0x{fields["imm24"]:06x}u);']
    if name=='lq':
        return [f'v.vu1.load_qword(v.vu_mem,v.vu_mem_defined,{fields["it"]},{fields["is"]},{fields["imm11"]},{fields["dest"]});']
    if name=='sq':
        # Sony SQ encodes the vector source in bits 11..15 and the integer
        # address register in bits 16..20.  The structural decoder exposes
        # those slots as is/it, so stores intentionally use them in this order.
        return [f'v.vu1.store_qword(v.vu_mem,v.vu_mem_defined,{fields["is"]},{fields["it"]},{fields["imm11"]},{fields["dest"]});']
    if name=='sqi':
        return [f'v.vu1.store_qword_increment(v.vu_mem,v.vu_mem_defined,{fields["is"]},{fields["it"]},{fields["dest"]});']
    if name=='move':
        return [f'v.vu1.move_vector({fields["ft"]},{fields["fs"]},{fields["dest"]});']
    if name=='isw':
        return [f'v.vu1.store_integer(v.vu_mem,v.vu_mem_defined,{fields["it"]},{fields["is"]},{fields["imm11"]},{fields["dest"]});']
    if name=='ilw':
        return [f'v.vu1.load_integer(v.vu_mem,v.vu_mem_defined,{fields["it"]},{fields["is"]},{fields["imm11"]},{fields["dest"]});']
    if name in {'iaddiu','isubiu'}:
        subtract='true' if name=='isubiu' else 'false'
        return [f'v.vu1.integer_add({fields["it"]},{fields["is"]},0x{fields["imm15"]:04x}u,{subtract});']
    if name in {'iadd','isub'}:
        subtract='true' if name=='isub' else 'false'
        return [f'v.vu1.integer_add_register({fields["id"]},{fields["is"]},{fields["it"]},{subtract});']
    if name=='iaddi':
        imm=fields['imm5']
        if imm<0:
            return [f'v.vu1.integer_add({fields["it"]},{fields["is"]},0x{-imm:04x}u,true);']
        return [f'v.vu1.integer_add({fields["it"]},{fields["is"]},0x{imm:04x}u,false);']
    if name=='mtir':
        return [f'v.vu1.mtir({fields["it"]},{fields["fs"]},{fields["fsf"]});']
    if name=='div':
        return [f'v.vu1.divide({fields["fs"]},{fields["ft"]},{fields["fsf"]},{fields["ftf"]});']
    if name=='erleng':
        return [f'v.vu1.erleng({fields["fs"]});']
    if name=='mfp':
        return [f'v.vu1.move_from_p({fields["ft"]},{fields["dest"]});']
    if name=='waitp':
        return ['v.vu1.wait_p();']
    if name=='xgkick':
        return [f'v.xgkick({fields["is"]},gif,gs);']
    if name in _UNSUPPORTED_CONTROL:
        raise ValueError(f'VU1 AOT indirect/unsupported control operation {name} at 0x{pair.address:04x}')
    raise ValueError(f'VU1 AOT runtime operation not implemented: lower {name} at 0x{pair.address:04x}')


def _upper(pair):
    name,fields=pair.upper_name,pair.upper_fields
    if name=='nop':
        return []
    if name in {'itof0','itof4','itof12','itof15','ftoi0','ftoi4','ftoi12','ftoi15'}:
        fractional={'itof0':0,'itof4':4,'itof12':12,'itof15':15,
                    'ftoi0':0,'ftoi4':4,'ftoi12':12,'ftoi15':15}[name]
        method='convert_integer' if name.startswith('itof') else 'convert_fixed'
        return [f'v.vu1.{method}({fields["ft"]},{fields["fs"]},{fields["dest"]},{fractional});']
    if name.startswith('mula') and name[-1] in _BROADCAST:
        lane=_BROADCAST[name[-1]]
        if fields['dest']==14:
            return [f'hg::vu_xyz_multiply_acc(v.vu1,{fields["fs"]},{fields["ft"]},{lane},false);']
        return [f'v.vu1.multiply_acc({fields["fs"]},{fields["ft"]},{fields["dest"]},{lane},false);']
    if name.startswith('madda') and name[-1] in _BROADCAST:
        lane=_BROADCAST[name[-1]]
        if fields['dest']==14:
            return [f'hg::vu_xyz_multiply_acc(v.vu1,{fields["fs"]},{fields["ft"]},{lane},true);']
        return [f'v.vu1.multiply_acc({fields["fs"]},{fields["ft"]},{fields["dest"]},{lane},true);']
    if name.startswith('madd') and name[-1] in _BROADCAST:
        lane=_BROADCAST[name[-1]]
        if fields['dest']==14:
            return [f'hg::vu_xyz_madd(v.vu1,{fields["fd"]},{fields["fs"]},{fields["ft"]},{lane});']
        return [f'v.vu1.madd_vector({fields["fd"]},{fields["fs"]},{fields["ft"]},{fields["dest"]},{lane});']
    if name.startswith('adda') and name[-1] in _BROADCAST:
        lane=_BROADCAST[name[-1]]
        return [f'v.vu1.add_acc({fields["fs"]},{fields["ft"]},{fields["dest"]},{lane},false);']
    if name.startswith('add') and name[-1] in _BROADCAST:
        lane=_BROADCAST[name[-1]]
        return [f'v.vu1.add_vector_broadcast({fields["fd"]},{fields["fs"]},{fields["ft"]},{fields["dest"]},{lane},false);']
    if name.startswith('max') and name[-1] in _BROADCAST:
        broadcast=_BROADCAST[name[-1]]
        suffix=f'{pair.address:04x}'
        lines=[
            f'const auto max_scalar_raw_{suffix}=v.vu1.read_vf({fields["ft"]},{broadcast});',
            f'const auto max_scalar_{suffix}=((max_scalar_raw_{suffix}>>23)&0xffu)?max_scalar_raw_{suffix}:(max_scalar_raw_{suffix}&0x80000000u);',
        ]
        for lane in range(4):
            if fields['dest']&(8>>lane):
                lines += [
                    f'const auto max_source_raw_{suffix}_{lane}=v.vu1.read_vf({fields["fs"]},{lane});',
                    f'const auto max_source_{suffix}_{lane}=((max_source_raw_{suffix}_{lane}>>23)&0xffu)?max_source_raw_{suffix}_{lane}:(max_source_raw_{suffix}_{lane}&0x80000000u);',
                    f'v.vu1.write_vf({fields["fd"]},{lane},hg::Vu1State::float_less(max_scalar_{suffix},max_source_{suffix}_{lane})?max_source_{suffix}_{lane}:max_scalar_{suffix});',
                ]
        return lines
    if name.startswith('mul') and name[-1] in _BROADCAST:
        broadcast=_BROADCAST[name[-1]]
        return [f'v.vu1.multiply_vector({fields["fd"]},{fields["fs"]},{fields["ft"]},{fields["dest"]},{broadcast});']
    if name=='add':
        return [f'v.vu1.add_vector({fields["fd"]},{fields["fs"]},{fields["ft"]},{fields["dest"]},false);']
    if name=='addi':
        suffix=f'{pair.address:04x}'
        lines=[
            f'std::array<std::uint32_t,4> addi_values_{suffix}{{}};',
            f'std::array<bool,4> addi_underflow_{suffix}{{}},addi_overflow_{suffix}{{}};',
        ]
        for lane in range(4):
            if fields['dest']&(8>>lane):
                lines += [
                    f'hg::Fpu addi_arithmetic_{suffix}_{lane};',
                    f'addi_values_{suffix}[{lane}]=addi_arithmetic_{suffix}_{lane}.add(v.vu1.read_vf({fields["fs"]},{lane}),v.vu1.i);',
                    f'addi_underflow_{suffix}[{lane}]=bool(addi_arithmetic_{suffix}_{lane}.control&0x4000u);',
                    f'addi_overflow_{suffix}[{lane}]=bool(addi_arithmetic_{suffix}_{lane}.control&0x8000u);',
                ]
        lines.append(f'v.vu1.update_arithmetic_flags(addi_values_{suffix},{fields["dest"]},addi_underflow_{suffix},addi_overflow_{suffix});')
        for lane in range(4):
            if fields['dest']&(8>>lane):
                lines.append(f'v.vu1.write_vf({fields["fd"]},{lane},addi_values_{suffix}[{lane}]);')
        return lines
    if name=='sub':
        return [f'v.vu1.add_vector({fields["fd"]},{fields["fs"]},{fields["ft"]},{fields["dest"]},true);']
    if name=='mul':
        return [f'v.vu1.multiply_vector_lanes({fields["fd"]},{fields["fs"]},{fields["ft"]},{fields["dest"]});']
    if name=='madd':
        return [f'v.vu1.madd_vector_lanes({fields["fd"]},{fields["fs"]},{fields["ft"]},{fields["dest"]});']
    if name=='minii':
        lines=['const auto mini_scalar_raw=v.vu1.i;',
               'const auto mini_scalar=((mini_scalar_raw>>23)&0xffu)?mini_scalar_raw:(mini_scalar_raw&0x80000000u);']
        for lane in range(4):
            if fields['dest']&(8>>lane):
                lines += [
                    f'const auto mini_source_raw_{lane}=v.vu1.read_vf({fields["fs"]},{lane});',
                    f'const auto mini_source_{lane}=((mini_source_raw_{lane}>>23)&0xffu)?mini_source_raw_{lane}:(mini_source_raw_{lane}&0x80000000u);',
                    f'v.vu1.write_vf({fields["fd"]},{lane},hg::Vu1State::float_less(mini_source_{lane},mini_scalar)?mini_source_{lane}:mini_scalar);',
                ]
        return lines
    if name in {'mulq','addq'}:
        multiply='true' if name=='mulq' else 'false'
        return [f'v.vu1.arithmetic_q({fields["fd"]},{fields["fs"]},{fields["dest"]},{multiply});']
    if name=='clip':
        return [f'v.vu1.clip_test({fields["fs"]},{fields["ft"]});']
    raise ValueError(f'VU1 AOT runtime operation not implemented: upper {name} at 0x{pair.address:04x}')


def _pipeline_accesses(pair):
    """Static register dependencies, Sony VU manual 3.4 and latency table 7.4."""
    reads={}; writes={}; vi_reads=set(); vi_writes={}
    def read(reg,mask):
        if reg and mask: reads[reg]=reads.get(reg,0)|mask
    def write(reg,mask):
        if reg and mask: writes[reg]=writes.get(reg,0)|mask
    n=pair.upper_name; f=pair.upper_fields; mask=f.get('dest',0)
    if n=='clip':
        read(f['fs'],14);read(f['ft'],1)
    elif n!='nop':
        read(f['fs'],mask)
        if n in {'add','sub','mul','madd'}:read(f['ft'],mask)
        elif n[-1:] in _BROADCAST:read(f['ft'],8>>_BROADCAST[n[-1]])
        target=f.get('fd',f.get('ft') if n.startswith(('itof','ftoi')) else 0)
        write(target,mask)
    upper_writes=dict(writes)
    n=pair.lower_name;f=pair.lower_fields;mask=f.get('dest',0)
    if pair.lower_is_immediate:return reads,writes,vi_reads,vi_writes
    if n=='lq':
        vi_reads.add(f['is']);write(f['it'],mask)
    elif n in {'sq','sqi'}:
        read(f['is'],mask);vi_reads.add(f['it'])
        if n=='sqi':vi_writes[f['it']]=1
    elif n=='move':read(f['fs'],mask);write(f['ft'],mask)
    elif n=='mfp':write(f['ft'],mask)
    elif n=='mtir':read(f['fs'],8>>f['fsf']);vi_writes[f['it']]=1
    elif n=='div':read(f['fs'],8>>f['fsf']);read(f['ft'],8>>f['ftf'])
    elif n=='erleng':read(f['fs'],14)
    elif n=='ilw':vi_reads.add(f['is']);vi_writes[f['it']]=4
    elif n=='isw':vi_reads.update((f['is'],f['it']))
    elif n in {'iaddiu','isubiu','iaddi'}:
        vi_reads.add(f['is']);vi_writes[f['it']]=1
    elif n in {'iadd','isub'}:
        vi_reads.update((f['is'],f['it']));vi_writes[f['id']]=1
    elif n in {'ibeq','ibne'}:vi_reads.update((f['is'],f['it']))
    elif n=='xgkick':vi_reads.add(f['is'])
    elif n=='xtop':vi_writes[f['it']]=1
    elif n=='fcand':vi_writes[1]=1
    elif n not in {'nop','b','waitq','waitp','fcset'}:
        raise ValueError(f'VU1 AOT dependency metadata missing for {n}')
    # Current programs have no dual VF writes. Reject any new collision until
    # whole-register upper priority is implemented (manual 3.4.3).
    lower_target=f.get('it') if n=='lq' else f.get('ft') if n in {'move','mfp'} else 0
    if lower_target in upper_writes:
        raise ValueError('VU1 simultaneous upper/lower VF write requires priority handling')
    vi_reads.discard(0);vi_writes.pop(0,None)
    return reads,writes,vi_reads,vi_writes


class _VFReadinessProof:
    """Conservative minimum elapsed cycles, local to one static basic block."""
    def __init__(self,optimize_vi_readiness=True):
        self.remaining={}
        self.vi_remaining={}
        self.optimize_vi_readiness=optimize_vi_readiness

    def advance(self):
        self.remaining={key:max(0,cycles-1) for key,cycles in self.remaining.items()}
        self.vi_remaining={reg:max(0,cycles-1) for reg,cycles in self.vi_remaining.items()}

    def vi_required(self,reg):
        # Absence means arbitrary entry-state readiness, never assumed ready.
        return not self.optimize_vi_readiness or self.vi_remaining.get(reg,1)>0

    def vi_read(self,reg):
        self.vi_remaining[reg]=0

    def vi_write(self,reg,latency):
        self.vi_remaining[reg]=latency

    def required(self,reg,mask):
        return sum(bit for lane,bit in enumerate((8,4,2,1))
                   if mask&bit and self.remaining.get((reg,lane),1)>0)

    def read(self,reg,mask):
        for lane,bit in enumerate((8,4,2,1)):
            if mask&bit:self.remaining[reg,lane]=0

    def write(self,reg,mask):
        for lane,bit in enumerate((8,4,2,1)):
            if mask&bit:self.remaining[reg,lane]=4


def _pair_lines(pair,readiness=None):
    reads,writes,vi_reads,vi_writes=_pipeline_accesses(pair)
    lines=['v.vu1.advance_pipeline(1);']
    if readiness is not None:
        readiness.advance()
        # Preserve uint64 clock wrap: fall back for this entire invocation.
        lines.append('if(!v.vu1.issue_cycle)vu_ready_clock_safe=false;')
    for reg,mask in sorted(reads.items()):
        needed=mask if readiness is None else readiness.required(reg,mask)
        if needed==mask:lines.append(f'v.vu1.require_vf({reg},{mask});')
        elif not needed:lines.append(f'if(!vu_ready_clock_safe)v.vu1.require_vf({reg},{mask});')
        else:lines.append(f'v.vu1.require_vf({reg},vu_ready_clock_safe?{needed}:{mask});')
        if readiness is not None:readiness.read(reg,mask)
    for reg in sorted(vi_reads):
        guard=f'v.vu1.require_vi({reg});'
        if readiness is not None and not readiness.vi_required(reg):
            guard='if(!vu_ready_clock_safe)'+guard
        lines.append(guard)
        if readiness is not None:readiness.vi_read(reg)
    # WAITQ and divider resource hazards stall both pipelines, so a same-pair
    # upper Q consumer observes the completed preceding division (manual p47).
    if not pair.lower_is_immediate and pair.lower_name in {'waitq','div'}:
        if readiness is not None:lines.append(f'const auto q_wait_cycle_{pair.address:04x}=v.vu1.issue_cycle;')
        lines.append('v.vu1.wait_q();')
        if readiness is not None:lines.append(f'if(v.vu1.issue_cycle<q_wait_cycle_{pair.address:04x})vu_ready_clock_safe=false;')
    if not pair.lower_is_immediate and pair.lower_name=='waitp':
        lines.append('v.vu1.wait_p();')
    lower_snapshot=None
    if (not pair.lower_is_immediate and pair.lower_name in {'sq','sqi'} and
            writes.get(pair.lower_fields['is'],0)&pair.lower_fields['dest']):
        # Upper/lower VU pipelines issue concurrently. SQ reads its VF/VI
        # operands before same-pair upper writeback, so preserve the pair-start
        # register state for the lower store while committing the upper normally.
        # Disjoint VF lanes need no snapshot: upper operations do not write VI,
        # and SQ observes neither ACC nor arithmetic flags. SQI stores the same
        # pair-start VF value; its VI post-increment then applies to live state
        # (2026-10-11: an ITOF12/SQI pair stored the new upper result as a GIF tag).
        suffix=f'{pair.address:04x}'
        lower_snapshot=f'lower_vu1_snapshot_{suffix}'
        lines.append(f'const auto {lower_snapshot}=v.vu1;')
    lines+=_upper(pair)
    if pair.lower_is_immediate:
        # Sony I-bit semantics: the lower word is data, not a lower operation.
        # The same-pair upper instruction observes the old I value; the new
        # immediate becomes visible at T for the following instruction.
        lines.append(f'v.vu1.i=0x{pair.lower_fields["value"]:08x}u;')
    elif lower_snapshot is not None:
        fields=pair.lower_fields
        if pair.lower_name=='sqi':
            lines.append(f'{lower_snapshot}.store_qword(v.vu_mem,v.vu_mem_defined,{fields["is"]},{fields["it"]},0,{fields["dest"]});')
            lines.append(f'v.vu1.write_vi({fields["it"]},std::uint16_t(v.vu1.read_vi({fields["it"]})+1));')
        else:
            lines.append(f'{lower_snapshot}.store_qword(v.vu_mem,v.vu_mem_defined,{fields["is"]},{fields["it"]},{fields["imm11"]},{fields["dest"]});')
    else:
        lines+=_lower(pair)
    lines += [f'v.vu1.produced_vf({reg},{mask});' for reg,mask in sorted(writes.items())]
    lines += [f'v.vu1.produced_vi({reg},{latency});' for reg,latency in sorted(vi_writes.items())]
    if readiness is not None:
        for reg,mask in writes.items():readiness.write(reg,mask)
        for reg,latency in vi_writes.items():readiness.vi_write(reg,latency)
    return lines


def _branch_condition(pair):
    fields=pair.lower_fields
    if pair.lower_name=='b':
        return None
    if pair.lower_name=='ibeq':
        return f'v.vu1.read_vi({fields["is"]})==v.vu1.read_vi({fields["it"]})'
    if pair.lower_name=='ibne':
        return f'v.vu1.read_vi({fields["is"]})!=v.vu1.read_vi({fields["it"]})'
    raise ValueError(f'VU1 AOT unsupported branch {pair.lower_name} at 0x{pair.address:04x}')


def _reachable(pairs,entries):
    by_address={pair.address:pair for pair in pairs}
    reachable=set()
    pending=list(entries)
    while pending:
        pc=pending.pop()
        if pc in reachable:
            continue
        pair=by_address.get(pc)
        if pair is None:
            raise ValueError(f'VU1 AOT control flow leaves configured program at 0x{pc:04x}')
        reachable.add(pc)
        if pair.lower_name in _UNSUPPORTED_CONTROL:
            raise ValueError(f'VU1 AOT unsupported control operation {pair.lower_name} at 0x{pc:04x}')
        if pair.lower_name in _BRANCHES or pair.end:
            delay=by_address.get(pc+8)
            if delay is None:
                raise ValueError(f'VU1 AOT control pair at 0x{pc:04x} has no in-program delay pair')
            if delay.lower_name in _BRANCHES or delay.lower_name in _UNSUPPORTED_CONTROL or delay.end:
                raise ValueError(f'VU1 AOT nested control/end delay pair at 0x{pc+8:04x} remains unsupported')
            _pair_lines(delay)
        if pair.end:
            continue
        if pair.lower_name in _BRANCHES:
            if pair.branch_target not in by_address:
                raise ValueError(f'VU1 AOT branch at 0x{pc:04x} leaves configured program')
            pending.append(pair.branch_target)
            if pair.lower_name!='b':
                pending.append(pc+16)
        else:
            pending.append(pc+8)
    return reachable



def _static_transform_loop(pairs,pc,leaders):
    """Resolve a bounded transform-loop scoreboard at emit time, never at runtime."""
    block=[pairs.get(pc+n*8) for n in range(15)]
    if any(p is None or p.end or p.lower_is_immediate for p in block):return []
    if any(pc+n*8 in leaders for n in range(1,15)):return []
    uppers=['clip','nop','nop','mulq','mulax','madday','maddaz','maddw',
            'mulax','madday','maddaz','maddw','mulq','ftoi4','ftoi0']
    lowers=['lq','lq','isubiu','sq','sq','lq','fcand','iadd','mtir','sq',
            'isw','div','iaddiu','ibne','iaddiu']
    actual_lowers=[p.lower_name for p in block]
    for n in (12,14):
        if actual_lowers[n]=='iaddi':actual_lowers[n]='iaddiu'
    if [p.upper_name for p in block]!=uppers or actual_lowers!=lowers:return []
    if block[13].branch_target!=pc:return []
    # Full accumulator producers make all following ACC reads defined.
    if any(block[n].upper_fields['dest']!=15 for n in (4,5,6,7,8,9,10,11)):return []
    # HG-DIAG-061: fuse only chains with dead intermediate MAC/STATUS/ACC.
    coefficients=[block[n].upper_fields['fs'] for n in range(4,12)]
    coordinate=block[4].upper_fields['ft']
    if not coordinate:return []
    if any(block[n].upper_fields['ft']!=coordinate for n in (4,5,6,8,9,10)):return []
    if any(block[n].upper_fields['ft']!=0 for n in (7,11)):return []
    all_writes=set()
    for p in block:all_writes.update(_pipeline_accesses(p)[1])
    if any(reg in all_writes for reg in coefficients if reg):return []
    # These fixed lower opcodes cannot observe MAC, STATUS or ACC.
    # Keep VF publication at the original pair and reject input overwrites.
    if any(coordinate in _pipeline_accesses(p)[1] for p in block[4:11]):return []
    vf_last={};vi_last={};vf_import={};vi_import={};memory=[]
    for slot,p in enumerate(block,1):
        reads,writes,vi_reads,vi_writes=_pipeline_accesses(p)
        if any(reg>=16 for reg in vi_reads|vi_writes.keys()):return []
        for reg,mask in reads.items():
            for lane,bit in enumerate((8,4,2,1)):
                if not mask&bit:continue
                key=(reg,lane)
                if key in vf_last:
                    if vf_last[key]>slot:return []
                else:vf_import[key]=min(vf_import.get(key,slot),slot)
        for reg in vi_reads:
            if reg in vi_last:
                if vi_last[reg]>slot:return []
            else:vi_import[reg]=min(vi_import.get(reg,slot),slot)
        if p.lower_name in {'lq','sq','isw'}:
            f=p.lower_fields
            base=f['it'] if p.lower_name=='sq' else f['is']
            if base in vi_last or base>=16:return []
            memory.append((slot,base,f['imm11'],p.lower_name=='lq',f['dest']))
        for reg,mask in writes.items():
            for lane,bit in enumerate((8,4,2,1)):
                if mask&bit:vf_last[reg,lane]=slot+4
        for reg,latency in vi_writes.items():vi_last[reg]=slot+latency
    # Every backedge satisfies the imported scoreboards automatically.
    if any(vf_last.get(key,0)>15+slot for key,slot in vf_import.items()):return []
    if any(vi_last.get(key,0)>15+slot for key,slot in vi_import.items()):return []
    masks={}
    for reg,lane in vf_import:masks[reg]=masks.get(reg,0)|(1<<lane)
    guard=['v.vu1.issue_cycle<=~std::uint64_t(0)-19u',
           '(!v.vu1.q_pending||v.vu1.q_cycles_remaining<=12u)']
    guard += [f'(v.vu1.vf_defined[{reg}]&{mask})=={mask}' for reg,mask in sorted(masks.items())]
    guard += [f'v.vu1.vf_ready[{reg}][{lane}]<=v.vu1.issue_cycle+{slot}u'
              for (reg,lane),slot in sorted(vf_import.items())]
    guard += [f'v.vu1.vi_ready[{reg}]<=v.vu1.issue_cycle+{slot}u' for reg,slot in sorted(vi_import.items())]
    out=['// Static whole-loop schedule; failed guards preserve the checked CFG.',
         'if('+ ' && '.join(guard)+') {',
         '    // HG-DIAG-061: exact fused block with live boundary outputs.',
         '    static constexpr std::array<std::uint32_t,4> transform_zero{0,0,0,0x3f800000u};',
         '    const hg::VuTransformSources transform_sources{'+','.join(f'v.vu1.vf[{reg}].data()' if reg else 'transform_zero.data()' for reg in coefficients)+'};',
         '    for(;;) {',
         '        const auto loop_cycle=v.vu1.issue_cycle;',
         '        if(loop_cycle>~std::uint64_t(0)-19u)break;']
    for slot,base,imm,read,mask in memory:
        out.append(f'        const auto loop_address_{slot}=std::int32_t(v.vu1.read_vi({base}))+({imm});')
    checks=[f'(loop_address_{slot}<0||loop_address_{slot}>=1024)' for slot,*_ in memory]
    out.append('        if('+' || '.join(checks)+')break;')
    checks=[]
    for slot,base,imm,read,mask in memory:
        if read:
            defined=sum(1<<lane for lane,bit in enumerate((8,4,2,1)) if mask&bit)
            checks.append(f'(v.vu_mem_defined[loop_address_{slot}]&{defined})!={defined}')
            for other,_,_,other_read,_ in memory:
                if not other_read:checks.append(f'loop_address_{slot}==loop_address_{other}')
    out.append('        if('+' || '.join(checks)+')break;')
    last_clock=0
    for slot,p in enumerate(block,1):
        if slot in (4,12,13):
            out.append(f'        v.vu1.advance_pipeline({slot-last_clock});')
            last_clock=slot
        # Same-pair SQ sees the old VF even when upper writes it.
        reads,writes,_,_=_pipeline_accesses(p)
        snapshot=p.lower_name=='sq' and writes.get(p.lower_fields['is'],0)&p.lower_fields['dest']
        if snapshot:out.append(f'        const auto loop_snapshot_{slot}=v.vu1;')
        if slot==5:
            out.append(f'        const auto transform=hg::fused_vu_transform(transform_sources,v.vu1.vf[{coordinate}]);')
        if slot in (8,12):
            destination=p.upper_fields['fd']
            if destination:
                out += [f'        v.vu1.vf[{destination}]=transform.values[{0 if slot==8 else 1}];',
                        f'        v.vu1.vf_defined[{destination}]|=15;']
            if slot==12:
                out += ['        v.vu1.acc=transform.acc;v.vu1.acc_defined|=15;',
                        '        v.vu1.mac=transform.mac;',
                        '        v.vu1.status=(v.vu1.status&~15u)|transform.current|(transform.sticky<<6);']
        elif not 5<=slot<=12:
            out += ['        '+line for line in _upper(p)]
        if snapshot:
            f=p.lower_fields
            out.append(f'        loop_snapshot_{slot}.store_qword(v.vu_mem,v.vu_mem_defined,{f["is"]},{f["it"]},{f["imm11"]},{f["dest"]});')
        else:out += ['        '+line for line in _lower(p)]
        if slot==14:out.append('        const bool loop_take='+_branch_condition(p)+';')
    out.append(f'        v.vu1.advance_pipeline({15-last_clock});')
    for (reg,lane),ready in sorted(vf_last.items()):
        out.append(f'        v.vu1.vf_ready[{reg}][{lane}]=loop_cycle+{ready}u;')
    for reg,ready in sorted(vi_last.items()):out.append(f'        v.vu1.vi_ready[{reg}]=loop_cycle+{ready}u;')
    out += [f'        if(!loop_take)goto L_{pc+120:04x};','    }','}']
    return out

def _body_lines(pairs,entries,optimize_readiness=False,optimize_vi_readiness=True,optimize_static_loop=False):
    by_address={pair.address:pair for pair in pairs}
    reachable=_reachable(pairs,entries)
    leaders=set(entries)
    for pair in pairs:
        if pair.lower_name in _BRANCHES:leaders.update((pair.branch_target,pair.address+16))
        if pair.end:leaders.add(pair.address+16)
    out=[];readiness=None;previous=None
    for pc in sorted(reachable):
        if optimize_readiness and (readiness is None or pc in leaders or previous!=pc-8):
            readiness=_VFReadinessProof(optimize_vi_readiness=optimize_vi_readiness)
        pair=by_address[pc]
        out.append(f'L_{pc:04x}: {{')
        # Registers this block writes, for the speculative VU1 commit merge.
        if pc in leaders:out += ['    '+line for line in _written_lines(by_address,pc,leaders)]
        if optimize_static_loop and pc in leaders:
            out += ['    '+line for line in _static_transform_loop(by_address,pc,leaders)]
        out += ['    '+line for line in _pair_lines(pair,readiness)]
        if pair.end:
            delay=by_address[pc+8]
            out += ['    '+line for line in _pair_lines(delay,readiness)]
            out.append(f'    v.vu1.tpc=0x{pc+16:04x}u;')
            out.append('    return;')
        elif pair.lower_name in _BRANCHES:
            delay=by_address[pc+8]
            condition=_branch_condition(pair)
            if condition is not None:
                out.append(f'    const bool take={condition};')
            out += ['    '+line for line in _pair_lines(delay,readiness)]
            if condition is None:
                out.append(f'    goto L_{pair.branch_target:04x};')
            else:
                out.append(f'    if(take)goto L_{pair.branch_target:04x};')
                out.append(f'    goto L_{pc+16:04x};')
        else:
            out.append(f'    goto L_{pc+8:04x};')
        out.append('}')
        previous=None if pair.end or pair.lower_name in _BRANCHES else pc
        if previous is None:readiness=None
    return out


_LIVENESS_UPPER={'nop','clip','add','addaw','addq','addx','addy','addz','addw','addi','sub','subq','subx','suby','subz','subw',
                 'ftoi0','ftoi4','ftoi12','ftoi15','itof0','itof4','itof12','itof15','madd','madday','maddaz','maddax','maddaw',
                 'maddx','maddy','maddz','maddw','maddq','maddi','maxx','maxy','maxz','maxw','minii','mul','mulax','mulay','mulaz',
                 'mulaw','mulq','muli','mulx','muly','mulz','mulw'}
_EFU={'erleng','eleng','esqrt','ersqrt','esum','ersadd','esadd','eatan','eatanxy','eatanxz','eexp','esin','ercpr'}
# Speculation misc bits (runtime/include/hg/vu1_spec.hpp): VI1-15 at their
# index, ACC X..W at 16..19, I at 20, the Q group at 21 and the P group at 22.
_SPEC_ACC=16;_SPEC_I=20;_SPEC_Q=21;_SPEC_P=22


def _lanes(reg,mask):
    # Field masks use bit3=X ... bit0=W; result bit reg*4+lane, lane 0=X.
    return sum(1<<(reg*4+lane) for lane in range(4) if mask&(8>>lane))


def _acc_lanes(mask,shift=_SPEC_ACC):
    return sum(1<<(shift+lane) for lane in range(4) if mask&(8>>lane))


def _spec_pair(pair):
    """(VF lanes read, misc read, VF lanes written, misc written) of one pair."""
    reads,writes,vi_reads,vi_writes=_pipeline_accesses(pair)
    vf_read=0;vf_written=0;misc_read=0;misc_written=0
    for reg,mask in reads.items():vf_read|=_lanes(reg,mask)
    for reg,mask in writes.items():vf_written|=_lanes(reg,mask)
    for reg in vi_reads:misc_read|=1<<reg
    for reg in vi_writes:misc_written|=1<<reg
    u=pair.upper_name;dest=pair.upper_fields.get('dest',0)
    if u not in _LIVENESS_UPPER:
        vf_read|=((1<<128)-1)&~15;misc_read|=(1<<21)-2
    if u.startswith(('madd','msub')):misc_read|=_acc_lanes(dest)
    if u.startswith(('mula','adda','suba','madda','msuba')):misc_written|=_acc_lanes(dest)
    if u.endswith('i') and not u.startswith(('itof','ftoi')):misc_read|=1<<_SPEC_I
    if pair.lower_is_immediate:misc_written|=1<<_SPEC_I
    else:
        n=pair.lower_name
        if n in {'div','sqrt','rsqrt'}:misc_written|=1<<_SPEC_Q
        if n in _EFU:misc_written|=1<<_SPEC_P
    return vf_read,misc_read,vf_written,misc_written


def _successors(by_address,pc):
    pair=by_address[pc]
    if pair.end:return []
    if pair.lower_name in _BRANCHES:
        return [pair.branch_target]+([pc+16] if pair.lower_name!='b' else [])
    return [pc+8]


def _node(by_address,pc):
    return [pc]+([pc+8] if by_address[pc].end or by_address[pc].lower_name in _BRANCHES else [])


def _spec_live_in(pairs,entries):
    """Per entry: (VF lanes, misc bits) that some path may read before writing.

    Misc covers VI, ACC lanes and I. Over-approximation is safe for
    speculation validation."""
    by_address={pair.address:pair for pair in pairs}
    reachable=_reachable(pairs,entries)
    effects={}
    for pc in reachable:
        gen_vf=gen_misc=kill_vf=kill_misc=0
        for address in _node(by_address,pc):
            vf_read,misc_read,vf_written,misc_written=_spec_pair(by_address[address])
            # Both slots of a pair read before either writes (Sony VU manual 3.4).
            gen_vf|=vf_read&~kill_vf;gen_misc|=misc_read&~kill_misc
            kill_vf|=vf_written;kill_misc|=misc_written
        mask=~((1<<_SPEC_Q)|(1<<_SPEC_P))
        effects[pc]=(gen_vf,gen_misc&mask,kill_vf,kill_misc&mask)
    every=((1<<128)-1)&~15
    live={pc:(0,0) for pc in reachable}
    changed=True
    while changed:
        changed=False
        for pc in reachable:
            out_vf=out_misc=0
            for successor in _successors(by_address,pc):
                vf,misc=live.get(successor,(0,0));out_vf|=vf;out_misc|=misc
            gen_vf,gen_misc,kill_vf,kill_misc=effects[pc]
            value=((gen_vf|(out_vf&~kill_vf))&every,(gen_misc|(out_misc&~kill_misc))&~1)
            if value!=live[pc]:live[pc]=value;changed=True
    # Q/P inputs are detected at runtime (Vu1State::spec_old_q_read/p_read).
    return {entry:live[entry] for entry in entries}


def _live_in_vf(pairs,entries):
    return {entry:value[0] for entry,value in _spec_live_in(pairs,entries).items()}


def _block_written(by_address,pc,leaders):
    """VF lanes and misc bits written by the straight-line block entered at pc."""
    vf=misc=0;address=pc
    while address in by_address:
        pair=by_address[address]
        for member in [address]+([address+8] if pair.end or pair.lower_name in _BRANCHES else []):
            _,_,vf_written,misc_written=_spec_pair(by_address[member])
            vf|=vf_written;misc|=misc_written
        if pair.end or pair.lower_name in _BRANCHES:break
        address+=8
        if address in leaders:break
    return vf,misc


def _written_lines(by_address,pc,leaders):
    vf,misc=_block_written(by_address,pc,leaders)
    lines=[]
    if vf&(2**64-1):lines.append(f'v.vu1.spec_written_vf[0]|=0x{vf&(2**64-1):x}ull;')
    if vf>>64:lines.append(f'v.vu1.spec_written_vf[1]|=0x{vf>>64:x}ull;')
    if misc:lines.append(f'v.vu1.spec_written_misc|=0x{misc:x}u;')
    return lines


def _entry_ranges(pairs,entry):
    """MicroMem pairs one activation at `entry` can execute: every pair reachable
    from it (relative branches only; JR/JALR are rejected) plus branch/END delay
    pairs. Returned as contiguous (first byte address, pair count) runs."""
    by_address={pair.address:pair for pair in pairs}
    covered=set()
    for pc in _reachable(pairs,[entry]):
        covered.update(_node(by_address,pc))
    runs=[]
    for address in sorted(covered):
        if runs and runs[-1][0]+runs[-1][1]*8==address:
            runs[-1][1]+=1
        else:
            runs.append([address,1])
    return [tuple(run) for run in runs]


def emit_vu1(image,programs):
    if not programs:
        return []
    out=[]
    dispatch=[]
    by_entry={}
    for number,program in enumerate(programs):
        micro_base,data=_program_bytes(image,program)
        count=len(data)//8
        pairs=decode_program(data,micro_base)
        expected=[int.from_bytes(data[n:n+8],'little') for n in range(0,len(data),8)]
        symbol=f'vu1_program_{number}'
        # HG: identity is per activation entry and covers exactly the MicroMem
        # pairs that activation can execute. A later upload elsewhere in
        # MicroMem (a new area's composite) leaves an already-proven program
        # valid; any changed reachable pair still fails explicitly.
        for entry in program['entries']:
            checks=[]
            for first,pair_count in _entry_ranges(pairs,entry):
                start=(first-micro_base)//8
                words=expected[start:start+pair_count]
                name=f'{symbol}_e{entry:04x}_{first:04x}'
                out.append(f'static constexpr std::array<std::uint64_t,{pair_count}> {name}={{')
                out.append('    '+','.join(f'0x{word:016x}ull' for word in words))
                out.append('};')
                checks.append((first//8,name))
            out.append(f'static bool {symbol}_e{entry:04x}_matches(const hg::Vif1Path& v) {{')
            for base,name in checks:
                out.append(f'    for(std::size_t n=0;n<{name}.size();++n)if(v.vu_micro_mem[0x{base:x}u+n]!={name}[n])return false;')
            out.append('    return true;')
            out.append('}')
            by_entry.setdefault(entry,[]).append((f'{symbol}_e{entry:04x}_matches',symbol,program['name']))
        fn=f'{symbol}_run'
        out.append(f'static void {fn}(hg::Vif1Path& v,std::uint16_t entry,hg::GifPath& gif,hg::GsRegisterState& gs) {{')
        out.append('    bool vu_ready_clock_safe=true;')
        for entry in program['entries']:
            out.append(f'    if(entry==0x{entry:04x}u)goto L_{entry:04x};')
        out.append('    throw std::runtime_error("VU1 AOT entry is not configured for this program; entry="+std::to_string(entry));')
        # Experimental VI elision is disabled until repeatable scene benefit is established.
        # Keep accepted VF proof and the original unconditional VI readiness checks.
        out += ['    '+line for line in _body_lines(pairs,program['entries'],optimize_readiness=True,optimize_vi_readiness=False,optimize_static_loop=True)]
        out.append('}')
        dispatch.append((symbol,fn,program['name']))
        live=_spec_live_in(pairs,program['entries'])
        out.append(f'static hg::Vu1SpecInfo {symbol}_spec_info(std::uint16_t entry) {{')
        for entry in program['entries']:
            vf,misc=live[entry]
            out.append(f'    if(entry==0x{entry:04x}u)return {{0x{vf&(2**64-1):016x}ull,0x{vf>>64:016x}ull,0x{misc:08x}u,true}};')
        out.append('    return {};')
        out.append('}')
    out.append('void run_vu1_aot(hg::Vif1Path& v,std::uint16_t entry,hg::GifPath& gif,hg::GsRegisterState& gs) {')
    out.append('    switch(entry) {')
    for entry in sorted(by_entry):
        out.append(f'    case 0x{entry:04x}u:')
        for matches,symbol,name in by_entry[entry]:
            out.append(f'        if({matches}(v)) {{{symbol}_run(v,entry,gif,gs);return;}} // {name}')
        out.append('        break;')
    out.append('    default:break;')
    out.append('    }')
    out.append('    throw std::runtime_error("VU1 AOT entry/program identity has no static translation; entry="+std::to_string(entry));')
    out.append('}')
    # Static live-in set of an activation; known=false for unknown programs/entries.
    out.append('hg::Vu1SpecInfo vu1_aot_spec_info(const hg::Vif1Path& v,std::uint16_t entry) {')
    out.append('    switch(entry) {')
    for entry in sorted(by_entry):
        out.append(f'    case 0x{entry:04x}u:')
        for matches,symbol,name in by_entry[entry]:
            out.append(f'        if({matches}(v))return {symbol}_spec_info(entry);')
        out.append('        break;')
    out.append('    default:break;')
    out.append('    }')
    out.append('    return {};')
    out.append('}')
    return out
