"""Compare a RESULTS.BIN from tools/hwtest/fpu_capture.c (real PS2 or an emulator) with
our FPU model, and fit the truncated-column Booth multiplier family (T = columns not
computed) to the multiply results. Usage: python tools/hwtest/analyze.py RESULTS.BIN

Record (12 words): a, b, c, mul(a,b), mul(b,a), div(a,b), madd(acc=c; a*b), vu0 vmul.x(a,b),
d, add(a,d), sub(a,d), sqrt(|a|).
"""
import struct
import sys


def exact_mul(a, b):
    ea, eb = (a >> 23) & 255, (b >> 23) & 255
    sign = (a ^ b) & 0x80000000
    if not ea or not eb:
        return sign
    p = ((a & 0x7FFFFF) | 0x800000) * ((b & 0x7FFFFF) | 0x800000)
    return pack(sign, ea + eb - 127, p)


def pack(sign, e, p):
    wide = (p >> 47) & 1
    e += wide
    if e <= 0:
        return sign
    if e > 255:
        return sign | 0x7FFFFFFF
    return sign | (e << 23) | ((p >> (24 if wide else 23)) & 0x7FFFFF)


def our_add(a, b):
    """Python transcription of hg::Fpu::add (runtime/include/hg/fpu.hpp): chopped alignment."""
    ea, eb = (a >> 23) & 255, (b >> 23) & 255
    if not ea:
        a &= 0x80000000
    if not eb:
        b &= 0x80000000
    if not ea and not eb:
        return a & b
    if not ea:
        return b
    if not eb:
        return a
    if (a & 0x7FFFFFFF) < (b & 0x7FFFFFFF):
        a, b, ea, eb = b, a, eb, ea
    ma, mb = (a & 0x7FFFFF) | 0x800000, (b & 0x7FFFFF) | 0x800000
    d = ea - eb
    mb = 0 if d >= 24 else mb >> d
    m = ma - mb if (a ^ b) & 0x80000000 else ma + mb
    if not m:
        return 0
    if m & 0x1000000:
        m >>= 1
        ea += 1
    while m < 0x800000 and ea > 0:
        m <<= 1
        ea -= 1
    sign = a & 0x80000000
    if ea > 255:
        return sign | 0x7FFFFFFF
    if ea <= 0:
        return sign
    return sign | (ea << 23) | (m & 0x7FFFFF)


def our_sqrt(a):
    """hg::Fpu::sqrt_exact for non-negative inputs: integer root, nearest."""
    e = (a >> 23) & 255
    if not e:
        return 0
    m = (a & 0x7FFFFF) | 0x800000
    odd = (e - 127) & 1
    rad = m << (24 if odd else 23)
    r = 0
    for bit in range(23, -1, -1):
        if (r | (1 << bit)) ** 2 <= rad:
            r |= 1 << bit
    if rad - r * r > r:
        r += 1
    ue = (e - 127 - odd) // 2 + 127
    if r >> 24:
        r >>= 1
        ue += 1
    return (ue << 23) | (r & 0x7FFFFF)


def booth_mul(a, b, T, width=52):
    """Radix-4 Booth on b (second operand); partial-product columns below T not computed."""
    ea, eb = (a >> 23) & 255, (b >> 23) & 255
    sign = (a ^ b) & 0x80000000
    if not ea or not eb:
        return sign
    ma, mb = (a & 0x7FFFFF) | 0x800000, (b & 0x7FFFFF) | 0x800000
    M = (1 << width) - 1
    total = 0
    for i in range(13):
        d = {0: 0, 1: 1, 2: 1, 3: 2, 4: -2, 5: -1, 6: -1, 7: 0}[((mb << 1) >> (2 * i)) & 7]
        if not d:
            continue
        s = 2 * i
        x = ma * abs(d)
        pp = (x << s) & M if d > 0 else ((~x) << s) & M
        if d < 0 and s >= T:
            pp = (pp + (1 << s)) & M
        pp &= ~((1 << T) - 1) & M
        total = (total + pp) & M
    return pack(sign, ea + eb - 127, total & ((1 << 48) - 1))


def main(path):
    data = open(path, 'rb').read()
    words = 12 if len(data) % 48 == 0 else 8
    recs = [struct.unpack_from(f'<{words}I', data, o) for o in range(0, len(data) - 4 * words + 1, 4 * words)]
    print(len(recs), 'records')
    order = sum(r[3] != r[4] for r in recs)
    print('mul(a,b) != mul(b,a):', order)
    print('mul(a,b) != exact chop:', sum(r[3] != exact_mul(r[0], r[1]) for r in recs))
    print('vu0 vmul != exact chop:', sum(r[7] != exact_mul(r[0], r[1]) for r in recs))
    print('vu0 vmul != fpu mul(a,b):', sum(r[7] != r[3] for r in recs))
    if words == 12:
        print('add(a,d) != our add:', sum(r[9] != our_add(r[0], r[8]) for r in recs))
        print('sub(a,d) != our add(a,-d):', sum(r[10] != our_add(r[0], r[8] ^ 0x80000000) for r in recs))
        print('sqrt != our sqrt:', sum(r[11] != our_sqrt(r[0] & 0x7FFFFFFF) for r in recs))
        bad = [r for r in recs if r[9] != our_add(r[0], r[8])][:5]
        for r in bad:
            print(f'  add {r[0]:08x} + {r[8]:08x}: file {r[9]:08x} ours {our_add(r[0], r[8]):08x}')
    fits = []
    for T in range(0, 26):
        bad = sum(booth_mul(r[0], r[1], T) != r[3] or booth_mul(r[1], r[0], T) != r[4] for r in recs)
        fits.append((bad, T))
    print('Booth model mismatches by T:', ' '.join(f'T{T}:{b}' for b, T in fits))


if __name__ == '__main__':
    main(sys.argv[1])
