"""Write a save directory's files over the same-sized files already on a raw PS2 memory card
image (512-byte pages + 16 ECC bytes, as the game's MCMAN wrote them), following the existing
FAT chains, and recompute each touched page's ECC. The ECC function is first checked against
every written page of the source card.

usage: mcinject.py CARD_IN CARD_OUT SAVE_DIR   (SAVE_DIR holds the unpacked files)
       mcinject.py --check CARD
"""
import os
import struct
import sys

PAGE, RAW = 512, 528


def parity(x):
    x ^= x >> 4
    x ^= x >> 2
    x ^= x >> 1
    return x & 1


# Column parity bits of one byte: even/odd bit groups at three granularities.
COLUMN = [parity(b & 0x55) | parity(b & 0x33) << 1 | parity(b & 0x0F) << 2 |
          parity(b & 0xAA) << 4 | parity(b & 0xCC) << 5 | parity(b & 0xF0) << 6 for b in range(256)]


def ecc_chunk(chunk):
    """3-byte Hamming code of a 128-byte chunk (column parity, line parity, inverted line parity)."""
    cp, lp0, lp1 = 0x77, 0x7F, 0x7F
    for i, b in enumerate(chunk):
        cp ^= COLUMN[b]
        if parity(b):
            lp0 ^= ~i & 0x7F
            lp1 ^= i
    return bytes([cp, lp0, lp1])


def ecc_page(page):
    return b''.join(ecc_chunk(page[k:k + 128]) for k in range(0, PAGE, 128)) + b'\0' * 4


def check(card):
    good = bad = 0
    for n in range(len(card) // RAW):
        raw = card[n * RAW:(n + 1) * RAW]
        if raw == b'\xff' * RAW:
            continue
        if ecc_page(raw[:PAGE]) == raw[PAGE:]:
            good += 1
        else:
            bad += 1
            if bad <= 3:
                print('mismatch page', n, raw[PAGE:].hex(), ecc_page(raw[:PAGE]).hex())
    return good, bad


class Card:
    def __init__(self, data):
        self.d = bytearray(data)
        sb = self.page(0)
        if not sb.startswith(b'Sony PS2 Memory Card Format'):
            raise ValueError('not a formatted PS2 memory card')
        self.ppc = struct.unpack_from('<H', sb, 0x2A)[0]
        self.alloc = struct.unpack_from('<I', sb, 0x34)[0]
        ifc = struct.unpack_from('<I', sb, 0x50)[0]
        fat_clusters = [c for c in struct.unpack_from('<256I', self.cluster(ifc)) if c not in (0, 0xFFFFFFFF)]
        self.fat = b''.join(self.cluster(c) for c in fat_clusters)

    def page(self, n):
        return bytes(self.d[n * RAW:n * RAW + PAGE])

    def write_page(self, n, data):
        assert len(data) == PAGE
        self.d[n * RAW:n * RAW + PAGE] = data
        self.d[n * RAW + PAGE:(n + 1) * RAW] = ecc_page(data)

    def cluster(self, c):
        return b''.join(self.page(c * self.ppc + k) for k in range(self.ppc))

    def chain(self, c):
        out = [c]
        while True:
            n = struct.unpack_from('<I', self.fat, c * 4)[0]
            if n == 0xFFFFFFFF:
                return out
            if not n & 0x80000000:
                raise ValueError('broken FAT chain')
            c = n & 0x7FFFFFFF
            out.append(c)

    def entries(self, cluster, count):
        data = b''.join(self.cluster(self.alloc + c) for c in self.chain(cluster))
        for i in range(count):
            e = data[i * 512:(i + 1) * 512]
            mode, _, length = struct.unpack_from('<HHI', e, 0)
            first = struct.unpack_from('<I', e, 0x10)[0]
            yield mode, length, first, e[0x40:0x60].split(b'\0')[0].decode('ascii', 'replace')

    def find_dir(self, name):
        count = next(self.entries(0, 1))[1]
        for mode, length, first, n in self.entries(0, count):
            if n == name and mode & 0x20:
                return {f: (l, c) for m, l, c, f in self.entries(first, length) if m & 0x10}
        raise KeyError(name)

    def overwrite(self, length_and_cluster, data):
        length, first = length_and_cluster
        if length != len(data):
            raise ValueError(f'size differs: card {length}, save {len(data)}')
        cs = PAGE * self.ppc
        for k, c in enumerate(self.chain(first)):
            block = data[k * cs:(k + 1) * cs]
            if not block:
                break
            block = block + bytes(self.page((self.alloc + c) * self.ppc + len(block) // PAGE))[:0]
            for p in range(self.ppc):
                part = data[k * cs + p * PAGE:k * cs + (p + 1) * PAGE]
                if not part:
                    break
                old = self.page((self.alloc + c) * self.ppc + p)
                self.write_page((self.alloc + c) * self.ppc + p, part + old[len(part):])


if __name__ == '__main__':
    if sys.argv[1] == '--check':
        print('ECC good/bad pages:', check(open(sys.argv[2], 'rb').read()))
        sys.exit(0)
    src, dst, save = sys.argv[1:4]
    raw = open(src, 'rb').read()
    good, bad = check(raw)
    print('source card ECC good/bad pages:', good, bad)
    if bad:
        sys.exit('ECC function does not match the source card; not writing')
    card = Card(raw)
    name = os.path.basename(os.path.normpath(save))
    files = card.find_dir(name)
    for f in sorted(os.listdir(save)):
        data = open(os.path.join(save, f), 'rb').read()
        card.overwrite(files[f], data)
        print('wrote', f, len(data))
    open(dst, 'wb').write(card.d)
    print('card written:', dst, 'ECC good/bad:', check(bytes(card.d)))
