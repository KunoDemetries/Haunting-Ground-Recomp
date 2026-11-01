"""Ahead-of-time C++ effects for the checked IOP integer subset."""
import json


def u(value):return f'0x{value&0xffffffff:08x}u'


def operation(i):
    name=i.name;a=f's.r({i.rs})';b=f's.r({i.rt})';imm=u(i.immediate)
    dest=i.rd;expression=None
    if name=='lui':dest=i.rt;expression=u((i.word&65535)<<16)
    elif name=='addiu':dest=i.rt;expression=f'{a}+{imm}'
    elif name=='addi':dest=i.rt;expression=f's.checked_arithmetic({a},{imm},false)'
    elif name in ('add','sub'):expression=f's.checked_arithmetic({a},{b},{str(name=="sub").lower()})'
    elif name in ('andi','ori','xori'):
        dest=i.rt;expression=f'{a}{dict(andi="&",ori="|",xori="^")[name]}{u(i.word&65535)}'
    elif name in ('slti','sltiu'):
        dest=i.rt;expression=f'hg::iop_signed_less({a},{imm})' if name=='slti' else f'{a}<{imm}'
    elif name in ('addu','subu','and','or','xor','nor'):
        op={'addu':'+','subu':'-','and':'&','or':'|','xor':'^','nor':'|'}[name]
        expression=f'({a}{op}{b})'
        if name=='nor':expression='~'+expression
    elif name in ('slt','sltu'):
        expression=f'hg::iop_signed_less({a},{b})' if name=='slt' else f'{a}<{b}'
    elif name in ('sll','srl','sra','sllv','srlv','srav'):
        amount=f'({a}&31)' if name.endswith('v') else str(i.sa)
        if name.startswith('sra'):expression=f'hg::iop_sra({b},{amount})'
        else:expression=f'{b}{"<<" if name.startswith("sll") else ">>"}{amount}'
    elif name in ('mfhi','mflo'):expression='s.'+name[2:]
    elif name in ('mthi','mtlo'):return f's.{name[2:]}={a};'
    elif name in ('mult','multu'):return f's.multiply({a},{b},{str(name=="mult").lower()});'
    elif name in ('div','divu'):return f's.divide({a},{b},{str(name=="div").lower()});'
    elif name=='mfc0':return f's.load_register({i.rt},s.read_cop0({i.rd}));'
    elif name=='mtc0':return f's.write_cop0({i.rd},{b});'
    elif name=='break':return f'throw hg::IopFault(s.pc,"IOP BREAK trap code 0x{(i.word>>6)&0xfffff:x}");'
    elif name in ('lwl','lwr'):
        return f's.load_unaligned({i.rt},{a}+{imm},{str(name=="lwl").lower()});'
    elif name in ('swl','swr'):
        return f's.store_unaligned({a}+{imm},{b},{str(name=="swl").lower()});'
    elif name in ('lb','lbu','lh','lhu','lw'):
        size={'lb':1,'lbu':1,'lh':2,'lhu':2,'lw':4}[name]
        return f's.load_register({i.rt},s.load({a}+{imm},{size},{str(name in ("lb","lh")).lower()}));'
    elif name in ('sb','sh','sw'):
        return f's.store({a}+{imm},{dict(sb=1,sh=2,sw=4)[name]},{b});'
    if expression is not None:return f's.w({dest},{expression});'
    return f'throw hg::IopFault(s.pc,"unsupported IOP instruction {name}");'


def emit(code,blocked=None,linked=None,native=None,hooks=None):
    blocked=blocked or {}
    linked=linked or {}
    native=native or {}
    hooks=hooks or {}
    lines=['#include "hg/iop_image.hpp"',
           '// HG-DIAG-002: optional instruction history; SIF events remain recorded.',
           '#ifndef HG_IOP_INSTRUCTION_TRACE', '#define HG_IOP_INSTRUCTION_TRACE 1', '#endif',
           '#if defined(_MSC_VER)', '#define HG_IOP_NOINLINE __declspec(noinline)',
           '#elif defined(__GNUC__)', '#define HG_IOP_NOINLINE __attribute__((noinline))',
           '#else', '#define HG_IOP_NOINLINE', '#endif','namespace hg {','namespace {']
    page=None;pages=[]
    fallback='default:if constexpr(!HG_IOP_INSTRUCTION_TRACE)s.trace_sifman_pc();throw IopFault(s.pc,"IOP address has no static translation");'
    for pc in sorted(set(code)|set(blocked)|set(native)):
        if pc>>12!=page:
            if page is not None:lines += [fallback,'} } return true; }']
            page=pc>>12;pages.append(page)
            lines += [f'HG_IOP_NOINLINE bool iop_page_{page:x}(IopState& s,std::uint64_t& budget,bool cooperative) {{',
                      f'while(budget && (s.pc >> 12)=={u(page)}) {{ --budget;if constexpr(HG_IOP_INSTRUCTION_TRACE)s.trace_pc();switch(s.pc) {{']
        lines.append(f'case {u(pc)}: {{')
        # Existing diagnostic event sites, not execution substitutes or roots.
        if pc in (0x9c610,0x9c838):
            lines.append('if constexpr(!HG_IOP_INSTRUCTION_TRACE)s.trace_sifman_pc();')
        if pc in hooks:
            if hooks[pc]!='prepare_buffered_module' or pc not in code or pc in native:raise ValueError('invalid IOP entry observation')
            lines.append('s.prepare_buffered_module(s.r(4),s.r(5),s.r(6));')
        if pc in native:
            handler=native[pc]
            if handler in ('flush_instruction_cache','flush_data_cache'):
                lines.append(f'const auto target=s.r(31);s.flush_cache({str(handler=="flush_instruction_cache").lower()});s.finish_instruction();s.pc=target;break;}}')
            elif handler in ('set_dma_madr','get_dma_madr','set_dma_bcr','set_dma_chcr','get_dma_chcr'):
                offset={'madr':0,'bcr':4,'chcr':8}[handler.split('_')[-1]]
                dma_operation=f's.store(address,4,s.r(5));' if handler.startswith('set') else 's.w(2,s.load(address,4,false));'
                lines.append(f'const auto target=s.r(31);const auto address=s.dma_register_address(s.r(4),{offset});{dma_operation}s.finish_instruction();s.pc=target;break;}}')
            elif handler=='set_dma_priority_control':
                lines.append('const auto target=s.r(31);s.dmac.dpcr=s.r(4);s.finish_instruction();s.pc=target;break;}')
            elif handler=='get_dma_priority_control':
                lines.append('const auto target=s.r(31);const auto result=s.dmac.dpcr;s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler=='set_dma_priority':
                lines.append('const auto target=s.r(31);s.set_dma_priority(s.r(4),s.r(5));s.finish_instruction();s.pc=target;break;}')
            elif handler=='set_slice_dma':
                lines.append('const auto target=s.r(31);const auto result=s.set_slice_dma(s.r(4),s.r(5),s.r(6),s.r(7),s.load(s.r(29)+16,4,false));s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler=='start_dma':
                lines.append('const auto target=s.r(31);s.start_dma(s.r(4));s.finish_instruction();s.pc=target;break;}')
            elif handler in ('enable_dma_channel','disable_dma_channel'):
                enabled='true' if handler=='enable_dma_channel' else 'false'
                lines.append(f'const auto target=s.r(31);s.set_dma_channel_enabled(s.r(4),{enabled});s.finish_instruction();s.pc=target;break;}}')
            elif handler=='delete_heap':
                lines.append('const auto target=s.r(31);s.delete_heap(s.r(4));s.finish_instruction();s.pc=target;break;}')
            elif handler in ('create_heap','alloc_heap_memory','free_heap_memory','heap_total_free_size'):
                args='s.r(4)' if handler=='heap_total_free_size' else 's.r(4),s.r(5)'
                lines.append(f'const auto target=s.r(31);const auto result=s.{handler}({args});s.finish_instruction();s.w(2,result);s.pc=target;break;}}')
            elif handler=='enable_cpu_interrupts':
                lines.append('const auto target=s.r(31);s.enable_cpu_interrupts();s.finish_instruction();s.pc=target;break;}')
            elif handler=='disable_cpu_interrupts':
                lines.append('const auto target=s.r(31);const auto result=s.disable_cpu_interrupts();s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler in ('set_new_context_callback','set_should_preempt_callback'):
                lines.append(f'const auto target=s.r(31);s.{handler}(s.r(4));s.finish_instruction();s.pc=target;break;}}')
            elif handler in ('set_secrman_mc_command_callback','set_secrman_mc_devid_callback'):
                command='true' if handler=='set_secrman_mc_command_callback' else 'false'
                lines.append(f'const auto target=s.r(31);s.set_secrman_callback({command},s.r(4));s.finish_instruction();s.pc=target;break;}}')
            elif handler=='disable_interrupt':
                lines.append('const auto target=s.r(31);const auto result=s.disable_interrupt(s.r(4),s.r(5));s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler in ('invoke_in_kmode','syscall_invoke_in_kmode'):
                lines.append('const auto target=s.r(31),function=s.r(4);if(!function || function%4 || function>=s.ram.size())throw IopFault(s.pc,"invalid native INTRMAN kernel callback");const auto a0=s.r(5),a1=s.r(6),a2=s.r(7);s.w(4,a0);s.w(5,a1);s.w(6,a2);s.w(31,target);s.finish_instruction();s.pc=function;break;}')
            elif handler in ('threadman_reschedule','syscall_threadman_reschedule'):
                lines.append('const auto target=s.r(31);s.threadman_reschedule(s.r(7));s.finish_instruction();s.pc=target;break;}')
            elif handler=='wait_semaphore':
                lines.append('const auto target=s.r(31);if(!s.wait_semaphore(s.r(4)))return false;s.finish_instruction();s.w(2,0);s.pc=target;break;}')
            elif handler=='wait_event_flag':
                lines.append('const auto target=s.r(31);if(!s.wait_event_flag(s.r(4),s.r(5),s.r(6),s.r(7)))return false;s.finish_instruction();s.w(2,0);s.pc=target;break;}')
            elif handler in ('cdvd_open','cdvd_close','cdvd_read','cdvd_lseek'):
                args={'cdvd_open':'s.r(4),s.r(5),s.r(6),s.r(7)','cdvd_close':'s.r(4)',
                      'cdvd_read':'s.r(4),s.r(5),s.r(6)','cdvd_lseek':'s.r(4),s.r(5),s.r(6)'}[handler]
                lines.append(f'const auto target=s.r(31);const auto result=s.{handler}({args});s.finish_instruction();s.w(2,result);s.pc=target;break;}}')
            elif handler=='alloc_system_memory':
                lines.append('const auto target=s.r(31);const auto result=s.alloc_system_memory(s.r(4),s.r(5),s.r(6));s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler=='free_system_memory':
                lines.append('const auto target=s.r(31);const auto result=s.free_system_memory(s.r(4));s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler=='link_static_iop_module':
                lines.append('const auto target=s.r(31);const auto address=s.r(4),size=s.r(5);const auto result=s.link_static_iop_module(address,size);link_compiled_iop_module(s,address,size);s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler=='wait_vblank_start':
                lines.append('const auto target=s.r(31);if(!s.wait_vblank_start())return false;s.finish_instruction();s.w(2,0);s.pc=target;break;}')
            elif handler=='exit_thread':
                lines.append('s.exit_thread();s.finish_instruction();break;}')
            elif handler=='authenticate_card':
                lines.append('const auto target=s.r(31);const auto result=s.authenticate_card(s.r(4),s.r(5),s.r(6));s.finish_instruction();s.w(2,result);s.pc=target;break;}')
            elif handler in ('delay_thread','change_thread_priority','refer_thread_status','refer_event_status','irefer_event_status','create_semaphore','signal_semaphore','print_console','get_thread_id','sleep_thread','wakeup_thread','create_thread','delete_thread','start_thread','get_system_status_flag','create_event_flag','set_event_flag','clear_event_flag','query_interrupt_context','suspend_interrupts','resume_interrupts','register_interrupt','release_interrupt','enable_interrupt','wait_vblank_end'):
                args='s.r(4),s.r(5),s.r(6),s.r(7)' if handler=='register_interrupt' else 's.r(4)'
                if handler in ('get_thread_id','sleep_thread','get_system_status_flag','query_interrupt_context','wait_vblank_end'):args=''
                elif handler in ('change_thread_priority','refer_thread_status','refer_event_status','irefer_event_status','set_event_flag','clear_event_flag','start_thread'):args='s.r(4),s.r(5)'
                lines.append(f'const auto target=s.r(31);const auto result=s.{handler}({args});s.finish_instruction();s.w(2,result);s.pc=target;break;}}')
            else:raise ValueError('unknown IOP native service')
            lines[-1]=lines[-1].replace('break;}','if(cooperative)return false;break;}')
            continue
        if pc in linked:
            target,slot_word=linked[pc]
            jump=0x08000000|((target>>2)&0x03ffffff)
            lines.append(f'if(s.load({u(pc)},4,false)!={u(jump)} || s.load({u(pc+4)},4,false)!={u(slot_word)}) throw IopFault(s.pc,"IOP import has not been linked to its compiled provider");')
            from .iop_decode import decode
            lines.append(f's.finish_instruction();s.pc={u(pc+4)};')
            lines.append(operation(decode(pc+4,slot_word)))
            lines.append(f's.finish_instruction();s.pc={u(target)};break;}}');continue
        if pc in blocked:
            lines.append(f'throw IopFault(s.pc,{json.dumps(blocked[pc])});');lines.append('}');continue
        i=code[pc]
        if i.control:
            if i.name in ('jr','jalr'):target=f's.r({i.rs})'
            elif i.name in ('j','jal'):target=u(i.target)
            else:
                a,b=f's.r({i.rs})',f's.r({i.rt})'
                condition={'beq':f'{a}=={b}','bne':f'{a}!={b}',
                           'blez':f'!hg::iop_signed_less(0,{a})','bgtz':f'hg::iop_signed_less(0,{a})',
                           'bltz':f'hg::iop_signed_less({a},0)','bgez':f'!hg::iop_signed_less({a},0)'}[i.name]
                target=f'({condition})?{u(i.target)}:{u(pc+8)}'
            lines.append(f'const std::uint32_t target={target};')
            if i.name in ('jal','jalr'):lines.append(f's.w({31 if i.name=="jal" else i.rd},{u(pc+8)});')
            lines.append(f's.finish_instruction();s.pc={u(pc+4)};')
            slot=code.get(pc+4)
            if not slot or slot.control or pc+4 in blocked:
                lines.append('throw IopFault(s.pc,"invalid IOP branch delay slot");')
            else:
                lines.append(operation(slot));lines.append('s.finish_instruction();s.pc=target;')
                if i.name=='jr' and i.rs==31:lines.append('if(cooperative && (target==0x1f0000u || target==0x1f0020u))return false;')
                lines.append('break;')
        else:
            lines.append(operation(i));lines.append(f's.finish_instruction();s.pc={u(pc+4)};break;')
        lines.append('}')
    if page is not None:lines += [fallback,'} } return true; }']
    lines += ['void run_iop_pages(IopState& s,std::uint64_t budget,bool cooperative) {',
              'if constexpr(!HG_IOP_INSTRUCTION_TRACE)if(s.trace_watch_count)throw IopFault(s.pc,"IOP instruction history is disabled in this AOT build");',
              'while(budget) {switch(s.pc >> 12) {']
    lines += [f'case {u(page)}: if(!iop_page_{page:x}(s,budget,cooperative))return;break;' for page in pages]
    lines += ['default:--budget;if constexpr(HG_IOP_INSTRUCTION_TRACE)s.trace_pc();else s.trace_sifman_pc();throw IopFault(s.pc,"IOP address has no static translation");','}}','}','}',
              'bool compiled_iop_instruction_trace(){return HG_IOP_INSTRUCTION_TRACE!=0;}',
              'void run_iop(IopState& s,std::uint64_t budget){run_iop_pages(s,budget,false);}',
              'void run_iop_burst(IopState& s,std::uint64_t budget){run_iop_pages(s,budget,true);}','}']
    return '\n'.join(lines)+'\n'
