"""Drive the pinned PCSX2 oracle (docs/ORACLE.md) for save-based section checks.

Memory reads use PINE (127.0.0.1:28011; message = u32 size, u8 opcode, args; Read32 = 2,
Read64 = 3, Status = 15). Controller input is posted as key messages to the PCSX2 window
through its configured keyboard bindings (Cross K, Circle L, Start Return, D-pad arrows,
left stick WASD), so the user's focus is not taken. External reference only: nothing here
runs inside our build.
"""
import ctypes
import socket
import struct
import time
from ctypes import wintypes

PINE = ('127.0.0.1', 28011)
user32 = ctypes.WinDLL('user32', use_last_error=True)
WM_KEYDOWN, WM_KEYUP = 0x0100, 0x0101
VK = {'cross': 0x4B, 'circle': 0x4C, 'square': 0x4A, 'triangle': 0x49, 'start': 0x0D, 'select': 0x08,
      'up': 0x26, 'down': 0x28, 'left': 0x25, 'right': 0x27, 'l1': 0x51, 'r1': 0x45,   # arrows: ignored while inactive
      'lup': 0x57, 'ldown': 0x53, 'lleft': 0x41, 'lright': 0x44, 'shot': 0x77}   # shot = F8
EXTENDED = {0x25, 0x26, 0x27, 0x28}


class Pine:
    def __init__(self):
        self.s = socket.create_connection(PINE, timeout=5)

    def _call(self, opcode, payload=b''):
        msg = struct.pack('<IB', 5 + len(payload), opcode) + payload
        self.s.sendall(msg)
        head = self._recv(4)
        size, = struct.unpack('<I', head)
        body = self._recv(size - 4)
        if body[0] != 0:
            raise RuntimeError(f'PINE opcode {opcode} failed')
        return body[1:]

    def _recv(self, n):
        out = b''
        while len(out) < n:
            chunk = self.s.recv(n - len(out))
            if not chunk:
                raise ConnectionError('PINE closed')
            out += chunk
        return out

    def u32(self, address):
        return struct.unpack('<I', self._call(2, struct.pack('<I', address)))[0]

    def f32(self, address):
        return struct.unpack('<f', struct.pack('<I', self.u32(address)))[0]

    def block(self, address, words):
        """Many Read32 commands in one PINE message (one reply), so a few KB are read
        within a single game frame."""
        out = []
        for start in range(0, words, 4096):
            n = min(4096, words - start)
            body = b''.join(struct.pack('<B', 2) + struct.pack('<I', address + 4 * (start + k)) for k in range(n))
            self.s.sendall(struct.pack('<I', 4 + len(body)) + body)
            size, = struct.unpack('<I', self._recv(4))
            reply = self._recv(size - 4)
            if reply[0] != 0:
                raise RuntimeError('PINE batch read failed')
            out.append(reply[1:])
        return b''.join(out)

    def save_state(self, slot):
        self._call(9, bytes([slot]))

    def load_state(self, slot):
        self._call(10, bytes([slot]))

    def status(self):
        return struct.unpack('<I', self._call(15))[0]   # 0 running, 1 paused, 2 shutdown


def find_window(pid, title_part='Haunting'):
    """Visible top-level windows of process `pid` only (other apps may share the title)."""
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        n = user32.GetWindowTextLengthW(hwnd)
        if n:
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and title_part.lower() in buf.value.lower() and user32.IsWindowVisible(hwnd):
                found.append((hwnd, buf.value))
        return True
    user32.EnumWindows(each, 0)
    return found


def _lparam(vk, up):
    scan = user32.MapVirtualKeyW(vk, 0)
    lp = 1 | scan << 16
    if vk in EXTENDED:
        lp |= 1 << 24
    if up:
        lp |= 3 << 30
    return lp


def key(hwnd, name, down):
    """Hold (down=True) or release a bound key without the timed press."""
    user32.PostMessageW(hwnd, 0x0006, 1, 0)
    user32.PostMessageW(hwnd, 0x0007, 0, 0)
    vk = VK[name]
    user32.PostMessageW(hwnd, WM_KEYDOWN if down else WM_KEYUP, vk, _lparam(vk, not down))


def press(hwnd, name, hold=0.15):
    # Qt drops key events while its window is inactive; posted WM_ACTIVATE/WM_SETFOCUS make
    # it accept them without taking the real foreground from the user.
    user32.PostMessageW(hwnd, 0x0006, 1, 0)
    user32.PostMessageW(hwnd, 0x0007, 0, 0)
    vk = VK[name]
    user32.PostMessageW(hwnd, WM_KEYDOWN, vk, _lparam(vk, False))
    time.sleep(hold)
    user32.PostMessageW(hwnd, WM_KEYUP, vk, _lparam(vk, True))


def fiona(p):
    """Same fields the HG-DIAG-123 autopilot reads in our build."""
    f = p.u32(0x44F818) & 0x1FFFFFF
    if not f or p.u32(f) != 0x46AAB0:
        return None
    return {'room': p.u32(f + 0x30), 'x': p.f32(f + 0x10), 'y': p.f32(f + 0x14), 'z': p.f32(f + 0x18),
            'yaw': p.f32(f + 0x54)}


class Debug:
    """PCSX2-MCP DebugServer (127.0.0.1:21512): newline-delimited JSON requests
    {"cmd": ...} with {"ok": ..., "data"/"error": ...} replies. Used to pause the emulated
    EE exactly when the game writes a watched word (a memory write check)."""

    def __init__(self):
        import json
        self.json = json
        self.s = socket.create_connection(('127.0.0.1', 21512), timeout=10)
        self.buf = b''

    def call(self, **request):
        self.s.sendall(self.json.dumps(request).encode() + b'\n')
        while b'\n' not in self.buf:
            chunk = self.s.recv(65536)
            if not chunk:
                raise ConnectionError('DebugServer closed')
            self.buf += chunk
        line, self.buf = self.buf.split(b'\n', 1)
        reply = self.json.loads(line)
        if not reply.get('ok'):
            raise RuntimeError(f'DebugServer {request.get("cmd")}: {reply.get("error")}')
        return reply['data'] if 'data' in reply else reply   # read_memory replies carry 'hex'

    def read(self, address, length, cpu='ee'):
        out = b''
        for start in range(0, length, 0x10000):
            n = min(0x10000, length - start)
            out += bytes.fromhex(self.call(cmd='read_memory', cpu=cpu, address=address + start, length=n)['hex'])
        return out
