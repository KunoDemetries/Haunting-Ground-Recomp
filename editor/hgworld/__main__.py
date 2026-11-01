"""World-editor tooling (docs/WORLD_EDITOR.md). Read-only on the user's disc files."""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from hgworld.archive import Archive, DEFAULT_ARCHIVE, DEFAULT_ORIGIN  # noqa: E402
from hgworld.archive import checked_output  # noqa: E402
from hgworld.pac import RoomPac, ROOM_LIMIT, SLOT_COUNT, SLOT_NOTES, room_path  # noqa: E402
from hgworld import doors, layout, tex, text  # noqa: E402


def default_mods():
    import os
    local = os.environ.get('LOCALAPPDATA')
    return Path(local) / 'HauntingGroundRecomp' / 'mods' if local else Path('hg-world-editor-mods')


def room_id(text):
    value = int(text, 16)
    if not 0 <= value < ROOM_LIMIT:
        raise argparse.ArgumentTypeError(f'room id must be 0..0x{ROOM_LIMIT - 1:x}')
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(prog='world.py', description=__doc__)
    parser.add_argument('--archive', type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument('--volume-offset', type=int, default=DEFAULT_ORIGIN)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('extract', help='copy disc files whose path contains MATCH')
    p.add_argument('match')
    p.add_argument('--out', type=Path, required=True,
                   help='outside the repository, or under out/')
    sub.add_parser('rooms', help='list every room file and its present slots')
    p = sub.add_parser('room', help='show one room file\'s section table')
    p.add_argument('room', type=room_id, help='hex room id, e.g. 21')
    p = sub.add_parser('textures', help='export a texture bank (.TEX file or room slot 9) as PNGs')
    p.add_argument('source', help='disc path containing this text (e.g. FIN_000.TEX) or room:<hex id>')
    p.add_argument('--out', type=Path, required=True,
                   help='outside the repository, or under out/')
    p = sub.add_parser('doors', help='print every room\'s exits from the executable\'s door table')
    p.add_argument('--elf', type=Path, default=Path(__file__).resolve().parents[2] / 'Haunting Ground (USA)' / 'SLUS_210.75')
    p = sub.add_parser('text', help="print a room's message text (slot 10)")
    p.add_argument('room', type=room_id, help='hex room id, e.g. 21')
    p.add_argument('--dump', type=Path, help='write the messages to an editable text file instead')
    p = sub.add_parser('text-apply', help='put edited messages into the room (saved in the mods folder)')
    p.add_argument('room', type=room_id, help='hex room id, e.g. 21')
    p.add_argument('file', type=Path, help='a file written by `text --dump` and then edited')
    p.add_argument('--mods', type=Path, default=default_mods(),
                   help='the editor\'s mods folder (default %%LOCALAPPDATA%%\\HauntingGroundRecomp\\mods)')
    p = sub.add_parser('path', help='autopilot lines (HG-DIAG-123) walking a room from X Z to a door or point')
    p.add_argument('room', type=room_id, help='hex room id, e.g. 21')
    p.add_argument('x', type=float)
    p.add_argument('z', type=float)
    p.add_argument('--door', type=int, help='slot-7 portal number')
    p.add_argument('--to', type=float, nargs=2, metavar=('X', 'Z'))
    p.add_argument('--list', action='store_true', help='list the room portals instead')
    args = parser.parse_args(argv)

    archive = Archive(args.archive, args.volume_offset)
    if args.command == 'extract':
        written = archive.extract(args.match, args.out)
        for path, size in written:
            print(f'{size:10} {path}')
        print(f'{len(written)} files written under {args.out}')
        return 0 if written else 1
    if args.command == 'path':
        from hgworld import route
        if args.list:
            tris, portals = route.load_room(archive, args.room)
            for k, portal in enumerate(portals):
                cx, cz = route._centroid(tris[portal['triangle']])
                print(f'door {k}: trigger triangle {portal["triangle"]} at {cx:.2f} {cz:.2f}')
            return 0
        if (args.door is None) == (args.to is None):
            parser.error('give --door N or --to X Z')
        for line in route.script(archive, args.room, (args.x, args.z), args.door, tuple(args.to) if args.to else None):
            print(line)
        return 0
    if args.command == 'rooms':
        rooms = 0
        for room in range(ROOM_LIMIT):
            path = room_path(room)
            if path not in archive.entries:
                continue
            pac = RoomPac(archive.read(path))
            slots = ''.join('x' if s.present and s.size else ('0' if s.present else '.')
                            for s in pac.sections)
            print(f'room {room:03X}  {len(pac.data):8}  slots {slots}')
            rooms += 1
        print(f'{rooms} rooms (x = data, 0 = empty, . = absent; slots 0..{SLOT_COUNT - 1})')
        return 0
    if args.command == 'room':
        path = room_path(args.room)
        if path not in archive.entries:
            print(f'no room file {path}')
            return 1
        pac = RoomPac(archive.read(path))
        print(f'{path}  {len(pac.data)} bytes')
        for s in pac.sections:
            if not s.present:
                print(f'  slot {s.slot:2}  absent')
                continue
            alias = pac.aliases(s.slot)
            note = SLOT_NOTES.get(s.slot, '')
            extra = f'  same data as slot {alias}' if alias else ''
            print(f'  slot {s.slot:2}  offset 0x{s.offset:07x}  size 0x{s.size:06x}{extra}  {note}')
        return 0
    if args.command == 'doors':
        links = doors.door_table(args.elf.read_bytes())
        for room in range(ROOM_LIMIT):
            path = room_path(room)
            if path not in archive.entries:
                continue
            modelled = len(layout.portals(RoomPac(archive.read(path)).section(7)))
            exits = []
            for door in range(8):
                other, link = doors.destination(links, room, door)
                if other:
                    star = '' if door < modelled else '*'
                    exits.append(f'{door}->{other["room"]:03X}{star}')
            print(f'room {room:03X}: ' + (' '.join(exits) if exits else 'no exits listed'))
        print(f'{len(links)} door-table entries (* = exit without a door model)')
        return 0
    if args.command == 'text':
        path = room_path(args.room)
        if path not in archive.entries:
            print(f'no room file {path}')
            return 1
        slot10 = RoomPac(archive.read(path)).section(10)
        lines = [f'{i:03}: {text.readable(raw)}' for i, raw in enumerate(text.messages(slot10))]
        if args.dump:
            header = ['# Room %03X messages. Edit the text after "NNN: ". \\n = line break, \\p = page break,'
                      % args.room, '# {XX} = control byte; keep the codes. Lines starting with # are ignored.',
                      f'# The room\'s text may grow by at most {text.spare(slot10)} bytes in total '
                      '(shorten other messages to make space).']
            args.dump.write_text('\n'.join(header + lines) + '\n', encoding='utf-8')
            print(f'{len(lines)} messages written to {args.dump}')
        else:
            print('\n'.join(lines))
        return 0
    if args.command == 'text-apply':
        path = room_path(args.room)
        if path not in archive.entries:
            print(f'no room file {path}')
            return 1
        target = checked_output(args.mods) / path.lstrip('/').split(';')[0]
        base = target.read_bytes() if target.exists() else archive.read(path)
        if len(base) != archive.entries[path].size:
            raise ValueError(f'{target} is not the same size as the disc copy')
        pac = RoomPac(base)
        current = text.messages(pac.section(10))
        edited = list(current)
        for number, line in enumerate(args.file.read_text(encoding='utf-8').splitlines(), 1):
            if not line.strip() or line.startswith('#'):
                continue
            index, sep, body = line.partition(': ')
            if not sep or not index.isdigit() or int(index) >= len(current):
                raise ValueError(f'{args.file}:{number}: expected "NNN: text" with NNN below {len(current)}')
            edited[int(index)] = text.from_readable(body)
        changed = sum(1 for a, b in zip(current, edited) if a != b)
        slot = pac.sections[10]
        new_slot = text.encode(pac.section(10), edited)
        data = bytearray(base)
        data[slot.offset:slot.offset + slot.size] = new_slot
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(bytes(data))
        print(f'{changed} message(s) changed; saved {target} ({text.spare(new_slot)} spare bytes left). '
              'Open the editor and press Ctrl+E to export the modded disc.')
        return 0
    if args.command == 'textures':
        if args.source.lower().startswith('room:'):
            room = room_id(args.source[5:])
            path = room_path(room)
            banks = [(f'room_{room:03X}', RoomPac(archive.read(path)).section(9))]
        else:
            banks = [(e.path.lstrip('/').split(';')[0].replace('/', '_').rsplit('.', 1)[0], archive.read(e))
                     for e in archive.files(args.source) if e.path.upper().endswith('.TEX;1')]
        root = checked_output(args.out)
        root.mkdir(parents=True, exist_ok=True)
        count = 0
        for name, data in banks:
            if not data:
                continue
            for t in tex.parse(data):
                target = root / f'{name}_{t.index:02}.png'
                target.write_bytes(tex.png(t.width, t.height, tex.rgba(data, t)))
                count += 1
        print(f'{count} textures from {len(banks)} banks written under {args.out}')
        return 0 if count else 1
    return 2


if __name__ == '__main__':
    sys.exit(main())
