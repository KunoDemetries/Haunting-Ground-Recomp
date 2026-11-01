"""Trace EE words on the PCSX2 oracle every K game frames after loading a save slot,
matching HG_WATCH_WORDS/HG_WATCH_EVERY in our build (HG-DIAG-124).
usage: python tools/oracle/watch_trace.py SLOT END_FRAME EVERY addr,addr,... [EXPECTED_ROOM]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import pcsx2_drive as drive  # noqa: E402
import section_check as sc  # noqa: E402


def main():
    slot, end, every = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    addrs = [int(a, 0) for a in sys.argv[4].split(',')]
    o = sc.Oracle()
    hwnd, p = o.hwnd, o.p
    o.load(slot, int(sys.argv[5], 0) if len(sys.argv) > 5 else None)
    last = None
    deadline = time.time() + end / 30 + 120
    while time.time() < deadline:
        f = p.u32(sc.COUNTER)
        if f < 0x100000 and f != last and f % every == 0:
            last = f
            print('watch frame=%d %s' % (f, ' '.join('%x' % p.u32(a) for a in addrs)), flush=True)
            if f >= end:
                break
        time.sleep(0.002)


if __name__ == '__main__':
    main()
