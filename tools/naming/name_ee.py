"""Evidence-only EE function naming, pass 1 v2.
Self-name evidence in a referenced string (one of):
  - identifier right after an optional error code at the string start, followed by
    ':' or '()' then ':'       e.g. "E0203261: MWSFCRE_ResetSfdHn: SFD_Stop() failed"
  - trailing "(Identifier)"  e.g. "...is NULL.(ADXF_Stop)"
  - C++ "Class::method" at the string start, e.g. "basic_string::reserve length_error"
Identifiers followed by "()" mid-sentence are callees and ignored. A function gets a
name only when one candidate has a strict majority of its self-name evidence AND no
other function claims the same name. Source-file strings (*.c/*.cpp/*.cc) give a
module tag. Syscall wrappers (addiu v1,zero,N; syscall; jr ra) are named from the
ps2tek EE syscall list. usage: python tools/naming/name_ee.py <elf> config/haunting_ground_us.toml out/listing.txt tools/naming/ps2tek_names.json out/function_names.json out/function_names.txt"""
import json, re, struct, sys
from collections import Counter, defaultdict
elf_path, cfg_path, listing_path, sys_path, out_json, out_txt = sys.argv[1:7]
elf = open(elf_path, 'rb').read()
po = struct.unpack_from('<I', elf, 0x1c)[0]
kind, off, va, pa, fs, ms, fl, al = struct.unpack_from('<8I', elf, po)
ws = struct.unpack_from('<%dI' % (fs // 4), elf, off)
def w(a): return ws[(a - va) // 4]
def cstr(a):
    if not (va <= a < va + fs):
        return None
    b = elf[off + a - va: off + a - va + 160].split(b'\0')[0]
    return b.decode() if len(b) >= 4 and all(32 <= c < 127 for c in b) else None
compiled = set()
for line in open(listing_path):
    p = line.split()
    if len(p) >= 2:
        try:
            compiled.add(int(p[0], 16))
        except ValueError:
            pass
starts = set(int(x, 16) for x in re.findall(r'address = (0x[0-9a-f]+)', open(cfg_path, encoding='utf-8').read()))
for a in compiled:
    if va <= a < va + fs and w(a) >> 26 == 3:
        starts.add(((a + 4) & 0xf0000000) | ((w(a) & 0x03ffffff) << 2))
starts = sorted(s for s in starts if s in compiled and va <= s < va + fs)
lead = re.compile(r'^\s*(?:E?[0-9]+[A-Za-z0-9]*\s*:?\s*)?([A-Za-z_][A-Za-z0-9_]{3,})\s*(?:\(\))?\s*:')
trail = re.compile(r'\(([A-Za-z_][A-Za-z0-9_]{3,})\)\s*$')
cpp = re.compile(r'^\s*([A-Za-z_]\w*::[A-Za-z_~]\w*)')
srcfile = re.compile(r'([A-Za-z0-9_./\\-]+\.(?:c|cpp|cc))\b')
generic = {'NULL', 'ERROR', 'WARNING', 'Error', 'Warning', 'erAdx', 'basic_string', 'ios_base', 'locale', 'string'}
def self_names(s):
    out = []
    for rx in (cpp, lead, trail):
        m = rx.search(s)
        if m and m.group(1) not in generic and (rx is cpp or re.search(r'[A-Z_]', m.group(1)[1:]) or '_' in m.group(1)):
            out.append(m.group(1))
            break
    return out
per = {}
for f in starts:
    strs, his, pc = [], {}, f
    for _ in range(6000):
        if pc not in compiled or not (va <= pc < va + fs):
            break
        x = w(pc); op = x >> 26
        if op == 0x0f:
            his[(x >> 16) & 31] = (x & 0xffff) << 16
        elif op in (0x09, 0x0d) and ((x >> 21) & 31) in his:
            imm = x & 0xffff
            if op == 0x09 and imm & 0x8000:
                imm -= 0x10000
            s = cstr((his[(x >> 21) & 31] + imm) & 0xffffffff)
            if s:
                strs.append(s)
        if (op == 0 and (x & 0x3f) == 8) or op == 2:
            break
        pc += 4
    if strs:
        per[f] = strs
claims = defaultdict(list)
result = {}
for f, strs in per.items():
    votes = Counter(n for s in strs for n in self_names(s))
    module = next((m.group(1) for s in strs for m in [srcfile.search(s)] if m), None)
    entry = {}
    if votes:
        (top, n1), *rest = votes.most_common()
        if not rest or n1 > rest[0][1]:
            entry['candidate'] = top
            claims[top].append(f)
        entry['evidence'] = [s for s in strs if self_names(s)][:2]
    if module:
        entry['module'] = module.replace('\\', '/').split('/')[-1]
    if entry:
        result[f] = entry
names = {}
for f, e in result.items():
    rec = {}
    c = e.get('candidate')
    if c and len(claims[c]) == 1:
        rec = {'name': c, 'source': 'self-string', 'evidence': e['evidence'][0][:100]}
    if 'module' in e:
        rec.setdefault('source', 'module-string')
        rec['module'] = e['module']
    if rec:
        names[f] = rec
sysnames = {int(k, 16): v for k, v in json.load(open(sys_path))['ee_syscalls'].items()}
for i in range(len(ws) - 3):
    a, b, c = ws[i], ws[i + 1], ws[i + 2]
    if (a >> 16) == 0x2403 and b == 0x0000000c and c == 0x03e00008:
        n = a & 0xffff
        signed = n - 0x10000 if n & 0x8000 else n
        base = sysnames.get(abs(signed))
        if base:
            label = base if signed >= 0 else 'syscall_minus_0x%x_%s' % (abs(signed), base)
            names.setdefault(va + 4 * i, {'name': label, 'source': 'syscall %d' % signed,
                                          'evidence': 'addiu v1,zero,%d; syscall; jr ra (ps2tek EE syscall list)' % signed})
json.dump({'%08x' % k: v for k, v in sorted(names.items())}, open(out_json, 'w'), indent=1)
with open(out_txt, 'w', encoding='utf-8') as fh:
    for k, v in sorted(names.items()):
        fh.write('%08x  %-34s %-13s %-14s %s\n' % (k, v.get('name', '-'), v.get('module', ''), v['source'], v.get('evidence', '')))
named = sum(1 for v in names.values() if 'name' in v)
print('function starts', len(starts), 'named', named, 'module-tagged', sum(1 for v in names.values() if 'module' in v),
      'by source', dict(Counter(v['source'].split()[0] for v in names.values() if 'name' in v)))
