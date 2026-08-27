"""rda -- write a Rendeer application in Python.

    import rda

    app = rda.App("My application", 800, 600)

    @app.draw
    def draw(ui):
        ui.rect(0, 0, ui.width, ui.height, 0x1C202Cff)
        ui.text("Hello from Python", 40, 40, 0xF0F2F8ff)

    app.run()

There is no extension module to build and nothing here links the engine. A Rendeer
application is a *client*: it builds a description of its interface and sends it to the
shared runtime, which owns the device and does the drawing. This package speaks that wire
protocol directly, so Python is a first-class application exactly as C++ and JavaScript
are -- one graphics stack between all of them, whatever they happen to be written in.

---- what "commit" means here ----

Nothing a script does reaches the runtime the moment it is called. ui.rect() and ui.text()
append to a *queue*: vertices, indices and clipped draw commands accumulating in this
process. The frame is committed as one message when the draw function returns, and if the
resulting geometry is identical to the last frame's it is not sent at all -- an idle
application costs one small Present and nothing else.

That is not an implementation detail, it is the reason a script here cannot corrupt
anything. A queue that crosses a process boundary can be validated on arrival, and the
runtime does exactly that: every count, offset and index is checked against the bytes that
actually arrived before any of it is used, and a client that sends something impossible is
dropped rather than accommodated.

Contrast the engine's own in-process JavaScript, where `entity.position = ...` writes
straight into engine memory and is safe only because it runs inline on the loop thread.
That model does not survive leaving the process or the thread; this one is built for it.
"""
import os
import subprocess
import time

from . import _wire
from ._pipe import Channel, Disconnected

__all__ = ["App", "Ui", "Texture", "Font", "Disconnected", "DEFAULT_ENDPOINT"]

DEFAULT_ENDPOINT = "rendeer-runtime"


class Texture:
    """Pixels the runtime holds for this application.

    The id is real and scoped to this connection -- the runtime resolves it against this
    application alone, so a neighbour may be using the same number for something else and
    neither can reach the other's. Carrying it about is this library's job, not a script's.
    """

    def __init__(self, app, texture_id, width, height):
        self._app = app
        self.id = texture_id
        self.width = width
        self.height = height
        self._destroyed = False

    @property
    def ready(self):
        """Whether the runtime has confirmed it holds these pixels.

        Drawing with one it has not is refused outright, and a refused draw list drops the
        application -- so ask before the first ui.image().
        """
        return not self._destroyed and self.id in self._app._ready_textures

    def destroy(self):
        """Hands the pixels back.

        Disconnecting releases them anyway, since nothing else can name a texture
        belonging to this application. Guarded against a second call, because asking the
        runtime to forget an id it does not know is how a client gets dropped.
        """
        if self._destroyed or not self._app._channel.valid:
            return False
        self._destroyed = True
        self._app._channel.send(
            _wire.TEXTURE_DESTROY, _wire.TEXTURE_DESTROY_BODY.pack(self.id, 0))
        self._app._ready_textures.discard(self.id)
        return True

    def __repr__(self):
        state = "" if self.ready else ", not ready"
        return "Texture(id=%d, %dx%d%s)" % (self.id, self.width, self.height, state)


class Font:
    """The font the runtime will rasterise with, received when the surface is granted.

    A client has no device and therefore no atlas of its own, so this is the only way it
    can measure a string before drawing it -- and measuring against the very atlas the
    glyphs will be drawn from is what stops the two disagreeing.
    """

    def __init__(self):
        self.atlas_width = 0.0
        self.atlas_height = 0.0
        self.white_u = 0.0
        self.white_v = 0.0
        self.ascent = 0.0
        self.line_height = 0.0
        self.pixel_height = 0.0
        self.first_codepoint = 32
        self.glyphs = []

    @property
    def valid(self):
        return bool(self.glyphs)

    def advance(self, char):
        """Advance width of one character, in pixels."""
        code = ord(char)
        if code < self.first_codepoint:
            return 0.0
        index = code - self.first_codepoint
        if index >= len(self.glyphs):
            # Outside the baked range the atlas falls back to a space, and a client that
            # chose anything else would lay out text the runtime then drew differently.
            space = ord(" ") - self.first_codepoint
            return self.glyphs[space][6] if space < len(self.glyphs) else 0.0
        return self.glyphs[index][6]

    def measure(self, text):
        return sum(self.advance(c) for c in text)


class Ui:
    """The frame being built.

    Every call appends to the queue; none of it is sent until the frame is committed. The
    geometry is the same shape the engine's own interface layer produces, which is why the
    runtime can draw it through the identical code path.
    """

    def __init__(self, app):
        self._app = app
        self._vertices = bytearray()
        self._indices = bytearray()
        self._commands = []
        self._vertex_count = 0
        self._index_count = 0
        self._command_start = 0
        self._texture_id = 0
        self._clip_stack = []
        self._clip = (0.0, 0.0, 0.0, 0.0)

    # ---- what a script reads ----------------------------------------------------
    @property
    def width(self):
        return float(self._app.width)

    @property
    def height(self):
        return float(self._app.height)

    @property
    def pointer(self):
        return self._app._pointer

    @property
    def pointer_x(self):
        return self._app._pointer[0]

    @property
    def pointer_y(self):
        return self._app._pointer[1]

    @property
    def pointer_down(self):
        return self._app._pointer_down

    @property
    def pressed(self):
        return self._app._pressed

    @property
    def released(self):
        return self._app._released

    @property
    def scroll(self):
        return self._app._scroll

    @property
    def typed(self):
        return self._app._typed

    @property
    def time(self):
        return time.monotonic() - self._app._started

    @property
    def line_height(self):
        return self._app.font.line_height

    def measure(self, text):
        return self._app.font.measure(text)

    def hit(self, x, y, w, h):
        """Whether the pointer is inside that rectangle."""
        px, py = self._app._pointer
        return x <= px <= x + w and y <= py <= y + h

    # ---- building the queue ------------------------------------------------------
    @staticmethod
    def _pack_colour(rgba):
        """0xRRGGBBAA as a script writes it, to R in the low byte as the vertex wants."""
        r = (rgba >> 24) & 0xFF
        g = (rgba >> 16) & 0xFF
        b = (rgba >> 8) & 0xFF
        a = rgba & 0xFF
        return r | (g << 8) | (b << 16) | (a << 24)

    def _set_texture(self, texture_id):
        if texture_id == self._texture_id:
            return
        self._flush()          # one command is one texture
        self._texture_id = texture_id

    def _quad(self, x0, y0, x1, y1, u0, v0, u1, v1, packed):
        base = self._vertex_count
        # Indices are 16 bit, so this is a real ceiling rather than a guideline. Refusing
        # is better than wrapping, which would draw whatever sat at the low indices.
        if base + 4 > 0xFFFF:
            return
        corners = ((x0, y0, u0, v0), (x1, y0, u1, v0),
                   (x1, y1, u1, v1), (x0, y1, u0, v1))
        for (vx, vy, vu, vv) in corners:
            self._vertices += _wire.VERTEX.pack(vx, vy, vu, vv, packed, 0)
        for offset in (0, 1, 2, 0, 2, 3):
            self._indices += (base + offset).to_bytes(2, "little")
        self._vertex_count += 4
        self._index_count += 6

    def rect(self, x, y, w, h, colour=0xFFFFFFFF):
        """A filled rectangle."""
        if w <= 0.0 or h <= 0.0:
            return
        self._set_texture(0)
        # Solid fills sample the atlas's white texel, so they batch with text.
        font = self._app.font
        self._quad(x, y, x + w, y + h,
                   font.white_u, font.white_v, font.white_u, font.white_v,
                   self._pack_colour(colour))

    def text(self, value, x, y, colour=0xF0F2F8FF):
        """Draws a string with its top-left at (x, y). Returns the width drawn."""
        font = self._app.font
        if not font.valid or font.atlas_width <= 0.0 or font.atlas_height <= 0.0:
            return 0.0
        self._set_texture(0)   # glyphs come from the atlas
        packed = self._pack_colour(colour)
        # `y` is the top of the line, which is what a caller means; the glyph offsets are
        # relative to the baseline, so the ascent moves the pen down to it.
        baseline = y + font.ascent
        pen = x
        for char in value:
            code = ord(char)
            index = code - font.first_codepoint
            if code < font.first_codepoint or index >= len(font.glyphs):
                pen += font.advance(char)
                continue
            gx0, gy0, gx1, gy1, xoff, yoff, xadvance = font.glyphs[index]
            # A space has no box; advancing is all it does.
            if gx1 > gx0 and gy1 > gy0:
                left = pen + xoff
                top = baseline + yoff
                self._quad(left, top, left + (gx1 - gx0), top + (gy1 - gy0),
                           gx0 / font.atlas_width, gy0 / font.atlas_height,
                           gx1 / font.atlas_width, gy1 / font.atlas_height,
                           packed)
            pen += xadvance
        return pen - x

    def image(self, x, y, w, h, texture, tint=0xFFFFFFFF):
        """Draws a texture. `texture` is a Texture, or a raw id for anyone holding one."""
        if getattr(texture, "_destroyed", False):
            return
        texture_id = getattr(texture, "id", texture)
        if w <= 0.0 or h <= 0.0 or not texture_id:
            return
        self._set_texture(texture_id)
        self._quad(x, y, x + w, y + h, 0.0, 0.0, 1.0, 1.0, self._pack_colour(tint))

    def push_clip(self, x, y, w, h):
        """Restricts drawing to a rectangle, intersected with whatever is already set."""
        self._flush()
        self._clip_stack.append(self._clip)
        cx, cy, cw, ch = self._clip
        left, top = max(x, cx), max(y, cy)
        right, bottom = min(x + w, cx + cw), min(y + h, cy + ch)
        self._clip = (left, top, max(0.0, right - left), max(0.0, bottom - top))

    def pop_clip(self):
        if not self._clip_stack:
            return
        self._flush()
        self._clip = self._clip_stack.pop()

    # ---- committing ---------------------------------------------------------------
    def _flush(self):
        """Closes the command being built. One command is one texture and one clip."""
        if self._index_count <= self._command_start:
            return
        x, y, w, h = self._clip
        self._commands.append(_wire.COMMAND.pack(
            self._command_start, self._index_count - self._command_start,
            x, y, w, h, self._texture_id))
        self._command_start = self._index_count

    def _begin(self):
        self._clip = (0.0, 0.0, self.width, self.height)

    def _finish(self):
        self._flush()
        return (bytes(self._vertices), bytes(self._indices), b"".join(self._commands),
                self._vertex_count, self._index_count, len(self._commands))


class App:
    """A connection to the runtime, and the window it granted.

    Constructing one connects, starting the runtime if nothing is listening -- which is how
    the runtime comes to exist at all. Nobody launches it: the first application that wants
    it starts it, and every one after that shares it.
    """

    def __init__(self, title="Rendeer application", width=800, height=600,
                 endpoint=DEFAULT_ENDPOINT, runtime_path=None, start_timeout=30.0):
        self.title = title
        self.font = Font()
        self.width = width
        self.height = height
        self.client_id = 0
        self.surface_id = 1
        self.refusal = None

        self._channel = None
        self._draw_handler = None
        self._start_handler = None
        self._close_handler = None
        self._ready_textures = set()
        self._next_texture_id = 0
        self._surface_ready = False
        self._close_requested = False
        self._last_version = None
        self._frame_id = 0
        self._started = time.monotonic()

        self._pointer = (0.0, 0.0)
        self._pointer_down = False
        self._pressed = False
        self._released = False
        self._scroll = 0.0
        self._typed = ""

        self._connect(endpoint, runtime_path, start_timeout)
        self._create_surface(width, height, start_timeout)

    # ---- connecting ----------------------------------------------------------------
    def _connect(self, endpoint, runtime_path, timeout):
        self._channel = Channel.connect(endpoint)
        if self._channel is None:
            self._start_runtime(runtime_path)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                time.sleep(0.1)
                self._channel = Channel.connect(endpoint)
                if self._channel is not None:
                    break
        if self._channel is None:
            raise Disconnected("no runtime is listening, and one could not be started")

        self._channel.send(_wire.HELLO, _wire.HELLO_BODY.pack(
            _wire.PROTOCOL_MAJOR, _wire.PROTOCOL_MINOR, os.getpid(), 0))

        message_type, payload = self._await(_wire.WELCOME, _wire.REFUSED)
        if message_type == _wire.REFUSED:
            reason, major, minor = _wire.REFUSED_BODY.unpack(payload)
            self.refusal = reason
            self._channel.close()
            raise Disconnected(
                "the runtime refused this application (reason %d): it speaks %d.%d, this "
                "library speaks %d.%d" % (reason, major, minor,
                                          _wire.PROTOCOL_MAJOR, _wire.PROTOCOL_MINOR))
        major, _minor, self.client_id = _wire.WELCOME_BODY.unpack(payload)
        if not _wire.compatible(major):
            self._channel.close()
            raise Disconnected("runtime speaks protocol %d, this library speaks %d"
                               % (major, _wire.PROTOCOL_MAJOR))

    @staticmethod
    def _start_runtime(runtime_path):
        """Starts the runtime if nobody else has.

        Two applications racing to start one is expected and handled on the far side: the
        runtime holds a single-instance lock and the loser exits, so the winner serves
        everyone. Without that they would both succeed and clients would be split between
        two runtimes, each with its own device -- the duplication this exists to avoid.
        """
        path = runtime_path
        if path is None:
            here = os.path.dirname(os.path.abspath(__file__))
            candidates = (os.path.join(here, "RuntimeHost.exe"),
                          os.path.join(here, os.pardir, "RuntimeHost.exe"),
                          os.path.join(os.getcwd(), "RuntimeHost.exe"))
            for candidate in candidates:
                if os.path.isfile(candidate):
                    path = candidate
                    break
        if path is None or not os.path.isfile(path):
            raise Disconnected(
                "no runtime is listening and RuntimeHost.exe was not found; pass "
                "runtime_path=, or run from the directory holding it")
        # Detached, because the runtime outlives whichever application happened to start
        # it. That is the whole point of it being shared.
        detached = 0x00000008 | 0x08000000     # DETACHED_PROCESS | CREATE_NO_WINDOW
        subprocess.Popen([path], cwd=os.path.dirname(os.path.abspath(path)),
                         creationflags=detached)

    def _create_surface(self, width, height, timeout):
        self._channel.send(_wire.SURFACE_CREATE,
                           _wire.SURFACE_CREATE_BODY.pack(self.surface_id, width, height, 0))
        # Generous, because the wait is not for a message but for the runtime to build a
        # device, its pipelines and a font atlas for the first window anyone asks for.
        deadline = time.monotonic() + timeout
        while not self._surface_ready and time.monotonic() < deadline:
            self._pump()
            time.sleep(0.005)
        if not self._surface_ready:
            raise Disconnected("the runtime never provided a window")

    def _await(self, *types):
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if self._channel.pending():
                message_type, payload = self._channel.receive()
                if message_type in types:
                    return message_type, payload
            elif not self._channel.valid:
                raise Disconnected("the runtime closed the connection")
            else:
                time.sleep(0.002)
        raise Disconnected("the runtime did not answer in time")

    # ---- incoming --------------------------------------------------------------------
    def _pump(self):
        """Drains everything waiting: input the runtime saw, and its replies."""
        self._pressed = False
        self._released = False
        self._scroll = 0.0
        self._typed = ""
        while self._channel.valid and self._channel.pending():
            message_type, payload = self._channel.receive()
            if message_type == _wire.INPUT:
                self._on_input(payload)
            elif message_type == _wire.SURFACE_READY:
                _sid, self.width, self.height, _flags = \
                    _wire.SURFACE_READY_BODY.unpack(payload)
                self._surface_ready = True
                # The size granted may differ from the size asked for -- the window manager
                # has the final say -- so anything laid out against the old one is stale.
                self._last_version = None
            elif message_type == _wire.FONT_METRICS:
                self._on_font(payload)
            elif message_type == _wire.TEXTURE_READY:
                texture_id, ok = _wire.TEXTURE_READY_BODY.unpack(payload)
                if ok:
                    self._ready_textures.add(texture_id)
            elif message_type == _wire.CLOSE_REQUEST:
                self._close_requested = True

    def _on_input(self, payload):
        _sid, kind, _mods, x, y, codepoint, _pad = _wire.INPUT_BODY.unpack(payload)
        if kind == _wire.POINTER_MOVE:
            self._pointer = (x, y)
        elif kind == _wire.POINTER_DOWN:
            self._pointer = (x, y)
            self._pointer_down = True
            self._pressed = True
        elif kind == _wire.POINTER_UP:
            self._pointer = (x, y)
            self._pointer_down = False
            self._released = True
        elif kind == _wire.SCROLL:
            self._scroll += y
        elif kind == _wire.TEXT and codepoint:
            self._typed += chr(codepoint)

    def _on_font(self, payload):
        font = Font()
        (font.atlas_width, font.atlas_height, font.white_u, font.white_v,
         font.ascent, font.line_height, font.pixel_height,
         font.first_codepoint, glyph_count, _pad) = \
            _wire.FONT_METRICS_BODY.unpack_from(payload, 0)
        offset = _wire.FONT_METRICS_BODY.size
        font.glyphs = [_wire.GLYPH.unpack_from(payload, offset + i * _wire.GLYPH.size)
                       for i in range(glyph_count)]
        self.font = font

    # ---- textures --------------------------------------------------------------------
    def create_texture(self, width, height, pixels):
        """Uploads RGBA8 pixels and returns a Texture, or None if they were refused.

        `pixels` is any bytes-like holding width*height*4 bytes.
        """
        if width <= 0 or height <= 0:
            return None
        if width > _wire.MAX_TEXTURE_DIM or height > _wire.MAX_TEXTURE_DIM:
            return None
        expected = width * height * 4
        data = bytes(pixels)
        # Refused rather than padded: a short buffer means the caller and the size they
        # declared disagree, and uploading the difference as rubbish would hide that.
        if len(data) < expected:
            return None
        self._next_texture_id += 1
        texture_id = self._next_texture_id
        self._channel.send(_wire.TEXTURE_CREATE,
                           _wire.TEXTURE_CREATE_BODY.pack(texture_id, width, height, 0),
                           data[:expected])
        return Texture(self, texture_id, width, height)

    def load_texture(self, path):
        """Uploads an image file, if Pillow is installed to decode it.

        Optional on purpose: this package has no dependencies, and an application that
        builds its pictures rather than loading them should not acquire one.
        """
        try:
            from PIL import Image
        except ImportError:
            raise RuntimeError(
                "load_texture needs Pillow (pip install pillow); if you would rather not "
                "have it, build the pixels yourself and use create_texture")
        with Image.open(path) as image:
            rgba = image.convert("RGBA")
            return self.create_texture(rgba.width, rgba.height, rgba.tobytes())

    # ---- the loop --------------------------------------------------------------------
    def draw(self, handler):
        """Registers the per-frame drawing function. Usable as a decorator."""
        self._draw_handler = handler
        return handler

    def on_start(self, handler):
        """Registers a function to run once, before the first frame."""
        self._start_handler = handler
        return handler

    def on_close(self, handler):
        """Registers a function to run once, before the connection closes."""
        self._close_handler = handler
        return handler

    @property
    def closed(self):
        return self._close_requested or not self._channel.valid

    def frame(self):
        """Reads what the runtime sent and returns the Ui for this frame.

        Pair it with commit(). Use this directly when the application owns its own loop;
        run() is the same thing with the loop written for you.
        """
        self._pump()
        ui = Ui(self)
        ui._begin()
        return ui

    def commit(self, ui):
        """Sends one frame's queue.

        Geometry identical to the last frame is not resent -- the runtime keeps drawing
        what it already holds, so an idle application costs one Present. Present is always
        sent, because it is what marks the end of a frame.
        """
        vertices, indices, commands, vcount, icount, ccount = ui._finish()
        version = hash((vertices, indices, commands)) & 0xFFFFFFFFFFFFFFFF
        if version != self._last_version:
            self._channel.send(
                _wire.DRAW_LIST,
                _wire.DRAW_LIST_BODY.pack(self.surface_id, vcount, icount, ccount, version),
                vertices + indices + commands)
            self._last_version = version
        self._frame_id += 1
        self._channel.send(_wire.PRESENT,
                           _wire.PRESENT_BODY.pack(self.surface_id, 0, self._frame_id))

    def run(self, fps=60.0):
        """Runs until the runtime asks this application to close."""
        if self._start_handler:
            self._start_handler()
        period = 1.0 / fps if fps > 0 else 0.0
        try:
            while not self.closed:
                began = time.monotonic()
                ui = self.frame()
                if self._draw_handler:
                    self._draw_handler(ui)
                self.commit(ui)
                remaining = period - (time.monotonic() - began)
                if remaining > 0.0:
                    time.sleep(remaining)
        except Disconnected:
            pass          # the runtime went away; that is a close, not a failure
        finally:
            self.disconnect()

    def disconnect(self):
        if self._close_handler is not None:
            handler, self._close_handler = self._close_handler, None
            try:
                handler()
            except Disconnected:
                pass
        if self._channel is not None and self._channel.valid:
            try:
                self._channel.send(_wire.GOODBYE)     # a clean close, best effort
            except Disconnected:
                pass
            self._channel.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.disconnect()
