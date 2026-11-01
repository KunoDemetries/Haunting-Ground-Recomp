import hashlib
import struct
import tempfile
from pathlib import Path
import unittest
from hgtool.elf import Elf
from hgtool.decode import decode
from hgtool.discover import discover
from hgtool.emit import emit,emit_shards,straight
from hgtool.config import load


def make_elf(words):
    raw = struct.pack('<' + 'I' * len(words), *words)
    h = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + bytes(9),
                    2, 8, 1, 0x1000, 52, 0, 0, 52, 32, 1, 40, 0, 0)
    return h + struct.pack('<8I', 1, 84, 0x1000, 0x1000, len(raw), len(raw) + 16, 5, 4) + raw


class Tests(unittest.TestCase):
    def test_pref_all_fields_are_cache_neutral(self):
        from hgtool.constants import update
        offsets = (0, 1, 0x7fff, -1, -0x8000)
        for base in range(32):
            for hint in range(32):
                for offset in offsets:
                    word = 51 << 26 | base << 21 | hint << 16 | (offset & 0xffff)
                    instruction = decode(0x227e90, word)
                    self.assertEqual(instruction.name, 'pref')
                    self.assertEqual((instruction.rs, instruction.rt, instruction.immediate),
                                     (base, hint, offset))
                    self.assertEqual(straight(instruction),
                                     '; // Architecturally defined cache-neutral PREF hint.')
                    regs = {0: 0, 3: 0x12345678, 31: 0xffffffff81234567}
                    update(regs, instruction)
                    self.assertEqual(regs, {0: 0, 3: 0x12345678, 31: 0xffffffff81234567})

    def test_cache_dxin_all_bases_are_explicit_and_state_neutral(self):
        from hgtool.constants import update
        offsets = (0, 1, 0x7fff, -1, -0x8000)
        for base in range(32):
            for offset in offsets:
                word = 47 << 26 | base << 21 | 22 << 16 | (offset & 0xffff)
                instruction = decode(0x26cb74, word)
                self.assertEqual(instruction.name, 'cache_dxin')
                expected_base = 'std::uint64_t(0)' if base == 0 else f's.gpr[{base}].lo'
                expected_offset = offset & 0xffffffffffffffff
                self.assertEqual(straight(instruction),
                                 f's.cache_index_invalidate(std::uint32_t({expected_base}+{expected_offset}ull));')
                regs = {0: 0, 3: 0x12345678, 31: 0xffffffff81234567}
                update(regs, instruction)
                self.assertEqual(regs, {0: 0, 3: 0x12345678, 31: 0xffffffff81234567})

    def test_vu_macro_conversions(self):
        for base,prefix in ((0x13c,'vitof'),(0x17c,'vftoi')):
            for selector,scale in enumerate((0,4,12,15)):
                for mask in range(16):
                    for source,target in ((0,0),(0,31),(31,0),(7,7),(7,19)):
                        word=18<<26 | (16|mask)<<21 | target<<16 | source<<11 | base | selector
                        instruction=decode(0,word)
                        self.assertEqual(instruction.name,prefix+str(scale))
                        self.assertEqual(straight(instruction),
                            f's.vu_convert({target}, {source}, {mask}, {scale}, {str(prefix=="vitof").lower()});')
                        self.assertEqual(decode(0,word & ~(1<<25)).name,'unsupported')

    def test_divide_pipeline_one_encoding(self):
        for function,name in [(26,'div1'),(27,'divu1')]:
            word=28<<26 | 3<<21 | 6<<16 | function
            self.assertEqual(decode(0,word).name,name)
            self.assertEqual(decode(0,word | 1<<11).name,'unsupported')
            self.assertEqual(decode(0,word | 1<<6).name,'unsupported')

    def test_oracle_finds_only_bounded_gettoc_descriptors(self):
        from oracle_capture import find_iop_toc_dma_descriptors,IOP_TOC_DMA_BCR,IOP_TOC_DMA_CHCR
        data=bytearray(48)
        struct.pack_into('<III',data,8,0xc3464,IOP_TOC_DMA_BCR,IOP_TOC_DMA_CHCR)
        struct.pack_into('<III',data,24,0x1ffffc,IOP_TOC_DMA_BCR,IOP_TOC_DMA_CHCR)
        self.assertEqual(find_iop_toc_dma_descriptors(data,0x10000000),
                         [{'host_address':0x10000008,'madr':0xc3464}])

    def test_oracle_rejects_known_precommand_toc_hash(self):
        from oracle_capture import toc_candidate_status
        self.assertEqual(
            toc_candidate_status('b54e0963af0a5cfe515672bb81c7c4e3abd35f4d3a523f2aab2404846bd55e90'),
            'rejected: observed pre-GetToc command buffer; do not use as a TOC record')
        for digest in ('1d830c8af4ff60b1ec36350ea25d99c4247d5d040d8d2c9acbe28011ebb9039e',
                       '6602314dc16454765b397f53e52891c3ab6f3ceeb6c7e525e3212fbdd476710a',
                       'd6d66cf27e7968011610ffc670b47a06eb98e417a24e1428420dbe994397a503',
                       'e3ee9ebdb8b690c1cf2a63af53f016cdbcbd0a2f03f5848ee7dafb1f3e140c54'):
            self.assertTrue(toc_candidate_status(digest).startswith('rejected:'))
        self.assertEqual(toc_candidate_status('00'*32),
                         'candidate; do not use without independent command-stage provenance')

    def test_elf_bounds_and_bss(self):
        e = Elf(make_elf([0]))
        self.assertEqual(e.word(0x1000), 0)
        for a in (0x1001, 0x1004, 0xfffffffc):
            with self.assertRaises(ValueError): e.word(a)
        with self.assertRaises(ValueError): Elf(make_elf([0])[:-1])

    def test_reject_wrong_arch(self):
        b = bytearray(make_elf([0])); b[18] = 62
        with self.assertRaises(ValueError): Elf(bytes(b))

    def test_decoding(self):
        self.assertEqual(decode(0x1000, 0x1000ffff).target, 0x1000)
        self.assertEqual(decode(0x8ffffffc, 0x08000000).target, 0x90000000)
        self.assertEqual(decode(0, 28 << 26 | 16 << 6 | 40).name, 'padduw')
        self.assertEqual(decode(0, 28 << 26 | 24 << 6 | 40).name, 'paddub')
        self.assertEqual(decode(0, 28 << 26 | 20 << 6 | 40).name, 'padduh')
        self.assertEqual(decode(0, 1 << 21 | 2 << 16 | 3 << 11 | 34).name, 'sub')
        self.assertEqual(decode(0, 18 << 26 | 1 << 21 | 4 << 16 | 28 << 11).name, 'qmfc2')
        self.assertEqual(decode(0, 18 << 26 | 5 << 21 | 4 << 16 | 28 << 11 | 1).name, 'qmtc2')
        self.assertEqual(decode(0, 18 << 26 | 0x1a << 21 | 9 << 16 | 7 << 11 | 0x1fd).name, 'vabs')
        self.assertEqual(decode(0, 18 << 26 | 0x1a << 21 | 9 << 16 | 7 << 11 | 0x33c).name, 'vmove')
        self.assertEqual(decode(0, 18 << 26 | 0x1f << 21 | 9 << 16 | 7 << 11 | 0x33d).name, 'vmr32')
        self.assertEqual(decode(0, 18 << 26 | 0x1e << 21 | 4 << 16 | 4 << 11 | 5 << 6 | 0x2a).name, 'vmul')
        self.assertEqual(decode(0, 18 << 26 | 0x18 << 21 | 5 << 16 | 5 << 11 | 5 << 6 | 1).name, 'vaddbc')
        self.assertEqual(decode(0, 18 << 26 | 16 << 21 | 5 << 16 | 0x3bd).name, 'vsqrt')
        self.assertEqual(decode(0, 0x4a0003bf).name, 'vwaitq')
        self.assertEqual(decode(0, 0x4a0002ff).name, 'vnop')
        self.assertEqual(decode(0, 18 << 26 | 19 << 21 | 5 << 16 | 0x3bc).name, 'vdiv')
        self.assertEqual(decode(0, 18 << 26 | 30 << 21 | 5 << 16 | 4 << 11 | 4 << 6 | 0x18).name, 'vmulbc')
        self.assertEqual(decode(0, 18 << 26 | 17 << 21 | 7 << 16 | 0 << 11 | 7 << 6 | 0x04).name, 'vsubbc')
        self.assertEqual(straight(decode(0, 18 << 26 | 17 << 21 | 7 << 16 | 0 << 11 | 7 << 6 | 0x04)),
                         's.vu_arithmetic(7, 0, 7, 1, false, 8);')
        self.assertEqual(decode(0, 18 << 26 | 30 << 21 | 5 << 16 | 4 << 11 | 0x2fe).name, 'vopmula')
        self.assertEqual(decode(0, 18 << 26 | 30 << 21 | 4 << 16 | 5 << 11 | 6 << 6 | 0x2e).name, 'vopmsub')
        self.assertEqual(decode(0, 62 << 26 | 6 << 21 | 16 << 16).name, 'sqc2')
        self.assertEqual(decode(0, 47 << 26 | 16 << 16).name, 'cache_dxltg')
        self.assertEqual(decode(0, 47 << 26 | 20 << 16).name, 'cache_dxwbin')
        self.assertEqual(decode(0, 47 << 26 | 22 << 16).name, 'cache_dxin')
        self.assertEqual(decode(0, 18 << 26 | 2 << 21).name, 'cfc2')
        self.assertEqual(decode(0, 18 << 26 | 6 << 21).name, 'ctc2')
        for word in (18 << 26 | 2 << 21 | 2, 18 << 26 | 6 << 21 | 2, 47 << 26 | 17 << 16):
            self.assertEqual(decode(0, word).name, 'unsupported')
        self.assertEqual(decode(0, 54 << 26 | 6 << 21 | 16 << 16).name, 'lqc2')
        self.assertEqual(decode(0, 28 << 26 | 19 << 6 | 9).name, 'pxor')
        self.assertEqual(decode(0, 28 << 26 | 4 << 11 | 16).name, 'mfhi1')
        self.assertEqual(decode(0, 28 << 26 | 5 << 11 | 18).name, 'mflo1')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 1 << 6 | 8).name, 'psubw')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 5 << 6 | 8).name, 'psubh')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 25 << 6 | 8).name, 'psubsb')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 21 << 6 | 8).name, 'psubsh')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 17 << 6 | 8).name, 'psubsw')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 24 << 6 | 8).name, 'paddsb')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 20 << 6 | 8).name, 'paddsh')
        self.assertEqual(decode(0, 28 << 26 | 1 << 21 | 2 << 16 | 6 << 11 | 16 << 6 | 8).name, 'paddsw')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 3 << 6 | 1).name, 'sub.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 3 << 16 | 2 << 11 | 7 << 6 | 28).name, 'madd.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 4 << 6 | 2).name, 'mul.s')
        self.assertEqual(decode(0, 16 << 26 | 4 << 21 | 1 << 16 | 6 << 11).name, 'mtc0_wired')
        self.assertEqual(decode(0, 16 << 26 | 2 << 16 | 28 << 11).name, 'mfc0_tag_lo')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 5 << 6 | 3).name, 'div.s')
        self.assertEqual(decode(0, 17 << 26 | 20 << 21 | 1 << 11 | 6 << 6 | 32).name, 'cvt.s.w')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 1 << 11 | 7 << 6 | 6).name, 'mov.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 1 << 11 | 8 << 6 | 7).name, 'neg.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 50).name, 'c.eq.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 4 << 16 | 3 << 6 | 4).name, 'sqrt.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 4 << 16 | 1 << 11 | 3 << 6 | 4).name, 'unsupported')
        self.assertEqual(decode(0, 17 << 26 | 8 << 21 | 1 << 16 | 1).name, 'bc1t')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 1 << 11 | 9 << 6 | 36).name, 'cvt.w.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 26).name, 'mula.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 10 << 6 | 29).name, 'msub.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 30).name, 'madda.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 31).name, 'msuba.s')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 1 << 6 | 26).name, 'unsupported')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 1 << 6 | 30).name, 'unsupported')
        self.assertEqual(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 1 << 6 | 31).name, 'unsupported')
        self.assertEqual(straight(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 26)),
                         's.mula_float(1, 2);')
        self.assertEqual(straight(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 5 << 6 | 29)),
                         's.msub_float(1, 2, 5);')
        self.assertEqual(straight(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 30)),
                         's.madda_float(1, 2);')
        self.assertEqual(straight(decode(0, 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 31)),
                         's.msuba_float(1, 2);')
        self.assertEqual(decode(0, 0x2402ffff).immediate, -1)

    def test_packed_word_extension_encodings(self):
        from hgtool.emit import straight
        for function,name in ((8,'pextlw'),(40,'pextuw')):
            for register in range(32):
                for source,other,destination in ((register,register,register),
                        (0,register,31),(register,0,31),(31,register,0),(register,31,31)):
                    word=28<<26 | source<<21 | other<<16 | destination<<11 | 18<<6 | function
                    instruction=decode(0x22000,word)
                    self.assertEqual(instruction.name,name)
                    self.assertEqual((instruction.rs,instruction.rt,instruction.rd),
                                     (source,other,destination))
                    self.assertEqual(straight(instruction),
                        f's.extend_words({destination},{source},{other},{str(function==40).lower()});')
            # Nearby packed operations must not be mistaken for these two forms.
            for subcode in (17,19,26):
                self.assertNotIn(decode(0,28<<26 | 3<<21 | 6<<16 | 7<<11 | subcode<<6 | function).name,
                                 ('pextlw','pextuw'))
        for function in (9,41):
            self.assertNotIn(decode(0,28<<26 | 3<<21 | 6<<16 | 7<<11 | 18<<6 | function).name,
                             ('pextlw','pextuw'))

    def test_vu_broadcast_product_encodings(self):
        for mask in range(16):
            for bc in range(4):
                base = 18 << 26 | (16 | mask) << 21 | 13 << 16 | 11 << 11
                for low, name, operation in (
                    (0x1bc | bc, 'vmulabc', 'multiply_accumulator'),
                    (0x0bc | bc, 'vmaddabc', 'add_accumulator'),
                    (19 << 6 | 8 | bc, 'vmaddbc', 'add_vector'),
                ):
                    instruction = decode(0x1000, base | low)
                    self.assertEqual(instruction.name, name)
                    self.assertEqual((instruction.rd, instruction.rt, instruction.rs & 15),
                                     (11, 13, mask))
                    generated = emit({0x1000: instruction}, 'synthetic')
                    self.assertIn('s.vu_broadcast_product(', generated)
                    self.assertIn('hg::VuBroadcastProduct::' + operation, generated)
                    self.assertNotIn('unsupported instruction', generated)
                    self.assertEqual(decode(0, (base | low) & ~(1 << 25)).name, 'unsupported')
        for low in (0x1b8, 0x1b9, 0x1ba, 0x1bb, 0x0b8, 0x0b9, 0x0ba, 0x0bb,
                    19 << 6 | 0x0c, 19 << 6 | 0x0d, 19 << 6 | 0x0e, 19 << 6 | 0x0f):
            self.assertEqual(decode(0, 18 << 26 | 31 << 21 | 13 << 16 | 11 << 11 | low).name,
                             'unsupported')

    def test_vu_broadcast_subtract_encodings(self):
        from hgtool.emit import straight
        for mask in range(16):
            for bc in range(4):
                word = 18 << 26 | (16 | mask) << 21 | 7 << 16 | 5 << 11 | 9 << 6 | 0x04 | bc
                instruction = decode(0x1000, word)
                self.assertEqual(instruction.name, 'vsubbc')
                self.assertEqual((instruction.sa, instruction.rd, instruction.rt, instruction.rs & 15),
                                 (9, 5, 7, mask))
                self.assertEqual(straight(instruction),
                                 f's.vu_arithmetic(9, 5, 7, {mask}, false, {8+bc});')
                self.assertEqual(decode(0, word & ~(1 << 25)).name, 'unsupported')

    def test_ee_less_equal_encoding(self):
        word = 17 << 26 | 16 << 21 | 2 << 16 | 1 << 11 | 54
        self.assertEqual(decode(0, word).name, 'c.le.s')
        self.assertEqual(decode(0, word | 1 << 6).name, 'unsupported')

    def test_cfg_skips_data_after_jump(self):
        e = Elf(make_elf([0x08000404, 0, 0xffffffff, 0xffffffff, 0x0000000c]))
        code, report = discover(e, {})
        self.assertEqual(set(code), {0x1000, 0x1004, 0x1010})
        self.assertEqual(report['issues'][0]['reason'], 'syscall')

    def test_likely_trap_slot_keeps_annulled_path(self):
        e=Elf(make_elf([0x50200001,0x000001cd,0x0000000c]))
        code,report=discover(e,{})
        self.assertIn(0x1008,code)
        self.assertNotIn('unsupported delay slot',str(report))
        self.assertIn(0x1004,code)
        self.assertNotIn(0x100c,code)

    def test_break_emits_explicit_trap(self):
        code={0x1000: decode(0x1000, 0x000001cd)}
        translated=emit(code, 'synthetic')
        self.assertIn('s.trace_pc()', translated)
        self.assertIn('BREAK trap code 0x7', translated)
        self.assertNotIn('unsupported delay slot', translated)

    def test_emit_shards_preserves_page_bodies_and_dispatch(self):
        code={pc:decode(pc,0) for pc in (0x1000,0x21000)}
        main,shards=emit_shards(code,'synthetic',pages_per_shard=16)
        self.assertEqual(set(shards),{
            'translated-shard-0000.cpp','translated-shard-0002.cpp'})
        self.assertIn('bool page_1(hg::State&,std::uint64_t&,bool);',main)
        self.assertIn('bool page_21(hg::State&,std::uint64_t&,bool);',main)
        self.assertIn('case 0x1u: if(!page_1(s,block_budget,cooperative))return;',main)
        self.assertNotIn('step_1000:',main)
        self.assertIn('step_1000:',shards['translated-shard-0000.cpp'])
        self.assertIn('step_21000:',shards['translated-shard-0002.cpp'])

    def test_dma_poll_rejects_mutation_and_wrong_mask(self):
        from hgtool.emit import dma_poll_loops
        words=[0x8c820000,0x00021202,0x30420001,0x1440fffc,0]
        code={0x1000+n*4:decode(0x1000+n*4,w) for n,w in enumerate(words)}
        self.assertEqual(list(dma_poll_loops(code)),[0x1000])
        for offset,word in [(0,0xac820000),(8,0x30420002),(16,0x24030001),(4,0x00021a02)]:
            changed=dict(code);changed[0x1000+offset]=decode(0x1000+offset,word)
            self.assertEqual(dma_poll_loops(changed),{})

    def test_poll_recognizer_excludes_writes_and_changing_base(self):
        from hgtool.emit import poll_loops
        words=[0,0x90830000,0x1060fffd,0]
        code={0x1000+n*4:decode(0x1000+n*4,w) for n,w in enumerate(words)}
        self.assertEqual(list(poll_loops(code)),[0x1000])
        for replacement in (0xac820000,0x24420001):
            changed=dict(code);changed[0x1000]=decode(0x1000,replacement)
            self.assertEqual(poll_loops(changed),{})
        code[0x1004]=decode(0x1004,0x90630000)
        self.assertEqual(poll_loops(code),{})

    def test_eret_has_no_delay_slot(self):
        e=Elf(make_elf([0x42000018,0xffffffff,0x0000000c]))
        code,_=discover(e,{'indirect_targets':[{'site':0x1000,'targets':[0x1008]}]})
        self.assertEqual(set(code),{0x1000,0x1008})

    def test_config_indirect_pointer_table(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder); raw=make_elf([0x0040f809,0,12,0x1008])
            (path/'test.elf').write_bytes(raw)
            config='schema_version=1\n[image]\npath="test.elf"\nsha256="'+hashlib.sha256(raw).hexdigest()+'"\n'
            config+='[[indirect_targets]]\nsite=0x1000\ntable_start=0x100c\ntable_end=0x1010\n'
            (path/'test.toml').write_text(config)
            cfg,_=load(path/'test.toml',Elf)
            self.assertEqual(cfg['indirect_targets'][0]['targets'],[0x1008])
            (path/'test.toml').write_text(config.replace('table_end=0x1010','table_end=0x1014'))
            with self.assertRaises(ValueError): load(path/'test.toml',Elf)

    def test_callback_constants_include_call_delay_slot(self):
        e=Elf(make_elf([0x3c050000,0x0c000408,0x24a51030,13,13,13,13,13,
                       0x00a0a82d,0x02a0f809,0,13,0x03e00008,0]))
        binding={'callee':0x1020,'argument':5,'site':0x1024}
        cfg={'callback_bindings':[binding]}
        code,report=discover(e,cfg)
        self.assertIn(0x1030,code)
        self.assertEqual(report['callback_evidence'][0]['target'],0x1030)
        self.assertEqual(report['callback_evidence'][0]['caller'],0x1004)
        # A second entry can jump directly into the call with unknown a1.
        # Conservatively discard the preceding LUI constant at the join.
        cfg['functions']=[{'name':'join','address':0x1004}]
        code,report=discover(e,cfg)
        self.assertNotIn(0x1030,code)
        self.assertEqual(report['callback_evidence'],[])

    def test_callback_unknown_load_kills_stale_constant(self):
        from hgtool.constants import callback_targets
        words=[0x24051030,0x8c250000,0x0c000408,0]
        code={0x1000+n*4:decode(0x1000+n*4,w) for n,w in enumerate(words)}
        cfg={'callback_bindings':[{'callee':0x1020,'argument':5,'site':0x1024}]}
        self.assertEqual(callback_targets(code,cfg),[])

    def test_callback_calls_clobber_constants(self):
        from hgtool.constants import callback_targets
        words=[0x24051030,0x0c000410,0,0x0c000408,0]
        code={0x1000+n*4:decode(0x1000+n*4,w) for n,w in enumerate(words)}
        cfg={'callback_bindings':[{'callee':0x1020,'argument':5,'site':0x1024}]}
        self.assertEqual(callback_targets(code,cfg),[])

    def test_callback_unrelated_load_preserves_pointer(self):
        from hgtool.constants import callback_targets
        cfg={'callback_bindings':[{'callee':0x1020,'argument':5,'site':0x1024}]}
        for opcode in (32,36,33,37,35,39,55,30,34,38,26,27):
            for target in (4,5):
                words=[0x24051030,(opcode<<26)|(1<<21)|(target<<16),0x0c000408,0]
                code={0x1000+n*4:decode(0x1000+n*4,w) for n,w in enumerate(words)}
                evidence=callback_targets(code,cfg)
                self.assertEqual([v['target'] for v in evidence],[0x1030] if target==4 else [])

    def test_indirect_manual_targets(self):
        e = Elf(make_elf([0x00800008, 0, 0x0000000c]))
        code, report = discover(e, {'indirect_targets':[{'site':0x1000, 'targets':[0x1008]}]})
        self.assertIn(0x1008, code)
        self.assertNotIn('unresolved indirect transfer', str(report))

    def test_static_indirect_target(self):
        # LUI/ORI prove jalr's target in a single straight-line block.
        e=Elf(make_elf([0x3c020000,0x34421020,0x0040f809,0,13,13,13,13,0x03e00008,0]))
        code,report=discover(e,{})
        self.assertIn(0x1020,code)
        self.assertEqual(report['static_indirect_evidence'],[{'site':0x1008,'target':0x1020,'register':2}])

    def test_static_indirect_unknown_load_is_not_target(self):
        e=Elf(make_elf([0x3c020000,0x8c421020,0x0040f809,0,13]))
        _,report=discover(e,{})
        self.assertIn('unresolved indirect transfer',str(report))
        self.assertEqual(report['static_indirect_evidence'],[])

    def test_file_pointer_load_is_triage_evidence_not_a_target(self):
        # A directly adjacent load/jalr can report the initialized image value,
        # but discovery must preserve the runtime indirect-transfer boundary.
        words=[0x8c021010,0x0040f809,0,0,0x1020,13,13,13,0x03e00008,0]
        e=Elf(make_elf(words)); code,report=discover(e,{})
        self.assertNotIn(0x1020,code)
        self.assertIn('unresolved indirect transfer',str(report))
        self.assertEqual(report['static_pointer_load_evidence'],
                         [{'site':0x1004,'load':0x1010,'target':0x1020,'register':2}])

    def test_configured_file_pointer_site_is_not_requeued(self):
        words=[0x8c021010,0x0040f809,0,0,0x1020,13,13,13,0x03e00008,0]
        e=Elf(make_elf(words))
        _,report=discover(e,{'indirect_targets':[{'site':0x1004,'targets':[0x1020]}]})
        self.assertEqual(report['static_pointer_load_evidence'],[])

    def test_installed_table_in_delay_slot_is_evidence_only(self):
        # Constructor stores a literal table in the call delay slot. Both
        # methods are outside discovery; finding their pointers must not root them.
        words=[0x24021020,0x0c000407,0xac820000,13,13,13,13,13,
               0,0,0x1034,0x103c,0xffffffff,0x03e00008,0,0x03e00008,0]
        code,report=discover(Elf(make_elf(words)),{})
        self.assertNotIn(0x1034,code)
        self.assertNotIn(0x103c,code)
        self.assertEqual(report['installed_table_evidence'],[{
            'table':0x1020,'installations':[{'pc':0x1008,'object_register':4}],
            'entries':[{'offset':8,'target':0x1034,'compiled':False},
                       {'offset':12,'target':0x103c,'compiled':False}],
            'scan_limit_reached':False}])

    def test_installed_table_invalidates_constants_and_excludes_stack(self):
        from hgtool.constants import installed_table_candidates
        words=[0]*8+[0,0,0x1034,0x103c,0xffffffff,0x03e00008,0,0x03e00008,0]
        image=Elf(make_elf(words))
        for setup in ([0x24021020,0x8c820000,0xac820000],
                      [0x24021020,0x0c00040d,0,0xac820000],
                      [0x24021020,0xafa20000]):
            code={0x1000+4*n:decode(0x1000+4*n,w) for n,w in enumerate(setup)}
            self.assertEqual(installed_table_candidates(code,{},image),[])

    def test_installed_table_respects_data_ranges_and_scan_cap(self):
        from hgtool.constants import installed_table_candidates
        image=Elf(make_elf([0]*8+[0x1000]*65))
        code={0x1000:decode(0x1000,0x24021020),0x1004:decode(0x1004,0xac820000)}
        found=installed_table_candidates(code,{},image)
        self.assertEqual(len(found[0]['entries']),64)
        self.assertTrue(found[0]['scan_limit_reached'])
        self.assertTrue(all(e['compiled'] for e in found[0]['entries']))
        self.assertEqual(installed_table_candidates(code,{'data_ranges':[{'start':0x1000,'end':0x1004}]},image),[])

    def test_member_descriptor_float_copy_is_evidence_only(self):
        # Floating-point loads copy pointer bits, without changing base GPRs.
        words=[0x24021020,0xc4400008,0xdc430000,13,13,13,13,13,
               0,0xffffffff,0x1030,0,0x03e00008,0]
        code,report=discover(Elf(make_elf(words)),{})
        self.assertNotIn(0x1030,code)
        self.assertEqual(report['direct_member_evidence'],[{
            'record':0x1020,'target':0x1030,'compiled':False,
            'loads':[{'pc':0x1004,'offset':8,'mnemonic':'lwc1'},
                     {'pc':0x1008,'offset':0,'mnemonic':'ld'}]}])

    def test_member_candidates_reject_stale_base_and_other_profiles(self):
        from hgtool.constants import direct_member_candidates
        words=[0]*8+[0,0xffffffff,0x1030,0,0x03e00008,0]
        setup=[0x24021020,0x8c820000,0xc4400008]
        code={0x1000+n*4:decode(0x1000+n*4,w) for n,w in enumerate(setup)}
        self.assertEqual(direct_member_candidates(code,{},Elf(make_elf(words))),[])
        code={0x1000:decode(0x1000,0x24021020),0x1004:decode(0x1004,0xc4400008)}
        for adjustment,selector in [(4,0xffffffff),(0,0)]:
            words[8:10]=[adjustment,selector]
            self.assertEqual(direct_member_candidates(code,{},Elf(make_elf(words))),[])

    def test_returns_are_separate_from_unknown_indirect_calls(self):
        _,report=discover(Elf(make_elf([0x03e00008,0])),{})
        self.assertEqual(report['issues'],[])
        self.assertEqual(report['return_sites'],[0x1000])

    def test_manual_function_and_boundary(self):
        e = Elf(make_elf([0x0000000c, 0, 0, 0]))
        code, report = discover(e, {'functions':[{'name':'extra', 'address':0x1008, 'end':0x100c}]})
        self.assertEqual(set(code), {0x1000, 0x1008})
        self.assertIn('configured function boundary', str(report))

    def test_budget(self):
        code, report = discover(Elf(make_elf([0, 0, 0])), {'analysis': {'max_instructions': 1}})
        self.assertEqual(len(code), 1)
        self.assertIn('budget exhausted', str(report))

    def test_configured_syscall_return(self):
        e=Elf(make_elf([0x24030040,12,0x03e00008,0]))
        code,_=discover(e,{'analysis':{'returning_syscalls':[64]}})
        self.assertIn(0x1008,code)
        code,_=discover(e,{'analysis':{'returning_syscalls':[65]}})
        self.assertNotIn(0x1008,code)

    def test_static_syscall_return(self):
        # A proven LUI/ORI number is as safe as the old adjacent ADDIU form.
        e=Elf(make_elf([0x3c030000,0x34630040,12,0x03e00008,0]))
        code,report=discover(e,{'analysis':{'returning_syscalls':[64]}})
        self.assertIn(0x100c,code)
        self.assertEqual(report['static_syscall_evidence'],[{'site':0x1008,'number':64}])
        self.assertEqual(report['issues'],[])

    def test_static_negative_syscall_return(self):
        e=Elf(make_elf([0x2403ff88,12,0x03e00008,0]))
        code,report=discover(e,{'analysis':{'returning_syscalls':[-120]}})
        self.assertIn(0x1008,code)
        self.assertEqual(report['static_syscall_evidence'],[{'site':0x1004,'number':-120}])
        self.assertEqual(report['issues'],[])

    def test_overlay_relocation_and_overlap(self):
        e=Elf(make_elf([0x1000ffff,0]))
        o={'name':'patch','source':0x1000,'address':0x80008000,'size':8}
        e.add_overlay(o)
        self.assertEqual(e.word(0x80008000),0x1000ffff)
        self.assertEqual(decode(0x80008000,e.word(0x80008000)).target,0x80008000)
        code,_=discover(e,{'overlays':[o]})
        self.assertIn(0x80008000,code)
        with self.assertRaises(ValueError):e.add_overlay(dict(o,name='overlap'))
        with self.assertRaises(ValueError):e.add_overlay(dict(o,address=0x1000))

    def test_manual_data_blocks_reachable_edge(self):
        e = Elf(make_elf([0, 0, 0]))
        code, report = discover(e, {'data_ranges':[{'start':0x1004, 'end':0x1008}]})
        self.assertEqual(set(code), {0x1000})
        self.assertIn('edge enters data', str(report))

    def test_reject_reserved_instruction_fields(self):
        self.assertEqual(decode(0x1000, 0x00ff00ff).name, 'dsra32')
        for word in (0x00200000, 0x00210008, 0x18210001, 0x3c210001, 0x00ff08ff):
            self.assertEqual(decode(0x1000, word).name, 'unsupported')

    def test_config_function_validation(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d); raw = make_elf([0x00800008, 0, 0x0000000c]); (p / 'game.elf').write_bytes(raw)
            base = f'schema_version = 1\n[image]\npath = "game.elf"\nsha256 = "{hashlib.sha256(raw).hexdigest()}"\n'
            f = p / 'game.toml'
            valid = '\n[[functions]]\nname = "entry"\naddress = 4096\nend = 4104\n'
            f.write_text(base + valid); self.assertEqual(len(load(f, Elf)[0]['functions']), 1)
            for extra in (valid + valid, valid.replace('4096', '4097'),
                          valid.replace('end = 4104', 'end = 4096'),
                          '\n[[indirect_targets]]\nsite = 4100\ntargets = [4104]\n',
                          valid + '\n[[data_ranges]]\nstart = 4096\nend = 4100\n'):
                f.write_text(base + extra)
                with self.assertRaises(ValueError): load(f, Elf)

    def test_config_identity_and_typo(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d); raw = make_elf([0x0000000c]); (p / 'game.elf').write_bytes(raw)
            cfg = f'schema_version = 1\n[image]\npath = "game.elf"\nsha256 = "{hashlib.sha256(raw).hexdigest()}"\n'
            f = p / 'game.toml'; f.write_text(cfg)
            self.assertEqual(load(f, Elf)[1].entry, 0x1000)
            f.write_text(cfg.replace('sha256 = "', 'sha256 = "0'))
            with self.assertRaises(ValueError): load(f, Elf)
            f.write_text(cfg + '[analysis]\nmax_instruction = 10\n')
            with self.assertRaises(ValueError): load(f, Elf)


if __name__ == '__main__': unittest.main()
