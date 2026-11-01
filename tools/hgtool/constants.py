"""Conservative straight-line constants for explicitly declared callback ABIs."""
MASK64=(1<<64)-1


def sx32(value):
    value&=0xffffffff
    return value | (0xffffffff00000000 if value&0x80000000 else 0)


def update(regs,i):
    a,b=regs.get(i.rs),regs.get(i.rt)
    name=i.name
    dest=None
    value=None
    if name=='lui': dest=i.rt; value=sx32((i.word&65535)<<16)
    elif name in ('addiu','daddiu','ori','andi','xori'):
        dest=i.rt
        if a is not None:
            if name=='addiu': value=sx32(a+i.immediate)
            elif name=='daddiu': value=(a+i.immediate)&MASK64
            elif name=='ori': value=a|(i.word&65535)
            elif name=='andi': value=a&(i.word&65535)
            else: value=a^(i.word&65535)
    elif name in ('addu','daddu','subu','dsubu','or','and','xor'):
        dest=i.rd
        if a is not None and b is not None:
            if name in ('addu','daddu'): value=(a+b)&MASK64
            elif name in ('subu','dsubu'): value=(a-b)&MASK64
            elif name=='or': value=a|b
            elif name=='and': value=a&b
            else: value=a^b
            if name in ('addu','subu'): value=sx32(value)
    elif name in ('sll','srl'):
        dest=i.rd
        if b is not None:
            value=sx32((b&0xffffffff)<<i.sa if name=='sll' else (b&0xffffffff)>>i.sa)
    elif name in ('lb','lbu','lh','lhu','lw','lwu','ld','lq','lwl','lwr','ldl','ldr'):
        # A memory value is unknown, but only the load destination changes.
        # Preserve independently proven callback pointers in other registers.
        dest=i.rt
    elif name in ('sb','sh','sw','sd','sq','swc1','sync','pref','cache_dxin'):
        pass
    else:
        # Unknown effects never leave a stale candidate constant behind.
        regs.clear()
    if dest:
        if value is None: regs.pop(dest,None)
        else: regs[dest]=value
    regs[0]=0


def callback_targets(code,cfg):
    bindings=cfg.get('callback_bindings',[])
    if not bindings:return []
    starts={f['address'] for f in cfg.get('functions',[])}
    starts.update(t for item in cfg.get('indirect_targets',[]) for t in item['targets'])
    starts.update(o['address']+v for o in cfg.get('overlays',[]) for v in o.get('entry_offsets',[0]))
    for i in code.values():
        if i.control:
            if i.target is not None: starts.add(i.target)
            starts.add(i.pc+8)
    regs={0:0}; previous=None; skip=None; found=[]
    for pc,i in sorted(code.items()):
        if pc==skip: previous=pc;continue
        if previous is None or pc!=previous+4 or pc in starts:regs={0:0}
        previous=pc
        if i.control:
            slot=code.get(pc+4)
            if i.name=='jal' and slot and not slot.control and slot.name not in ('syscall','break','unsupported'):
                regs[31]=sx32(pc+8)
                update(regs,slot)
                for binding in bindings:
                    value=regs.get(binding['argument'])
                    if i.target==binding['callee'] and value is not None:
                        found.append({'caller':pc,'site':binding['site'],'target':value&0xffffffff,
                                      'callee':i.target,'argument':binding['argument']})
            regs={0:0};skip=pc+4
        else:update(regs,i)
    return found


def static_indirect_targets(code,cfg):
    """Find only jalr/jr destinations proven by local straight-line constants.

    This is discovery evidence, never a replacement for the register-selected
    transfer at run time.  A control-flow join, call, unknown instruction, or
    non-contiguous word clears the candidate state.
    """
    starts={f['address'] for f in cfg.get('functions',[])}
    starts.update(t for item in cfg.get('indirect_targets',[]) for t in item['targets'])
    starts.update(o['address']+v for o in cfg.get('overlays',[]) for v in o.get('entry_offsets',[0]))
    for i in code.values():
        if i.control:
            if i.target is not None: starts.add(i.target)
            starts.add(i.pc+8)
    regs={0:0};loaded_from={};previous=None;skip=None;found=[]
    for pc,i in sorted(code.items()):
        if pc==skip:
            previous=pc;continue
        if previous is None or pc!=previous+4 or pc in starts:
            regs={0:0};loaded_from={}
        previous=pc
        if i.name in ('jr','jalr'):
            value=regs.get(i.rs)
            if value is not None:
                found.append({'site':pc,'target':value&0xffffffff,'register':i.rs})
        if i.control:
            regs={0:0};skip=pc+4
        else:
            update(regs,i)
    return found


def static_pointer_load_candidates(code,cfg,image):
    """Report initialized pointer loads preceding an indirect transfer.

    This is intentionally diagnostic-only.  File-backed data can be modified
    by the guest before the load, so an initial ELF value is useful batch
    triage evidence but never an indirect-target configuration or runtime
    transfer substitute.
    """
    configured_sites={item['site'] for item in cfg.get('indirect_targets',[])}
    starts={f['address'] for f in cfg.get('functions',[])}
    starts.update(t for item in cfg.get('indirect_targets',[]) for t in item['targets'])
    starts.update(o['address']+v for o in cfg.get('overlays',[]) for v in o.get('entry_offsets',[]))
    for i in code.values():
        if i.control:
            if i.target is not None: starts.add(i.target)
            starts.add(i.pc+8)
    regs={0:0};loaded_from={};previous=None;skip=None;found=[]
    for pc,i in sorted(code.items()):
        if pc==skip:
            previous=pc;continue
        if previous is None or pc!=previous+4 or pc in starts:
            regs={0:0};loaded_from={}
        previous=pc
        if i.name in ('lw','lwu'):
            base=regs.get(i.rs)
            if base is None:
                regs.pop(i.rt,None)
            else:
                address=(base+i.immediate)&0xffffffff
                try: value=image.file_word(address)
                except ValueError:
                    regs.pop(i.rt,None);loaded_from.pop(i.rt,None)
                else:
                    regs[i.rt]=sx32(value) if i.name=='lw' else value
                    loaded_from[i.rt]=address
        elif i.name in ('jr','jalr'):
            value=regs.get(i.rs)
            if value is not None and pc not in configured_sites:
                target=value&0xffffffff
                if image.executable(target):
                    found.append({'site':pc,'load':loaded_from.get(i.rs),'target':target,'register':i.rs})
        if i.control:
            regs={0:0};loaded_from={};skip=pc+4
        else:
            if i.name not in ('lw','lwu'):
                update(regs,i)
                # Only report a directly adjacent load/transfer sequence;
                # anything else is more likely to have a guest-visible
                # mutation or control-flow dependency we have not modeled.
                loaded_from={}
    return [item for item in found if item['load'] is not None]


def installed_table_candidates(code,cfg,image):
    """Find literal tables stored in object slot zero; never add CFG edges.

    A bounded run of executable pointers is a candidate, not proof of a table
    boundary or runtime type. Preserve the installing instruction as evidence.
    """
    starts={f['address'] for f in cfg.get('functions',[])}
    starts.update(t for item in cfg.get('indirect_targets',[]) for t in item['targets'])
    starts.update(o['address']+v for o in cfg.get('overlays',[]) for v in o.get('entry_offsets',[0]))
    for i in code.values():
        if i.control:
            if i.target is not None: starts.add(i.target)
            starts.add(i.pc+8)
    tables={}; regs={0:0}; previous=None; skip=None

    def consider(i):
        if i.name!='sw' or i.immediate!=0 or i.rs in (0,29):return
        value=regs.get(i.rt)
        if value is None:return
        address=value&0xffffffff
        if address%4:return
        if address not in tables:
            entries=[];terminated=False
            for offset in range(0,256,4):
                try: target=image.file_word(address+offset)
                except ValueError:
                    terminated=True;break
                if target==0 and offset<8:continue
                if not image.executable(target) or any(
                    r['start']<=target<r['end'] for r in cfg.get('data_ranges',[])):
                    terminated=True;break
                entries.append({'offset':offset,'target':target,'compiled':target in code})
            if len(entries)<2:return
            tables[address]={'table':address,'installations':[], 'entries':entries,
                             'scan_limit_reached':not terminated}
        tables[address]['installations'].append({'pc':i.pc,'object_register':i.rs})

    for pc,i in sorted(code.items()):
        if pc==skip:
            previous=pc;continue
        if previous is None or pc!=previous+4 or pc in starts:regs={0:0}
        previous=pc
        consider(i)
        if i.control:
            slot=code.get(pc+4)
            if slot and not slot.control and not i.likely:
                if i.name=='jal':regs[31]=sx32(pc+8)
                elif i.name=='jalr' and i.rd:regs.pop(i.rd,None)
                consider(slot)
            regs={0:0};skip=pc+4
        else:update(regs,i)
    return sorted(tables.values(),key=lambda t:t['table'])


def direct_member_candidates(code,cfg,image):
    """Read-only loaded (zero adjustment, -1 selector, target) descriptors.

    This profile comes from the original member-call helper; it is not a
    universal ABI inference. Only file-backed records referenced by code qualify.
    """
    starts={f['address'] for f in cfg.get('functions',[])}
    starts.update(t for item in cfg.get('indirect_targets',[]) for t in item['targets'])
    starts.update(o['address']+v for o in cfg.get('overlays',[]) for v in o.get('entry_offsets',[0]))
    for i in code.values():
        if i.control:
            if i.target is not None:starts.add(i.target)
            starts.add(i.pc+8)
    found={};regs={0:0};previous=None;skip=None

    def consider(i):
        if i.name not in ('lw','lwu','ld','lwc1'):return
        base=regs.get(i.rs)
        if base is None:return
        address=(base+i.immediate)&0xffffffff
        if address%4:return
        for offset in (0,4,8):
            record=address-offset
            if record<0:continue
            try: adjustment,selector,target=(image.file_word(record+n) for n in (0,4,8))
            except ValueError:continue
            if adjustment!=0 or selector!=0xffffffff or not image.executable(target):continue
            if any(r['start']<=target<r['end'] for r in cfg.get('data_ranges',[])):continue
            item=found.setdefault(record,{'record':record,'target':target,
                                         'compiled':target in code,'loads':[]})
            item['loads'].append({'pc':i.pc,'offset':offset,'mnemonic':i.name})

    for pc,i in sorted(code.items()):
        if pc==skip:
            previous=pc;continue
        if previous is None or pc!=previous+4 or pc in starts:regs={0:0}
        previous=pc;consider(i)
        if i.control:
            slot=code.get(pc+4)
            if slot and not slot.control and not i.likely:
                if i.name=='jal':regs[31]=sx32(pc+8)
                elif i.name=='jalr' and i.rd:regs.pop(i.rd,None)
                consider(slot)
            regs={0:0};skip=pc+4
        elif i.name!='lwc1':update(regs,i)
    return sorted(found.values(),key=lambda item:item['record'])


def static_syscalls(code,cfg):
    """Recover only syscall numbers proven in one straight-line basic block.

    The syscall ABI selects its service through v1.  This mirrors indirect
    target recovery's conservative rules: no value survives a branch, call,
    unknown instruction, function root, or non-contiguous instruction stream.
    It supplies discovery evidence only; runtime syscall handling stays
    register-selected.
    """
    starts={f['address'] for f in cfg.get('functions',[])}
    starts.update(t for item in cfg.get('indirect_targets',[]) for t in item['targets'])
    starts.update(o['address']+v for o in cfg.get('overlays',[]) for v in o.get('entry_offsets',[0]))
    for i in code.values():
        if i.control:
            if i.target is not None: starts.add(i.target)
            starts.add(i.pc+8)
    regs={0:0}; previous=None; skip=None; found=[]
    for pc,i in sorted(code.items()):
        if pc==skip:
            previous=pc; continue
        if previous is None or pc!=previous+4 or pc in starts: regs={0:0}
        previous=pc
        if i.name=='syscall':
            value=regs.get(3)
            if value is not None:
                number=value & 0xffff
                found.append({'site':pc,'number':number-0x10000 if number&0x8000 else number})
        if i.control:
            regs={0:0}; skip=pc+4
        else:
            update(regs,i)
    return found
