"""What the Python binding does, checked against a running engine.

Run by ctest as `rda_python_binding`. It opens a real window on a real device, because
that is what the thing under test does -- a binding that cannot be exercised without one
is a binding whose bugs only show up in an application.

The checklist here and the one in check.mjs are deliberately the same, in the same order.
Two bindings over one ABI should be able to answer the same questions, and a line that
appears in one output and not the other is a gap worth seeing.
"""

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

rda.init(config)
check("init returns with the engine up", rda.running(), True)

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
