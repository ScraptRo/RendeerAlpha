"""The wire format, mirrored from RendeerAlpha/include/Runtime/Protocol.h.

Deliberately a transcription and nothing more: the C++ header is the definition, this is
a copy of it in another language, and the two only stay in step because every struct here
names the file it came from. `struct` format strings are written with explicit padding for
the same reason the C++ side writes explicit padding members -- what is sent is exactly
what is read, and a field that drifts is a field read as a different one.

Sizes are asserted at import. A mismatch here is not something to discover at runtime
against a live runtime; it is something to discover the moment the module loads.
"""
import struct

MAGIC = 0x41445221          # "RDA!"
PROTOCOL_MAJOR = 2
PROTOCOL_MINOR = 0

MAX_PAYLOAD_BYTES = 2048 * 2048 * 4 + 64 * 1024   # derived from kMaxTextureDim, as in C++
MAX_TEXTURE_DIM = 2048

# ---- message types ----------------------------------------------------------------
HELLO           = 1
SURFACE_CREATE  = 2
SURFACE_RESIZE  = 3
DRAW_LIST       = 4
PRESENT         = 5
GOODBYE         = 6
TEXTURE_CREATE  = 7
TEXTURE_DESTROY = 8

WELCOME       = 128
REFUSED       = 129
SURFACE_READY = 130
INPUT         = 131
CLOSE_REQUEST = 132
FONT_METRICS  = 133
TEXTURE_READY = 134

# ---- input kinds ------------------------------------------------------------------
POINTER_MOVE = 0
POINTER_DOWN = 1
POINTER_UP   = 2
SCROLL       = 3
KEY_DOWN     = 4
KEY_UP       = 5
TEXT         = 6

# ---- refusal reasons --------------------------------------------------------------
REFUSE_UNKNOWN          = 0
REFUSE_VERSION_MISMATCH = 1
REFUSE_TOO_MANY_CLIENTS = 2
REFUSE_UNAUTHORISED     = 3

# ---- structs ("<" everywhere: the wire is little-endian and fixed layout) ----------
HEADER          = struct.Struct("<IHHII")      # magic, type, flags, size, sequence
HELLO_BODY      = struct.Struct("<HHIQ")       # major, minor, clientPid, featureFlags
WELCOME_BODY    = struct.Struct("<HHI")        # major, minor, clientId
REFUSED_BODY    = struct.Struct("<IHH")        # reason, runtimeMajor, runtimeMinor
SURFACE_CREATE_BODY = struct.Struct("<IIII")   # surfaceId, width, height, flags
SURFACE_RESIZE_BODY = struct.Struct("<IIII")   # surfaceId, width, height, padding
SURFACE_READY_BODY  = struct.Struct("<IIII")   # surfaceId, width, height, flags
DRAW_LIST_BODY  = struct.Struct("<IIIIQ")      # surfaceId, vertex/index/command counts, drawVersion
PRESENT_BODY    = struct.Struct("<IIQ")        # surfaceId, padding, frameId
INPUT_BODY      = struct.Struct("<IHHffII")    # surfaceId, kind, modifiers, x, y, codepoint, padding
CLOSE_BODY      = struct.Struct("<II")         # surfaceId, padding
TEXTURE_CREATE_BODY  = struct.Struct("<IIII")  # textureId, width, height, format
TEXTURE_DESTROY_BODY = struct.Struct("<II")    # textureId, padding
TEXTURE_READY_BODY   = struct.Struct("<II")    # textureId, ok
FONT_METRICS_BODY = struct.Struct("<fffffffIII")
VERTEX  = struct.Struct("<ffffII")             # x, y, u, v, colour, padding
COMMAND = struct.Struct("<IIffffQ")            # indexOffset, indexCount, clip x/y/w/h, textureId
GLYPH   = struct.Struct("<HHHHfff")            # x0, y0, x1, y1, xoff, yoff, xadvance

# The C++ side static_asserts these. So does this, for the same reason: a layout that has
# drifted is worth failing on at import rather than halfway through a frame.
assert HEADER.size == 16, HEADER.size
assert VERTEX.size == 24, VERTEX.size
assert COMMAND.size == 32, COMMAND.size
assert GLYPH.size == 20, GLYPH.size
assert DRAW_LIST_BODY.size == 24, DRAW_LIST_BODY.size
assert HELLO_BODY.size == 16, HELLO_BODY.size
assert WELCOME_BODY.size == 8, WELCOME_BODY.size
assert FONT_METRICS_BODY.size == 40, FONT_METRICS_BODY.size


def compatible(major):
    """A stream is acceptable only when the major versions match exactly."""
    return major == PROTOCOL_MAJOR
