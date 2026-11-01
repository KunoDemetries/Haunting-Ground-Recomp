import argparse
import json
from pathlib import Path
import sys
from .config import load
from .elf import Elf
from .discover import discover
from .emit import emit,emit_shards


def write_text_if_changed(path, text):
    data=text.encode('utf-8')
    try:
        if path.read_bytes()==data:return False
    except FileNotFoundError:
        pass
    path.write_bytes(data)
    return True


def symbol_table_source(code,cfg,names_path):
    """Host-diagnostic EE symbol table: configured roots + JAL targets, named from the
    evidence-only out/function_names.json when present (never affects guest code)."""
    starts={f['address'] for f in cfg.get('functions',[])}
    for pc,i in code.items():
        if i.name=='jal' and i.target is not None:starts.add(i.target)
    starts={a for a in starts if a in code}
    names={}
    if names_path.exists():
        for key,value in json.loads(names_path.read_text(encoding='utf-8')).items():
            if value.get('name'):names[int(key,16)]=value['name']
    starts|={a for a in names if a in code}
    rows=[]
    for a in sorted(starts):
        name=names.get(a)
        safe=''.join(c for c in name if c.isalnum() or c in '_:.~') if name else ''
        rows.append('{0x%08xu,%s}'%(a,'"'+safe+'"' if safe else 'nullptr'))
    lines=['','#include "hg/symbols.hpp"','namespace {','const hg::SymbolEntry hg_ee_symbols[]={',
           ',\n'.join(rows),'};',
           'const bool hg_ee_symbols_registered=(hg::ee_symbol_table={hg_ee_symbols,sizeof hg_ee_symbols/sizeof hg_ee_symbols[0]},true);',
           '}','']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description='Independent EE static analysis and C++ translation')
    parser.add_argument('command', choices=['analyze', 'emit', 'inspect', 'major-scan', 'vu-inspect', 'assets', 'modules', 'iop-plan', 'iop-emit', 'iop-audit', 'iop-bundle', 'reboot'])
    parser.add_argument('--config', type=Path, default=Path('config/haunting_ground_us.toml'))
    parser.add_argument('--out', type=Path, default=Path('out'))
    parser.add_argument('--pc', type=lambda s:int(s,0))
    parser.add_argument('--count', type=int, default=16)
    parser.add_argument('--triage', action='store_true',
                        help='write a grouped read-only unresolved-boundary queue')
    parser.add_argument('--state', type=Path)
    parser.add_argument('--vu-program',type=Path,help='External raw VU microprogram pairs for read-only analysis')
    parser.add_argument('--vu-base',type=lambda s:int(s,0),default=0)
    parser.add_argument('--archive',type=Path,default=Path('Haunting Ground (USA)/DATA.CVM'))
    parser.add_argument('--volume-offset',type=lambda s:int(s,0),default=0x1800)
    parser.add_argument('--asset',help='Exact indexed asset path to hash without extracting')
    parser.add_argument('--module-dir',type=Path,default=Path('Haunting Ground (USA)/MODULES'))
    parser.add_argument('--relocation-base',type=lambda s:int(s,0),help='IRX dry-run base, independently applied to each module')
    parser.add_argument('--iop-config',type=Path,default=Path('config/iop_modules.toml'))
    parser.add_argument('--iop-module',default='modmsin')
    parser.add_argument('--iop-modules',nargs='+')
    parser.add_argument('--reboot-image',type=Path,default=Path('Haunting Ground (USA)/MODULES/IOPRP300.IMG'))
    args = parser.parse_args()
    try:
        if args.command=='major-scan':
            from .major_scan import run
            report=run(args.config,args.iop_config)
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'major-scan.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            summary=report['summary']
            print(f'EE {summary["ee"]["reachable_words"]} words / {summary["ee"]["boundary_count"]} boundaries; '
                  f'IOP {summary["iop"]["module_count"]} modules, {summary["iop"]["reachable_words"]} words / '
                  f'{summary["iop"]["boundary_count"]} boundaries; all-module static bundle '
                  f'{report["iop"]["all_configured_bundle"]["guarded_bindings"]} links + '
                  f'{report["iop"]["all_configured_bundle"]["native_adapter_sites"]} native adapters / '
                  f'{report["iop"]["all_configured_bundle"]["residual_issue_count"]} residual / '
                  f'{report["iop"]["all_configured_bundle"]["trap_count"]} explicit traps; '
                  f'report: {args.out / "major-scan.json"}')
            return 0
        if args.command=='iop-bundle':
            from .iop_build import bundle
            cpp,report=bundle(args.iop_config,args.iop_modules)
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'iop-translated.cpp').write_text(cpp,encoding='utf-8')
            (args.out/'iop-analysis.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print(f'{len(report["modules"])} modules, {report["reachable_words"]} reachable words; input root: {report["input_root"]}')
            print(report['note']);return 0
        if args.command=='vu-inspect':
            if args.vu_program is None:raise ValueError('vu-inspect requires --vu-program')
            from .vu_decode import report_program
            report=report_program(args.vu_program.read_bytes(),args.vu_base)
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'vu-analysis.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print(f'{report["pair_count"]} VU instruction pairs; {len(report["control_pairs"])} lower control pairs; report: {args.out / "vu-analysis.json"}')
            print(report['note']);return 0
        if args.command=='iop-audit':
            from .iop_build import audit
            report=audit(args.iop_config)
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'iop-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            for m in report['modules']:
                print(f'{m["module"]}: {m["reachable_words"]} reachable words, {len(m["unsupported_words"])} unsupported words, {len(m["issues"])} boundaries')
            print(report['note']);return 0
        if args.command=='reboot':
            from .romdir import RebootImage
            report=RebootImage(args.reboot_image).inventory()
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'reboot.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print(f'{len(report["entries"])} reboot-image entries, {len(report["modules"])} embedded IRXs; metadata only.')
            return 0
        if args.command=='iop-emit':
            from .iop_build import build
            cpp,report=build(args.iop_config,args.iop_module)
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'iop-translated.cpp').write_text(cpp,encoding='utf-8')
            (args.out/'iop-analysis.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print(f'{report["module"]}: {report["reachable_words"]} reachable IOP words, {len(report["issues"])} boundaries')
            return 0
        if args.command=='iop-plan':
            from .iop_plan import plan
            report=plan(args.iop_config)
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'iop-plan.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print(f'{len(report["modules"])} placed modules, {len(report["bindings"])} local bindings, '
                  f'{len(report["native_bindings"])} native service bindings, {len(report["unresolved_imports"])} unresolved imports; analysis only.')
            return 0
        if args.command=='modules':
            from .irx import inspect_irx
            paths=sorted(p for p in args.module_dir.iterdir() if p.suffix.upper()=='.IRX')
            if not paths:raise ValueError('no IRX modules found')
            modules=[inspect_irx(p) for p in paths]
            if args.relocation_base is not None:
                import hashlib
                from .relocate import relocate_sections
                for module in modules:
                    sections=relocate_sections(Path(module['path']).read_bytes(),module,args.relocation_base)
                    module['relocation_dry_run']={'base':args.relocation_base,'sections':[
                        {'name':s['name'],'address':s['address'],'size':len(s['data']),
                         'sha256':hashlib.sha256(s['data']).hexdigest()} for s in sections]}
            args.out.mkdir(parents=True,exist_ok=True)
            (args.out/'modules.json').write_text(json.dumps(modules,indent=2)+'\n',encoding='utf-8')
            for module in modules:
                print(f'{Path(module["path"]).name}: {len(module["imports"])} import libraries, '
                      f'{len(module["exports"])} export libraries, {len(module["relocations"])} relocations')
            print(f'Read-only inventory: {args.out / "modules.json"}; no IOP execution yet.')
            return 0
        if args.command=='assets':
            from .volume import Volume
            from dataclasses import asdict
            volume=Volume(args.archive,args.volume_offset)
            entries=volume.index()
            if args.asset:
                entry=entries[args.asset]
                print(json.dumps({**asdict(entry),'sha256':volume.digest(entry)},indent=2))
            else:
                args.out.mkdir(parents=True,exist_ok=True)
                report={'archive':str(args.archive),'origin':volume.origin,'label':volume.label,
                        'blocks':volume.blocks,'entries':[asdict(e) for _,e in sorted(entries.items())]}
                (args.out/'assets.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
                print(f'{len(entries)} entries; metadata only: {args.out / "assets.json"}')
            return 0
        cfg, image = load(args.config, Elf)
        if args.command=='inspect':
            from .inspect import listing,snapshot
            if args.state: snapshot(image,args.state)
            elif args.pc is not None: listing(image,args.pc,args.count)
            else: raise ValueError('inspect requires --pc or --state external-prefix')
            return 0
        code, report = discover(image, cfg)
        # A truncated discovery silently drops reachable code (2026-10-14: 164 new roots
        # pushed the total past the cap and 0x2295c4 vanished). Fail explicitly instead.
        exhausted=[i for i in report['issues'] if i.get('reason')=='instruction budget exhausted']
        if exhausted:
            raise SystemExit(f"discovery hit analysis.max_instructions ({len(code)} words; first dropped pc 0x{exhausted[0]['pc']:08x}); raise the limit in the config")
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'analysis.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        listing = '\n'.join(f'{pc:08x} {i.word:08x} {i.name}' for pc, i in sorted(code.items()))
        (args.out / 'listing.txt').write_text(listing + '\n', encoding='utf-8')
        if args.triage:
            from .triage import make_triage
            triage=make_triage(code,report)
            (args.out / 'analysis-triage.json').write_text(json.dumps(triage,indent=2)+'\n',encoding='utf-8')
            summary=', '.join(f'{group["reason"]}: {group["count"]}' for group in triage['groups'])
            print(f'Grouped boundary queue: {summary}; {len(triage["file_pointer_candidates"])} file-pointer candidates; '
                  f'{len(triage["installed_table_candidates"])} installed-table candidates / '
                  f'{len(triage["missing_table_targets"])} distinct uncompiled candidate targets; '
                  f'{len(triage["direct_member_candidates"])} member records / '
                  f'{len(triage["missing_member_targets"])} uncompiled member candidates; '
                  f'report: {args.out / "analysis-triage.json"}')
        if args.command == 'emit':
            main,shards=emit_shards(code,image.sha256,cfg.get('overlays',[]),image,
                                    cfg.get('vu1_programs',[]))
            main+=symbol_table_source(code,cfg,args.out/'function_names.json')
            changed=int(write_text_if_changed(args.out/'translated.cpp',main))
            expected=set()
            for name,source in shards.items():
                path=args.out/name;expected.add(path)
                changed+=int(write_text_if_changed(path,source))
            for stale in args.out.glob('translated-shard-*.cpp'):
                if stale not in expected:
                    stale.unlink()
                    changed+=1
            print(f'{len(shards)} EE translation shards; {changed} generated files changed')
        print(f"{len(code)} reachable words, {len(report['issues'])} unresolved items; report: {args.out / 'analysis.json'}")
        print('Diagnostic translation only; incomplete coverage is recorded and traps at runtime.')
        return 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
