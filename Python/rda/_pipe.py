"""The named-pipe channel, mirroring RendeerAlpha/src/Runtime/Transport.cpp.

ctypes rather than open(): the reads have to be non-blocking, and the only way to ask a
Windows pipe whether anything is waiting is PeekNamedPipe. A plain file object would block
in read() and a frame loop cannot afford that -- which is the same reason the C++ Channel
peeks before it receives.
"""
import ctypes
import ctypes.wintypes as wt

from . import _wire

_k32 = ctypes.WinDLL("kernel32", use_last_error=True)

GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE = wt.HANDLE(-1).value
ERROR_PIPE_BUSY = 231

_k32.CreateFileW.restype = wt.HANDLE
_k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p,
                             wt.DWORD, wt.DWORD, wt.HANDLE]
_k32.ReadFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD,
                          ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
_k32.WriteFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD,
                           ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
_k32.PeekNamedPipe.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD,
                               ctypes.POINTER(wt.DWORD), ctypes.POINTER(wt.DWORD),
                               ctypes.POINTER(wt.DWORD)]
_k32.WaitNamedPipeW.argtypes = [wt.LPCWSTR, wt.DWORD]
_k32.CloseHandle.argtypes = [wt.HANDLE]


def _pipe_path(name):
    r"""\\.\pipe\<name>, in one place so both ends cannot disagree about it."""
    return "\\\\.\\pipe\\" + name


class Disconnected(Exception):
    """The runtime went away, or sent something that is not a Rendeer stream."""


class Channel:
    """One connection. Frames messages exactly as Transport.cpp does."""

    def __init__(self, handle):
        self._handle = handle
        self._sequence = 0

    @classmethod
    def connect(cls, endpoint, timeout_ms=200):
        path = _pipe_path(endpoint)
        while True:
            handle = _k32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, None,
                                      OPEN_EXISTING, 0, None)
            if handle != INVALID_HANDLE:
                return cls(handle)
            # A pipe that exists but has every instance busy is a wait, not a failure.
            if ctypes.get_last_error() != ERROR_PIPE_BUSY:
                return None
            if not _k32.WaitNamedPipeW(path, timeout_ms):
                return None

    @property
    def valid(self):
        return self._handle is not None

    def close(self):
        if self._handle is not None:
            _k32.CloseHandle(self._handle)
            self._handle = None

    # ---- raw io ---------------------------------------------------------------
    def _write_all(self, data):
        view = memoryview(data)
        while view:
            written = wt.DWORD(0)
            ok = _k32.WriteFile(self._handle, ctypes.c_char_p(view.tobytes()),
                                len(view), ctypes.byref(written), None)
            if not ok or written.value == 0:
                self.close()
                raise Disconnected("write failed")
            view = view[written.value:]

    def _read_all(self, count):
        out = bytearray()
        buffer = ctypes.create_string_buffer(count)
        while len(out) < count:
            got = wt.DWORD(0)
            ok = _k32.ReadFile(self._handle, buffer, count - len(out),
                               ctypes.byref(got), None)
            if not ok or got.value == 0:
                self.close()
                raise Disconnected("the runtime closed the connection")
            out += buffer.raw[:got.value]
        return bytes(out)

    def pending(self):
        """Whether a message is waiting. Closes the channel if the peer has gone."""
        if self._handle is None:
            return False
        available = wt.DWORD(0)
        ok = _k32.PeekNamedPipe(self._handle, None, 0, None,
                                ctypes.byref(available), None)
        if not ok:
            # A failed peek means the far end is gone rather than merely quiet, which is
            # what turns a vanished runtime into a clean shutdown instead of a hang.
            self.close()
            return False
        return available.value > 0

    # ---- messages -------------------------------------------------------------
    def send(self, message_type, body=b"", tail=b""):
        if not self.valid:
            raise Disconnected("channel is closed")
        total = len(body) + len(tail)
        if total > _wire.MAX_PAYLOAD_BYTES:
            raise ValueError("payload of %d bytes is past the protocol's cap" % total)
        self._sequence += 1
        header = _wire.HEADER.pack(_wire.MAGIC, message_type, 0, total, self._sequence)
        # Header, body and tail go out back to back: a reader frames on size, so a torn
        # write would desynchronise the stream.
        self._write_all(header + body + tail)

    def receive(self):
        """The next message as (type, payload). Blocks; check pending() first."""
        magic, message_type, _flags, size, _sequence = _wire.HEADER.unpack(
            self._read_all(_wire.HEADER.size))
        # Anything wrong with the envelope means the stream is no longer trustworthy, and
        # there is no resynchronising a byte stream whose framing is in doubt.
        if magic != _wire.MAGIC or size > _wire.MAX_PAYLOAD_BYTES:
            self.close()
            raise Disconnected("bad framing from the runtime")
        payload = self._read_all(size) if size else b""
        return message_type, payload
