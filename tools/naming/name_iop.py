"""Evidence-only IOP naming: module export tables (library name + ordinal -> target) and
import stubs (library + ordinal -> stub), relocated by each configured module base,
named from the ps2tek "BIOS IOP <LIBRARY>" export lists. Undocumented libraries/ordinals
stay unnamed. usage: python tools/naming/name_iop.py tools/naming/ps2tek_names.json config/iop_modules.toml out/modules.json out/reboot.json out/iop_function_names.json out/iop_function_names.txt"""
import json, re, sys
from collections import defaultdict
tek, cfg_path, mods_path, reboot_path, out_json, out_txt = sys.argv[1:7]
docs = {lib: {int(k, 16): v for k, v in d.items()} for lib, d in json.load(open(tek))['iop_exports'].items()}
cfg = open(cfg_path, encoding='utf-8').read()
blocks = re.split(r'\n\[\[modules\]\]\n', cfg)
inv = {m['path'].replace('\\', '/').split('/')[-1].upper(): m for m in json.load(open(mods_path))}
for m in json.load(open(reboot_path))['modules']:
    inv['#' + m['path'].split('#')[-1].upper()] = m
names, unmatched = {}, defaultdict(int)
for b in blocks:
    nm = re.search(r'^name\s*=\s*"([^"]+)"', b, re.M)
    base = re.search(r'^base\s*=\s*(0x[0-9a-fA-F]+)', b, re.M)
    if not nm or not base:
        continue
    base = int(base.group(1), 16)
    member = re.search(r'^member\s*=\s*"([^"]+)"', b, re.M)
    path = re.search(r'^path\s*=\s*"([^"]+)"', b, re.M)
    key = '#' + member.group(1).upper() if member else (path.group(1).split('/')[-1].upper() if path else None)
    m = inv.get(key)
    if not m:
        continue
    for ex in m.get('exports', []):
        lib = ex['name'].lower()
        for e in ex['exports']:
            fn = docs.get(lib, {}).get(e['ordinal'])
            if fn and e['target']:
                names[base + e['target']] = {'name': '%s.%s' % (lib, fn), 'module': nm.group(1),
                                             'evidence': 'export %s ordinal 0x%x -> +0x%x (ps2tek BIOS IOP %s)' % (lib, e['ordinal'], e['target'], lib.upper())}
            elif e['ordinal'] >= 4:
                unmatched[lib] += 1
    for im in m.get('imports', []):
        lib = im['name'].lower()
        for e in im['imports']:
            fn = docs.get(lib, {}).get(e['ordinal'])
            if fn:
                names.setdefault(base + e['stub'], {'name': 'stub:%s.%s' % (lib, fn), 'module': nm.group(1),
                                 'evidence': 'import stub %s ordinal 0x%x (ps2tek BIOS IOP %s)' % (lib, e['ordinal'], lib.upper())})
json.dump({'%08x' % k: v for k, v in sorted(names.items())}, open(out_json, 'w'), indent=1)
with open(out_txt, 'w', encoding='utf-8') as fh:
    for k, v in sorted(names.items()):
        fh.write('%08x  %-36s %-14s %s\n' % (k, v['name'], v['module'], v['evidence']))
print('documented libraries', len(docs), 'named', len(names),
      'exports', sum(1 for v in names.values() if not v['name'].startswith('stub:')),
      'stubs', sum(1 for v in names.values() if v['name'].startswith('stub:')))
print('undocumented export libraries:', dict(unmatched))
