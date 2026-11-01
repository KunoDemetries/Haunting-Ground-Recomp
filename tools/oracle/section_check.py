"""Save-based section check: load a memory-card save slot in our build and in the PCSX2
oracle with the same menu inputs, let it settle, compare game state; one line per slot.

usage: python tools/oracle/section_check.py SLOT [SLOT ...]
Needs: %TEMP%/hg-saves/icon771/card-icon771.ps2 (our side) and the same card as PCSX2's
slot-2 Mcd002.ps2 (oracle side); the pinned route's boot inputs and TOC under
%TEMP%/hg-world-editor/snap. Outputs go to %TEMP%/hg-saves/check/.
"""
import os
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import pcsx2_drive as drive  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEMP = os.environ['TEMP']
SAVES = os.path.join(TEMP, 'hg-saves', 'icon771')
SNAP = os.path.join(TEMP, 'hg-world-editor', 'snap')
OUT = os.path.join(TEMP, 'hg-saves', 'check')
GAME = os.path.join(TEMP, 'hg-world-editor', 'autopilot', 'hg_game.exe')
PCSX2 = os.path.join(ROOT, 'emu', 'PCSX2-MCP-v1.0.0-win64', 'pcsx2-qt.exe')
ISO = os.path.join(ROOT, 'Haunting Ground (USA)', 'Haunting Ground (USA).iso')
SNAPS = os.path.join(os.environ['USERPROFILE'], 'Documents', 'PCSX2', 'snaps')
NEUTRAL = 'pad:65535,128,128,128,128,0,0,0,0,0,0,0,0,0,0,0,0'
PADS = {'cross': 'pad:49151,128,128,128,128,0,0,0,0,0,0,255,0,0,0,0,0',
        'up': 'pad:65519,128,128,128,128,0,0,255,0,0,0,0,0,0,0,0,0',
        'down': 'pad:65471,128,128,128,128,0,0,0,255,0,0,0,0,0,0,0,0',
        'right': 'pad:65503,128,128,128,128,255,0,0,0,0,0,0,0,0,0,0,0'}
LIST_AT, STEP = 66_500_000, 600_000          # our slices: save list visible, gap between presses
COUNTER, AT_FRAME = 0xFC7280, int(os.environ.get('HG_CHECK_FRAME', '450'))            # game frames since gameplay began (both sides); compare here
OBJECTS = (('hewie', 0x44F820, 0x4000), ('random', 0x44E550, 0x40), ('fiona', 0x44F818, 0x2000),
           ('pursuer', 0x44F828, 0x2000),                       # stalker slot (lead: OpenHuey actor.h)
           ('sound', 0x44E560, 0x400), ('music', 0x44E970, 0x200), ('adx', 0x44E980, 0x200),   # game-side audio state
           ('summoner', 0x888404, 0x40))   # SceneGame (Game +0x400A04 scene slot) +0x7A4: stalker summoner (lead: OpenHuey summoner.md)
OBJECT_OFFSET = {'summoner': 0x7A4}
PURSUER_VTABLES = {0x469D10, 0x470A90, 0x46F020, 0x477AE0, 0x46F6B0, 0x4738A0, 0x4715C0, 0x4718F0}   # stalker classes seen in slot 2 across the 12 saves; empty slots point at stale memory
# Scripted walk (HG_CHECK_WALK=1): (frame, key, down). Our build gets the same controller
# state at the same frames through HG-DIAG-125 (--input-on-ee-word).
WALK = [(300, 'lup', True), (360, 'lup', False)]
WALK_ON = bool(os.environ.get('HG_CHECK_WALK'))
WALK_PADS = {('lup', True): 'pad:65535,128,128,128,0,0,0,0,0,0,0,0,0,0,0,0,0'}
LIMIT = int(os.environ.get("HG_CHECK_LIMIT", "40000000"))                           # slices after the last press, safety bound


SNDDRV_IRX = os.path.join(ROOT, 'Haunting Ground (USA)', 'MODULES', 'SNDDRV.IRX')


def snddrv_state(iop):
    """SNDDRV's data/BSS (module offsets 0xC670..0x17E90) found in an IOP RAM image by its
    code bytes (modules sit at different IOP addresses in our build and in PCSX2)."""
    irx = open(SNDDRV_IRX, 'rb').read()
    probe = irx[0xb0 + 0x40:0xb0 + 0x58]
    at = iop.find(probe)
    if at < 0:
        return None
    base = at - 0x40
    return base, iop[base + 0xC670:base + 0x17E90]


def snddrv_diffs(mine, ref):
    """Differing SNDDRV words, ignoring IOP addresses (heap/module placement differs)."""
    if not mine or not ref:
        return ['SNDDRV not found']
    (bo, a), (br, b) = mine, ref
    out = []
    for o in range(0, min(len(a), len(b)) - 0x200, 4):   # last 0x200 bytes: stack leftovers
        x, y = struct.unpack_from('<I', a, o)[0], struct.unpack_from('<I', b, o)[0]
        if x == y or (0x1000 <= x < 0x200000 and 0x1000 <= y < 0x200000):
            continue
        out.append(f'+0x{0xC670 + o:x}: ours 0x{x:08x} pcsx2 0x{y:08x}')
    return out


def moves(slot):
    """Cursor starts on the last-used slot (06); 01-06 left column, 07-12 right."""
    if slot <= 6:
        return ['up'] * (6 - slot)
    return ['right'] + ['up'] * (12 - slot)


def state_from(read32):
    f32 = lambda a: struct.unpack('<f', struct.pack('<I', read32(a)))[0]
    out = {}
    for name, ptr, vt in (('fiona', 0x44F818, 0x46AAB0), ('hewie', 0x44F820, 0x46A120), ('stalker', 0x44F808, None)):
        a = read32(ptr) & 0x1FFFFFF
        if a and (read32(a) == vt or (vt is None and read32(a) in PURSUER_VTABLES)):
            out[name] = (read32(a + 0x30), round(f32(a + 0x10), 2), round(f32(a + 0x18), 2), round(f32(a + 0x54), 3))
    return out


def walk_args():
    out = []
    for frame, name, down in (WALK if WALK_ON else []):
        out += ['--input-on-ee-word', hex(COUNTER), str(frame), WALK_PADS.get((name, down), NEUTRAL)]
    return out


def ours(slot):
    events = [l.strip() for l in open(os.path.join(SNAP, 'input-events.txt'))][:10]
    events += [f'52500000 {PADS["cross"]}', f'52600000 {NEUTRAL}', f'58500000 {PADS["cross"]}', f'58600000 {NEUTRAL}']
    t = LIST_AT
    for m in moves(slot) + ['cross']:
        events += [f'{t} {PADS[m]}', f'{t + 100000} {NEUTRAL}']
        t += STEP
    t += 2_400_000
    events += [f'{t} {PADS["cross"]}', f'{t + 100000} {NEUTRAL}']
    end = t + LIMIT
    os.makedirs(OUT, exist_ok=True)
    ev = os.path.join(OUT, f'slot{slot:02}.events.txt')
    open(ev, 'w').write('\n'.join(events) + '\n')
    card = os.path.join(OUT, 'run-card.ps2')
    shutil.copy(os.path.join(SAVES, 'card-icon771.ps2'), card)
    dump = os.path.join(OUT, f'ours{slot:02}.ram')
    preview = os.path.join(OUT, f'ours{slot:02}.ppm')
    with open(os.path.join(OUT, f'ours{slot:02}.err.txt'), 'w') as err:
        subprocess.run([GAME, '--clock-profile', 'issue-slots', '--slices', str(end), '--toc-record', os.path.join(SNAP, 'toc.bin'),
                        '--memory-card', card, '--preview-file', preview, '--dump-iop', dump, '--gpu-sprites', '--gpu-resident',
                        '--gpu-triangles', '--no-host-input', '--hidden-host', '--host-frames', '1000000', '--unlimited',
                        '--input-events-file', ev, '--stop-at-ee-word', hex(COUNTER), str(AT_FRAME)] + walk_args(), cwd=ROOT, stdout=subprocess.DEVNULL, stderr=err)
    ram = open(dump + '.ee-ram.bin', 'rb').read()
    read = lambda a: struct.unpack_from('<I', ram, a)[0]
    s = state_from(read)
    s['snddrv'] = snddrv_state(open(dump, 'rb').read())
    s['frame'] = read(COUNTER)   # stopped just after the game writes frame N (before its actor update)
    import re
    m = re.search(r'HG-DIAG-124 .* pc=0x([0-9a-f]+)', open(os.path.join(OUT, f'ours{slot:02}.err.txt'), errors='replace').read())
    s['pc'] = int(m.group(1), 16) if m else None
    s['raw'] = {name: ram[(read(ptr) & 0x1FFFFFF) + OBJECT_OFFSET.get(name, 0):(read(ptr) & 0x1FFFFFF) + OBJECT_OFFSET.get(name, 0) + size] for name, ptr, size in OBJECTS
                if name != 'pursuer' or read(read(ptr) & 0x1FFFFFF) in PURSUER_VTABLES}
    return s, preview


class Oracle:
    """One PCSX2 session; each check starts from savestate 9 (the save list, cursor on 06),
    made once by booting with the injected card and opening Load -> slot 2."""
    STICK = {'up': 'lup', 'down': 'ldown', 'right': 'lright'}   # arrows are ignored while inactive

    def __init__(self):
        pid_file = os.path.join(TEMP, 'pcsx2pid.txt')
        pid = int(open(pid_file).read()) if os.path.exists(pid_file) else 0
        wins = drive.find_window(pid) if pid else []
        self.proc = None
        if not wins:
            self.proc = subprocess.Popen([PCSX2, '-fastboot', ISO])
            pid = self.proc.pid
            open(pid_file, 'w').write(str(pid))
            for _ in range(60):
                time.sleep(0.5)
                wins = drive.find_window(pid)
                if wins:
                    break
            time.sleep(10)
        self.hwnd = wins[0][0]
        self.p = drive.Pine()

    def load(self, slot, expect_room=None, attempts=4):
        """Load a save slot from savestate 9; menu presses are sometimes dropped, so check
        Fiona's room against the expected one (from our build) and retry."""
        hwnd, p = self.hwnd, self.p
        s = {}
        for attempt in range(attempts):
            p.load_state(9); time.sleep(2.5)
            for m in moves(slot):
                drive.press(hwnd, self.STICK[m], 0.25); time.sleep(0.8)
            drive.press(hwnd, 'cross'); time.sleep(3)
            drive.press(hwnd, 'cross')
            s = {}
            for _ in range(80):
                time.sleep(0.5)
                s = state_from(p.u32)
                if 'fiona' in s and (s['fiona'][1] or s['fiona'][2]):
                    break
            if expect_room is None or s.get('fiona', (None,))[0] == expect_room:
                return s
            print(f'    oracle loaded room {s.get("fiona", (None,))[0]} instead of {expect_room}; retrying', flush=True)
        raise RuntimeError(f'slot {slot}: oracle could not load the expected save')

    def check(self, slot, stop_pc=None, expect_room=None):
        hwnd, p = self.hwnd, self.p
        s = self.load(slot, expect_room)
        # Pause the emulated EE exactly where the game writes frame N to the counter (a
        # DebugServer write check breaks before the store, with N-1 still in memory): the
        # same point where our build stops, before that frame's actor updates.
        deadline = time.time() + AT_FRAME / 30 + 90   # the oracle runs at real speed (30 frames/s)
        first = min([f for f, _, _ in WALK if WALK_ON] + [AT_FRAME])
        while time.time() < deadline and not (first - 20 <= p.u32(COUNTER) < first - 1):
            time.sleep(0.01)
        dbg = drive.Debug()
        dbg.call(cmd='set_memcheck', cpu='ee', address=COUNTER, end=COUNTER + 4, type='write', action='break')

        def pause_at(frame):
            while time.time() < deadline:
                st = dbg.call(cmd='status', cpu='ee')
                if not st['paused']:
                    time.sleep(0.002)
                    continue
                if p.u32(COUNTER) == frame - 1:
                    return
                dbg.call(cmd='resume', cpu='ee')
            raise TimeoutError(f'frame {frame} not reached')
        try:
            for frame, name, down in (WALK if WALK_ON else []):   # scripted inputs at exact frames
                pause_at(frame)
                drive.key(hwnd, name, down); time.sleep(0.05)
                dbg.call(cmd='resume', cpu='ee')
            pause_at(AT_FRAME)
            if stop_pc:   # run on to the instruction where our build stopped
                dbg.call(cmd='remove_memcheck', cpu='ee', address=COUNTER, end=COUNTER + 4)
                dbg.call(cmd='set_breakpoint', cpu='ee', address=stop_pc, temporary=True)
                dbg.call(cmd='resume', cpu='ee')
                while time.time() < deadline:
                    st = dbg.call(cmd='status', cpu='ee')
                    if st['paused'] and int(st['pc'], 16) == stop_pc and p.u32(COUNTER) == AT_FRAME:
                        break
                    if st['paused']:
                        dbg.call(cmd='resume', cpu='ee')
                    time.sleep(0.001)
                try:
                    dbg.call(cmd='remove_breakpoint', cpu='ee', address=stop_pc)
                except RuntimeError:
                    pass   # temporary breakpoints remove themselves
            s = state_from(p.u32)
            s['raw'] = {name: p.block((p.u32(ptr) & 0x1FFFFFF) + OBJECT_OFFSET.get(name, 0), size // 4) for name, ptr, size in OBJECTS
                        if name != 'pursuer' or p.u32(p.u32(ptr) & 0x1FFFFFF) in PURSUER_VTABLES}
            s['frame'] = p.u32(COUNTER) + (0 if stop_pc else 1)
            s['snddrv'] = snddrv_state(dbg.read(0, 0x200000, 'iop'))
        finally:
            for frame, name, down in (WALK if WALK_ON else []):
                drive.key(hwnd, name, False)
            try:
                dbg.call(cmd='remove_memcheck', cpu='ee', address=COUNTER, end=COUNTER + 4)
            except RuntimeError:
                pass
            dbg.call(cmd='resume', cpu='ee')
        before = set(os.listdir(SNAPS))
        drive.press(hwnd, 'shot'); time.sleep(2)
        new = sorted(set(os.listdir(SNAPS)) - before)
        return s, (os.path.join(SNAPS, new[-1]) if new else None)


def frame_diff(a_path, b_path):
    from PIL import Image, ImageChops
    a = Image.open(a_path).convert('L').resize((160, 112))
    b = Image.open(b_path).convert('L').resize((160, 112))
    d = ImageChops.difference(a, b)
    return sum(1 for v in d.getdata() if v > 40) / (160 * 112)


def main():
    shutil.copy(os.path.join(ROOT, 'build', 'Release', 'hg_game.exe'), GAME)
    oracle = Oracle()
    for slot in map(int, sys.argv[1:]):
        mine, preview = ours(slot)
        ref, shot = oracle.check(slot, mine.pop('pc', None), mine.get('fiona', (None,))[0])
        if shot:   # PCSX2 may still be writing the PNG
            for _ in range(20):
                try:
                    shutil.copy(shot, os.path.join(OUT, f'pcsx2{slot:02}.png'))
                    shot = os.path.join(OUT, f'pcsx2{slot:02}.png')
                    break
                except PermissionError:
                    time.sleep(0.5)
        raw_mine, raw_ref = mine.pop('raw', {}), ref.pop('raw', {})
        snd = snddrv_diffs(mine.pop('snddrv', None), ref.pop('snddrv', None))
        diffs = [k for k in ('fiona', 'hewie', 'stalker') if mine.get(k) != ref.get(k)]
        if snd:
            open(os.path.join(OUT, f'snddrv{slot:02}.txt'), 'w').write('\n'.join(snd) + '\n')
            diffs.append(f'snddrv({len(snd)})')
            if os.environ.get('HG_CHECK_DETAIL'):
                for line in snd[:25]:
                    print('    snddrv' + line)
        for name in raw_mine:
            a, b = raw_mine[name], raw_ref.get(name, b'')
            words = [o for o in range(0, min(len(a), len(b)), 4) if a[o:o + 4] != b[o:o + 4]]
            if words:
                diffs.append(name + '-object')
                if os.environ.get('HG_CHECK_DETAIL'):
                    for o in words[:12]:
                        x, y = struct.unpack_from('<I', a, o)[0], struct.unpack_from('<I', b, o)[0]
                        fx, fy = struct.unpack('<f', a[o:o + 4])[0], struct.unpack('<f', b[o:o + 4])[0]
                        print(f'    {name}+0x{o:x}: ours 0x{x:08x} ({fx:.6g})  pcsx2 0x{y:08x} ({fy:.6g})')
                    print(f'    {name}: {len(words)} differing words')
        if mine.get('frame') != ref.get('frame'):
            diffs.append('frame')
        frame = frame_diff(preview, shot) if shot else None
        def close(x, y):
            return x is not None and y is not None and x[0] == y[0] and all(abs(u - v) <= 0.05 for u, v in zip(x[1:], y[1:]))
        positions = [k for k in ('fiona', 'hewie', 'stalker') if mine.get(k) != ref.get(k)]
        drift_only = all(close(mine.get(k), ref.get(k)) for k in positions)
        objects = [d for d in diffs if d not in ('fiona', 'hewie', 'stalker')]
        if not diffs and frame is not None and frame < 0.05:
            verdict = 'MATCH'
        elif drift_only and frame is not None and frame < 0.05 and all(d.endswith('-object') or d.startswith('snddrv') for d in objects):
            verdict = 'MATCH(float-drift)'
        else:
            verdict = 'DIFF'
        print(f'slot {slot:02}: {verdict}  frame {mine.get("frame")}  positions {"exact" if not positions else ("within 0.05" if drift_only else "DIFFER")}'
              f'  stalker {mine.get("stalker")} / {ref.get("stalker")}  objects {",".join(objects) or "-"}  frame_diff={frame:.1%}' if frame is not None else f'slot {slot:02}: {verdict} frame none', flush=True)
        if verdict == 'DIFF':
            print(f'    ours={mine}  pcsx2={ref}', flush=True)


if __name__ == '__main__':
    main()
