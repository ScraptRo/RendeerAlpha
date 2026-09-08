# Testing

## Testing an interface without opening one

```cpp
State::define();                       // the signals the layout is compiled against

Probe probe;
probe.open("res/layouts/hello.rdab");
probe.click("root/top_bar/buttons/add-one");

CHECK(State::count() == 1);
CHECK(probe.text("root/top_bar/title") == "Clicked 1 time");
```

Everything this needs was already independent of the renderer, and nobody had put it
together: a blueprint is a POD array, instantiating it calls constructors, a handler is a
compiled program over signals, and signals are plain C++ objects. None of that wants a
device. What was missing was a way to **name a node and fire its handler**.

The ids are the paths the compiler assigned — the same ones `rda dump` prints. So a test
names what the layout named, and renaming a widget breaks the test, which is the point: a
test that kept passing through a rename was not testing that widget.

### What it does

| | |
| --- | --- |
| `click(id)` | a button |
| `set(id, bool)` / `set(id, double)` | a checkbox, a slider |
| `type(id, text)` | a text field |
| `choose(id, value)` | a select, by an option's value |
| `showTab(id, index)` | a tabs |
| `text(id)`, `visible(id)`, `shown(id)` | reading it back |
| `ids()`, `exists(id)`, `nodeCount()` | what is in it |

Each action runs the handler the layout bound and then applies the bindings that handler's
writes reached — because in a running program the next frame always would, and a test that
had to remember that would be a test about frames.

Each returns **false** when there is no such widget, or it is not the kind that does that.
Clicking a label is a bug in the test, and finding out there beats finding out four
assertions later.

`shown(id)` walks the ancestors as well: a widget inside a hidden container is not on
screen however visible it says it is, and `visible={() => state.expanded}` on a parent is
the usual way a layout hides a group.

`nodeCount()` is the *blueprint's* size, not the tree's — a list's row template is one node
and forty pooled copies. It is how a test says "this screen did not grow a widget per row".

### What it is not

It does not lay anything out, measure text, or draw. Those need a font atlas and a device,
and a test that wanted them would be a screenshot test rather than this. Everything above
the pixels is here; the rules that decide sizes are unit-tested separately, and that the
whole thing opens a window and draws without complaining is what the smoke run checks.

Nothing in it is a mock. An application's layout tests run against the same `.rdab`
files it ships, with the real compiled bindings and the signals its `state.ts` declared —
the counter, commands, write-back, visibility, local signals, expanded components,
navigation, tabs, a dropdown. If those pass and the application is broken, the bug is in
the drawing.

---

## What is tested

Four, because no one tool answers all of it.

**The engine's own `ctest` — 105 cases, no device needed.** Signals and their
dirty-marking, the expression grammar and everything it refuses, tables (columns,
resizing, and that an unchanged write does not move the version), commands (bound,
unbound, and refused from a value binding), row templates (columns read, mixed with
state, and refused outside a template), the handler parameter, the rule that decides what
a declared size means, the animation layer's arithmetic, the syntax languages the engine
ships, and the theme schema — including that a suggestion is offered for a typo and
withheld for a word that is not one.

The rule for what belongs here is *device-independence*, not membership of any
configuration: a source that needs no GPU is testable, so it is tested.

**An application's own `ctest` — what its interface does.** `Probe` opens the shipped
blueprints without a window and drives them — [Testing what you build](../setup/testing.md)
has the pattern. This is the layer that was missing: a screenshot cannot
tell you that clicking a button moved the right signal, and a person should not have to.

**An application's smoke run — does it actually run?** A layout that compiles is
not a layout that works. A smoke script builds everything (which compiles every `.tsx`,
the theme and the state), runs the unit tests, then runs each screen for a fixed number of
frames and fails if the exit code is not zero **or the log holds anything worse than
INFO**. The second half is the one that catches things: a run can exit cleanly and still
have warned that a binding found no signal or a command nothing had declared.

A `--frames N` argument is what makes that automatic — the application stops itself after N
frames and exits the way closing the window does, rather than being killed on a timer.

**The bindings — `bindings/tests/`, run by `ctest`.** The C ABI is a product with three
consumers and nothing else in the build compiles any of them. `rda_python_binding`,
`rda_node_binding` and `rda_csharp_binding` each start a real engine against a small declaration -- one signal of
each type, one table, two screens, a command bound and a command deliberately not -- and
walk the same checklist: signals round-tripping including UTF-8 and a string longer than
the buffer a binding guesses at, every row shape `fill()` promises to take, a run written
at an offset, ten thousand rows, routing with history, and the calls that must be
*refused* rather than quietly ignored.

**The three lists are the same list, in the same order**, so a line that appears in two
outputs and not the third is a gap rather than a difference of opinion. They differ in
two places, and both are the point: Python and C# assert that `invoke()` has already run
the handler while Node asserts it has only queued it until `poll()`; and where the other
two check at run time that a misspelled signal is refused, C# cannot -- that line would
not compile, which is the same guarantee collected earlier.

This tier needs a window, a device and another language runtime -- three things the
others are each arranged to avoid -- so it skips itself rather than failing when Python,
node, koffi or dotnet is not there. That is what took it from "no automated test" to a tier: it
was not hard, it was just the one nobody had made cheap enough to run by default.

`rda_command_invoke` is what makes it possible without a person. The interface is not the
only thing that can ask for work -- `Commands::invoke` is what a C++ application already
calls for a menu or a hotkey -- and exposing it closed a gap in the ABI *and* gave the
command path something to test against that is not a synthetic click.

The tests were checked by breaking the engine on purpose: making `rda_table_set_texts`
ignore its `first` argument fails all three, on the line that says *a run can start
anywhere*, and nothing else. A test suite that has never failed has not been tested.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
