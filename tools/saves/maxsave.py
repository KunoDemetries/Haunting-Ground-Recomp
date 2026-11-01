"""Unpack an Action Replay Max (.max, "Ps2PowerSave") PS2 save: header, then the save
directory's files LZARI-compressed (Okumura's LZ77 + adaptive arithmetic coder)."""
import struct
import sys
import os

N, F, THRESHOLD = 4096, 60, 2
M = 15
Q1, Q2, Q3, Q4 = 1 << M, 2 << M, 3 << M, 4 << M
MAX_CUM = Q1 - 1
N_CHAR = 256 - THRESHOLD + F


def lzari_decode(data, size, fill=0x20):
    pos = [0]
    nbits = len(data) * 8

    def bit():
        p = pos[0]
        pos[0] += 1
        if p >= nbits:
            return 0
        return (data[p >> 3] >> (7 - (p & 7))) & 1

    char_to_sym = [0] * N_CHAR
    sym_to_char = [0] * (N_CHAR + 1)
    sym_freq = [0] * (N_CHAR + 1)
    sym_cum = [0] * (N_CHAR + 1)
    position_cum = [0] * (N + 1)
    for s in range(N_CHAR, 0, -1):
        ch = s - 1
        char_to_sym[ch] = s
        sym_to_char[s] = ch
        sym_freq[s] = 1
        sym_cum[s - 1] = sym_cum[s] + 1
    sym_freq[0] = 0
    for i in range(N, 0, -1):
        position_cum[i - 1] = position_cum[i] + 10000 // (i + 200)

    def update(sym):
        if sym_cum[0] >= MAX_CUM:
            c = 0
            for i in range(N_CHAR, 0, -1):
                sym_cum[i] = c
                sym_freq[i] = (sym_freq[i] + 1) >> 1
                c += sym_freq[i]
            sym_cum[0] = c
        i = sym
        while sym_freq[i] == sym_freq[i - 1]:
            i -= 1
        if i < sym:
            ch_i, ch_s = sym_to_char[i], sym_to_char[sym]
            sym_to_char[i], sym_to_char[sym] = ch_s, ch_i
            char_to_sym[ch_i], char_to_sym[ch_s] = sym, i
        sym_freq[i] += 1
        i -= 1
        while i >= 0:
            sym_cum[i] += 1
            i -= 1

    st = {'low': 0, 'high': Q4, 'value': 0}
    for _ in range(M + 2):
        st['value'] = 2 * st['value'] + bit()

    def renorm():
        low, high, value = st['low'], st['high'], st['value']
        while True:
            if low >= Q2:
                value -= Q2; low -= Q2; high -= Q2
            elif low >= Q1 and high <= Q3:
                value -= Q1; low -= Q1; high -= Q1
            elif high > Q2:
                break
            low += low; high += high; value = 2 * value + bit()
        st['low'], st['high'], st['value'] = low, high, value

    def decode_char():
        rng = st['high'] - st['low']
        x = ((st['value'] - st['low'] + 1) * sym_cum[0] - 1) // rng
        i, j = 1, N_CHAR
        while i < j:
            k = (i + j) // 2
            if sym_cum[k] > x:
                i = k + 1
            else:
                j = k
        sym = i
        low = st['low']
        st['high'] = low + rng * sym_cum[sym - 1] // sym_cum[0]
        st['low'] = low + rng * sym_cum[sym] // sym_cum[0]
        renorm()
        ch = sym_to_char[sym]
        update(sym)
        return ch

    def decode_position():
        rng = st['high'] - st['low']
        x = ((st['value'] - st['low'] + 1) * position_cum[0] - 1) // rng
        i, j = 1, N
        while i < j:
            k = (i + j) // 2
            if position_cum[k] > x:
                i = k + 1
            else:
                j = k
        p = i - 1
        low = st['low']
        st['high'] = low + rng * position_cum[p] // position_cum[0]
        st['low'] = low + rng * position_cum[p + 1] // position_cum[0]
        renorm()
        return p

    text = bytearray([fill]) * N
    r = N - F
    out = bytearray()
    while len(out) < size:
        c = decode_char()
        if c < 256:
            out.append(c)
            text[r] = c
            r = (r + 1) & (N - 1)
        else:
            i = (r - decode_position() - 1) & (N - 1)
            for k in range(c - 255 + THRESHOLD):
                b = text[(i + k) & (N - 1)]
                out.append(b)
                text[r] = b
                r = (r + 1) & (N - 1)
    return bytes(out[:size])


def unpack(path):
    d = open(path, 'rb').read()
    if d[:12] != b'Ps2PowerSave':
        raise ValueError('not a .max save')
    crc, = struct.unpack_from('<I', d, 12)
    dirname = d[16:48].split(b'\0')[0].decode('ascii', 'replace')
    iconsys = d[48:80].split(b'\0')[0].decode('ascii', 'replace')
    csize, nfiles, usize = struct.unpack_from('<3I', d, 80)
    body = d[92:92 + csize]
    # some writers count the compressed size including a 4-byte length prefix
    raw = lzari_decode(body[4:] if struct.unpack_from('<I', body, 0)[0] == usize else body, usize)
    files, o = [], 0
    for _ in range(nfiles):
        size, = struct.unpack_from('<I', raw, o)
        name = raw[o + 4:o + 36].split(b'\0')[0].decode('ascii', 'replace')
        o += 36
        files.append((name, raw[o:o + size]))
        o += size
        o = (o + 8 + 15) // 16 * 16 - 8   # entries align so that data starts 16-aligned
    return dirname, iconsys, files, crc


if __name__ == '__main__':
    dirname, iconsys, files, crc = unpack(sys.argv[1])
    print('directory', dirname, 'icon', iconsys, 'files', len(files))
    out = os.path.join(os.path.dirname(sys.argv[1]), dirname)
    os.makedirs(out, exist_ok=True)
    for name, data in files:
        print(f'  {len(data):8} {name}')
        open(os.path.join(out, name), 'wb').write(data)
