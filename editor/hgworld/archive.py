"""Read-only access to the user's DATA.CVM through the independent ECMA-119 reader.

Extraction targets must stay out of version control: outside the repository, or
under the ignored `out/` folder (AGENTS.md hard rule on game-derived files).
"""
from pathlib import Path
import sys

REPO = Path(__file__).resolve().parents[2]
# The only shared code: the project's own read-only disc reader (no game runtime).
sys.path.insert(0, str(REPO / 'tools'))
from hgtool.volume import Volume  # noqa: E402

DEFAULT_ARCHIVE = REPO / 'Haunting Ground (USA)' / 'DATA.CVM'
DEFAULT_ORIGIN = 6144   # CVM header precedes the ISO volume (out/assets.json "origin")


def checked_output(path):
    target = Path(path).resolve()
    try:
        relative = target.relative_to(REPO)
    except ValueError:
        return target
    if relative.parts[:1] == ('out',):
        return target
    raise ValueError('extracted game files must go outside the repository or under out/')


class Archive:
    def __init__(self, path=DEFAULT_ARCHIVE, origin=DEFAULT_ORIGIN):
        self.volume = Volume(path, origin)
        self.entries = self.volume.index()

    def files(self, match=''):
        needle = match.upper()
        return [e for p, e in sorted(self.entries.items()) if not e.directory and needle in p.upper()]

    def read(self, entry):
        if isinstance(entry, str):
            entry = self.entries[entry]
        return b''.join(self.volume.chunks(entry))

    def extract(self, match, out):
        root = checked_output(out)
        written = []
        for entry in self.files(match):
            target = root / entry.path.lstrip('/').split(';')[0]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(self.read(entry))
            written.append((entry.path, entry.size))
        return written
