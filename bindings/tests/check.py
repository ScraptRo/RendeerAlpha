"""What the Python binding does, checked against a running engine.

Run by ctest as `rda_python_binding`. It opens a real window on a real device, because
that is what the thing under test does -- a binding that cannot be exercised without one
is a binding whose bugs only show up in an application.

The checklist here and the one in check.mjs are deliberately the same, in the same order.
Two bindings over one ABI should be able to answer the same questions, and a line that
appears in one output and not the other is a gap worth seeing.
"""

import base64
import os
import sys
import time

import rda
from state import State, Commands, Tables, ROUTES, define

failures = []


def shown(value):
    """A value as one short, printable line.

    ascii() rather than repr() because a Windows console is not UTF-8 and would render a
    correct round trip as question marks -- which reads like the failure this check
    exists to catch. And truncated, because five thousand x's in a test log is five
    thousand x's nobody reads.
    """
    text = ascii(value)
    return text if len(text) <= 60 else "{}... ({} chars)".format(text[:57], len(value))


def check(what, got, want):
    ok = got == want
    print("  ok   " if ok else "  FAIL ", what, "->", shown(got),
          "" if ok else "(wanted {})".format(shown(want)))
    sys.stdout.flush()
    if not ok:
        failures.append(what)


def refused(what, work):
    """A call that must fail, and must say so rather than quietly doing nothing."""
    try:
        work()
        check(what, "accepted", "refused")
    except (rda.RdaError, AttributeError) as problem:
        print("  ok   ", what, "->", type(problem).__name__)
        sys.stdout.flush()


# ---- lifecycle -----------------------------------------------------------------------
print("lifecycle")
config = rda.StartupConfig()
config.name = "python binding checks"
# How the window is dressed. Every one of these is a separate entry point, so a binding
# that forgot to declare one fails here rather than in somebody's application. Chosen to
# be invisible: a window that is already this size, slightly transparent, put somewhere
# ordinary. Nothing that takes the screen over while the tests run.
# The engine's own default size, stated rather than changed: the fixture's layout is
# written against it, and a smaller window would change what a placed viewport measures.
config.width, config.height = 1280, 800
config.min_width, config.min_height = 320, 200
config.max_width, config.max_height = 1600, 1200
config.resizable = False
config.opacity = 0.95
config.x, config.y = 120, 120

rda.init(config)
check("init returns with the engine up", rda.running(), True)

# What the engine publishes about its own window. Defined during bring-up, so they are
# there before any interface is.
from rda import _engine
check("the window reports its width", _engine.get_number("rda.width") >= 1.0, True)
check("the window reports its height", _engine.get_number("rda.height") >= 1.0, True)
check("the window says it is open", _engine.get_bool("rda.open"), True)
check("a fresh window is not maximised", _engine.get_bool("rda.maximized"), False)
# And writing one is how a frameless title bar's buttons work. Set and put back, so the
# rest of the checks run against the window they started with.
_engine.set_bool("rda.maximized", True)
time.sleep(0.4)
check("writing rda.maximized maximises", _engine.get_bool("rda.maximized"), True)
_engine.set_bool("rda.maximized", False)
time.sleep(0.4)
check("and writing it back restores", _engine.get_bool("rda.maximized"), False)

define()
rda.load_interface("res/layouts/first.rdab")
check("an interface loads", True, True)
refused("a blueprint that is not there is refused",
        lambda: rda.load_interface("res/layouts/nope.rdab"))

# ---- signals -------------------------------------------------------------------------
print("signals")
State.count = 41
check("numbers", State.count, 41.0)
State.count += 1
check("read-modify-write", State.count, 42.0)
State.flag = True
check("booleans", State.flag, True)
State.text = "written"
check("text", State.text, "written")
State.text = "éàü — non-ascii"
check("text is utf-8 on the way out and back", State.text, "éàü — non-ascii")

# Longer than the buffer a binding guesses at, so the length-then-retry path runs.
long_text = "x" * 5000
State.note = long_text
check("text longer than the guess comes back whole", State.note, long_text)
check("and is the length it should be", len(State.note), 5000)

refused("a misspelled signal is refused", lambda: setattr(State, "notAThing", 1))

# ---- tables --------------------------------------------------------------------------
print("tables")
Tables.items.fill([
    {"label": "one", "value": 1.5, "on": True},
    {"label": "two", "value": 2.5, "on": False},
])
check("fill sets the count", Tables.items.rows, 2)
check("text came back", Tables.items.label(0), "one")
check("numbers came back", Tables.items.value(1), 2.5)
check("booleans came back", Tables.items.on(0), True)
check("and a row that was not set is false", Tables.items.on(1), False)

# The other row shapes this binding promises to take.
Tables.items.fill([("tuple", 3.0, True)])
check("a sequence in column order works", Tables.items.label(0), "tuple")


class Row:
    label, value, on = "object", 4.0, False


Tables.items.fill([Row()])
check("an object with those attributes works", Tables.items.label(0), "object")

Tables.items.rows = 5
check("resizing grows it", Tables.items.rows, 5)
check("and the new rows are empty", Tables.items.label(4), "")
Tables.items.set_label(["a", "b"], first=3)
check("a run can start anywhere", Tables.items.label(3), "a")
check("and leaves the row before it alone", Tables.items.label(2), "")

refused("a row past the end is refused", lambda: Tables.items.label(999))

Tables.items.fill([{"label": "r%d" % i, "value": i, "on": i % 2 == 0}
                   for i in range(10000)])
check("ten thousand rows land", Tables.items.rows, 10000)
check("the last of them is right", Tables.items.label(9999), "r9999")

# ---- screens -------------------------------------------------------------------------
print("screens")
rda.set_transition_ms(0)          # instant, so a check cannot race a cross-fade
rda.open_routes(ROUTES, "res/layouts")
time.sleep(0.5)
check("the first declared route is where it opens", State.route, "first")
check("with nothing to go back to", rda.can_go_back(), False)

State.route = "second"
time.sleep(0.5)
check("writing the signal navigates", State.route, "second")
check("and that is history", rda.can_go_back(), True)
check("back returns", rda.back(), True)
time.sleep(0.5)
check("to where it was", State.route, "first")
check("and forward is open", rda.can_go_forward(), True)
check("forward returns", rda.forward(), True)
time.sleep(0.5)
check("to where it went", State.route, "second")

# ---- commands ------------------------------------------------------------------------
print("commands")
asked = []


@Commands.bump
def on_bump():
    asked.append(State.count)


# The engine calls a bound Python handler on its own loop thread, so by the time invoke()
# returns the handler has already run. Node cannot do this, and its check.mjs polls --
# which is the one place the two lists differ in *how* rather than in *what*.
State.count = 7
rda.invoke("bump")
check("invoke runs the handler", asked, [7.0])

refused("invoking a command nothing is bound to is refused", lambda: rda.invoke("unused"))
refused("invoking a command that does not exist is refused", lambda: rda.invoke("nope"))

# ---- the window, the clipboard, and measuring (ABI 1.2) --------------------------------
print("window and clipboard")
rda.set_title("binding test \u2014 renamed")
check("the title can be set while open", rda.running(), True)

rda.set_clipboard("round trip \u2014 dash")
check("the clipboard round-trips", rda.clipboard(), "round trip \u2014 dash")

one = rda.measure_text("M")
ten = rda.measure_text("MMMMMMMMMM")
check("ten monospace characters are ten times one", round(ten[0] / one[0]), 10)
check("and a line has a height", one[1] > 0, True)
refused("measuring with no text is refused", lambda: rda.measure_text(None))

# ---- pictures from memory --------------------------------------------------------------
# A 2x2 PNG written out here, so the check needs no image library to make one.
print("images")
PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFUlEQVR4nGM8YeP2n4GBgYEJRIAwACNKAk3nXZn3AAAAAElFTkSuQmCC")

rda.define_image("fromBytes", PNG)
check("a picture registered from encoded bytes", rda.running(), True)
rda.define_image_pixels("fromPixels", b"\xC8\x3C\x46\xFF" * 4, 2, 2)
check("and one from raw pixels", rda.running(), True)
rda.define_image("fromBytes", PNG)
check("registering the same name again is a replace, not an error", rda.running(), True)
rda.forget_image("fromBytes")
rda.forget_image("never registered")
check("forgetting one, or one that was never there, is fine", rda.running(), True)

refused("bytes that are not a picture are refused",
        lambda: rda.define_image("bad", b"not a picture"))
refused("no bytes at all is refused", lambda: rda.define_image("bad", b""))
refused("a picture with no name is refused", lambda: rda.define_image("", PNG))
refused("too few pixels for the size is refused",
        lambda: rda.define_image_pixels("bad", b"\0" * 4, 2, 2))

# ---- streams and effects ----------------------------------------------------------------
# A 2x2 frame, and a filter over it. Nothing shows either -- what is under test is that the
# calls cross, that a surface is reused rather than rebuilt, and that a shader compiles.
print("streams and effects")
FRAME = b"\x40\x80\xC0\xFF" * 4

rda.push_frame_pixels("feed", FRAME, 2, 2)
check("a frame pushed from raw pixels", rda.running(), True)
rda.push_frame("feed", PNG)
check("and one from an encoded frame", rda.running(), True)
# Not `shown`: this file already has a shown() for formatting values.
sent, drawn = rda.stream_counts("feed")
check("both were counted", sent, 2)
check("a stream nothing has drawn yet is still wanted", rda.stream_wanted("feed"), True)

refused("a frame with no bytes is refused", lambda: rda.push_frame("feed", b""))
refused("too few pixels for the size is refused",
        lambda: rda.push_frame_pixels("feed", b"\0" * 4, 2, 2))

rda.define_effect("grey", "void main() { vec4 c = texture(src, uv()); store(vec4(vec3(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722))), c.a)); }")
check("a filter compiled", rda.running(), True)
rda.apply_effect("grey", "feed", "feed.grey")
check("and ran over the feed", rda.stream_counts("feed.grey")[0], 1)
rda.apply_effect("grey", "feed", "feed.grey", [0.5, 1.0])
check("with parameters", rda.stream_counts("feed.grey")[0], 2)

refused("a shader that will not compile is refused",
        lambda: rda.define_effect("bad", "void main() { not glsl }"))
refused("an effect nothing defined is refused",
        lambda: rda.apply_effect("nope", "feed", "feed.out"))
refused("reading and writing one stream is refused",
        lambda: rda.apply_effect("grey", "feed", "feed"))
refused("filtering a stream with no frame is refused",
        lambda: rda.apply_effect("grey", "empty", "empty.out"))

# Several pictures into one filter, and the answer read back rather than looked at.
rda.push_frame_pixels("a", b"\xFF\x00\x00\xFF" * 4, 2, 2)
rda.push_frame_pixels("b", b"\x00\x00\xFF\xFF" * 4, 2, 2)
rda.define_effect("blend", "void main() { store(mix(tap(0, uv()), tap(1, uv()), param(0))); }")
rda.apply_effect("blend", ["a", "b"], "mixed", [0.0])
front, width, height = rda.read_frame("mixed")
check("a two-input blend read back at the right size", (width, height), (2, 2))
check("and at 0 it is the first picture", tuple(front[0:4]), (255, 0, 0, 255))
rda.apply_effect("blend", ["a", "b"], "mixed", [1.0])
back, _, _ = rda.read_frame("mixed")
check("at 1 it is the second", tuple(back[0:4]), (0, 0, 255, 255))

refused("more than four sources is refused",
        lambda: rda.apply_effect("blend", ["a", "b", "a", "b", "a"], "out"))
refused("no sources at all is refused", lambda: rda.apply_effect("blend", [], "out"))
refused("writing into one of the sources is refused",
        lambda: rda.apply_effect("blend", ["a", "b"], "b"))
refused("reading a stream that is not there is refused", lambda: rda.read_frame("nope"))

rda.forget_effect("grey")
rda.close_stream("feed")
check("forgetting both is fine", rda.running(), True)

# ---- the theme ------------------------------------------------------------------------
print("theme")
refused("changing to a theme that is not there is refused",
        lambda: rda.set_theme("layouts/nope.rdth"))
refused("changing to a theme with no name is refused", lambda: rda.set_theme(""))

# ---- the viewport --------------------------------------------------------------------
# The second screen is showing, and it has <viewport name="canvas"> on it.
print("viewport")
time.sleep(0.3)
width, height = rda.viewport_size("canvas")
check("a placed viewport reports its size", (width, height), (400.0, 120.0))
check("one nothing has drawn is zero", rda.viewport_size("nowhere"), (0.0, 0.0))

drawing = rda.Drawing()
drawing.clear("#11141A")
drawing.rect(8, 8, 80, 40, "#3A6AD0", radius=4)
drawing.line(0, 0, width, height, "#4C7CE6", 2)
drawing.circle(200, 60, 24, "#6ED09C")
drawing.text(12, 60, "drawn from Python", "#DCE0E7", 12)
check("five shapes built", len(drawing), 5)
rda.draw("canvas", drawing)
time.sleep(0.3)
check("the drawing was accepted", rda.running(), True)

rda.draw("canvas", rda.Drawing())   # an empty one takes it back down
refused("a colour that is not one is refused", lambda: rda.Drawing().clear("mauve"))
refused("drawing in a viewport without a name is refused", lambda: rda.draw("", drawing))

# ---- shutdown ------------------------------------------------------------------------
print("shutdown")
rda.stop()
rda.wait()
check("the engine stops", rda.running(), False)

print()
print("FAILED: " + ", ".join(failures) if failures else "python binding: all checks passed")
sys.exit(1 if failures else 0)
