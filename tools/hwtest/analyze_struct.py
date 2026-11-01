"""Summarize a STRUCT.BIN from tools/hwtest/struct_capture.c (real PS2 or an emulator).
Record (4 words): op, a, b, result; op 1 div.s, 2 sqrt.s, 3 mul.s, 4 rsqrt.s;
SPU2 envelope probe: op 5 key-on / 6 key-off {op, ADSR1<<16|ADSR2, microseconds, ENVX},
op 7 {7, load result, module result, 0} if the probe could not run.
Prints, per op, how often the result equals the chopped exact value, is one above or one
below it; with --sweeps, prints each fixed-divisor DIV sweep as a string of
'.' (chop) / '+' (chop+1) / '?' (other), one character per consecutive dividend.
Usage: python tools/hwtest/analyze_struct.py STRUCT.BIN [--sweeps]
"""
import collections
import struct
import sys
from math import isqrt


def mant(x):
    return (x & 0x7FFFFF) | 0x800000


def exact(op, a, b):
    """Chopped exact significand (24 bits) for the record's operation."""
    if op == 1:
        ma, mb = mant(a), mant(b)
        return (ma << (23 if ma >= mb else 24)) // mb
    if op == 2:
        e = (a >> 23) & 255
        return isqrt(mant(a) << (24 if (e - 127) & 1 else 23))
    if op == 3:
        p = mant(a) * mant(b)
        return p >> (24 if p >> 47 else 23)
    if op == 4:   # 1 / sqrt(b) (a = 1.0 here): 2^28 / sqrt(m * 2^(p-23)), normalized to 24 bits
        e = (b >> 23) & 255
        q, shift = isqrt((1 << (79 - ((e - 127) & 1))) // mant(b)), 0
        while q >> 24:
            q, shift = q >> 1, shift + 1
        return ((shift - 28 + 23 + 127 - (e - 127) // 2) << 23) | (q & 0x7FFFFF)   # full bits
    return None


def main():
    data = open(sys.argv[1], 'rb').read()
    words = struct.unpack('<%dI' % (len(data) // 4), data)
    stats = collections.Counter()
    sweeps = collections.OrderedDict()
    for i in range(0, len(words), 4):
        op, a, b, r = words[i:i + 4]
        if not op:
            continue
        if op >= 5:
            if op == 7:
                print('SPU2 probe did not run: load %d, module result %d' % (a if a < 1 << 31 else a - (1 << 32), b))
            else:
                sweeps.setdefault(('S', a & 0xFFFF), {5: [], 6: []})[op].append((b, r))
            continue
        ref = exact(op, a, b)
        d = (r & 0x7FFFFFFF) - ref if op == 4 else mant(r) - ref   # positive float bits are ordered
        tag = '.' if d == 0 else '+' if d == 1 else '-' if d == -1 else '?'
        stats[(op, tag)] += 1
        if op == 1:
            sweeps.setdefault(('D', mant(b)), []).append(tag)
    for op, name in ((1, 'div'), (2, 'sqrt'), (3, 'mul'), (4, 'rsqrt')):
        print(name, {t: stats[(op, t)] for t in '.+-?' if stats[(op, t)]})
    for key, phases in sweeps.items():
        if key[0] == 'S':
            on, off = phases[5], phases[6]
            show = lambda xs, picks: ' '.join('%dms:%04x' % (xs[j][0] // 1000, xs[j][1]) for j in picks if j < len(xs))
            print('ADSR2 %04x key-on %s | key-off %s' % (key[1], show(on, (0, 1, 5, 25, 50, 99)), show(off, (0, 4, 16, 32, 63))))
    if '--sweeps' in sys.argv:
        for key, tags in sweeps.items():
            if key[0] != 'D':
                continue
            print('%06x' % key[1], ''.join(tags[:200]))


if __name__ == '__main__':
    main()
