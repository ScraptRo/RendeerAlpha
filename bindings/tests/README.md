# The binding tests

Three tests, one checklist. `rda_python_binding`, `rda_node_binding` and
`rda_csharp_binding` each start a real engine and walk the same list of questions in the
same order.

```bash
ctest --test-dir build/default -C Debug -R binding --output-on-failure
```

Or one of them directly, which is what you want while fixing something — the output is
the checklist and it scrolls past in a few seconds:

```bash
cd build/default/bin/Debug/bindingtests
python check.py
node check.mjs
./rda_csharp_check.exe
```

## Why this is its own tier

The C ABI is a product with three consumers, and nothing else in the build compiles any
of them. `rendeer_tests` cannot: it deliberately links only device-independent sources,
and a binding is the opposite of device-independent — it opens a shared library, starts a
renderer, and puts a window on screen.

So this tier costs what the others avoid: a device, a window, and another language
runtime. That is the whole reason it did not exist for a while, and the whole reason it
skips itself rather than failing when any of those is missing.

## What is here

| | |
| --- | --- |
| `state.ts` | the declaration — one signal of each type, one table, two screens, a command bound and a command deliberately not |
| `layouts/*.tsx` | two screens, so routing has somewhere to go and history something to restore |
| `check.py` | the Python checklist |
| `check.mjs` | the same checklist in Node |
| `check.cs` + `Check.csproj` | the same checklist in C#, which unlike the other two has to be compiled |
| `CMakeLists.txt` | generates all three state modules, compiles the layouts, assembles a run directory, registers the tests |

Nothing here is committed as a build artefact. The blueprints and all three state modules
are generated into the build tree, because this is a fixture rather than something an
application ships — there is nothing to keep in step by hand.

## The rule: the three lists are one list

**A check added to one must be added to all of them, with the same wording, in the same
place.** That is what makes the outputs comparable: a line that appears in two and not
the third is a gap in a binding, not a difference of opinion between test authors.

They differ in two places today, and both are the point:

```
python:  invoke runs the handler              -> [7.0]
c#:      invoke runs the handler              -> 7
node:    invoke queues rather than calling    -> []
         and the poll runs the handler        -> [7]
```

A JavaScript value may only be touched on the thread that owns it, so the engine cannot
call a Node handler — it records that the command fired and `poll()` runs it.
Python and the CLR both take the call directly. That is the one asymmetry in the whole
seam, and it is an assertion here rather than a paragraph somewhere.

The other: where Python and Node check at run time that a misspelled signal is refused,
C# prints `at compile time` and checks nothing — because `State.notAThing` is
not a member and the line would not build. Same guarantee, collected earlier, and the
line stays in the list so the three outputs still line up.

## What it covers

Signals of each type, including **UTF-8** and **a string longer than the buffer a binding
guesses at** — that second one is the reason the list exists. All three read text by
guessing a size and retrying when the guess was too small, and none of them had ever been
made to guess wrong.

Then: every row shape `fill()` promises to take, a run written at an offset, ten thousand
rows, routing with history in both directions, and the calls that must be **refused**
rather than quietly ignored — a blueprint that is not there, a misspelled signal, a row
past the end, a command nothing is bound to, a command that does not exist.

## Adding a check

1. Write it in `check.py`, `check.mjs` and `check.cs`: same wording, same position.
2. If it needs a new name, add it to `state.ts` — the generators pick it up.
3. **Make it fail on purpose.** See below.

## Making sure a check works

A test that has never failed has not been tested. Break the thing it is checking, confirm
that check and only that check goes red, then put it back.

The one used on this suite: in `RendeerC.cpp`, make `rda_table_set_texts` ignore its
`first` argument —

```cpp
t.setText(static_cast<size_t>(i), ...);   // was: first + i
```

— rebuild, and all three fail on *a run can start anywhere* and nothing else. If a
mutation like that passes, the check is decoration.

## What makes each one skip

Skipping is deliberate: a machine without Node should still be able to run everything
else, and a clone that has not been `npm install`ed should not fail with a module error.

| | |
| --- | --- |
| `RDA_BUILD_C_API=OFF` | no shared library to open — all three skip |
| `RDA_BUILD_TOOLS=OFF` | no generators — all three skip |
| no Python interpreter | `rda_python_binding` skips |
| no `node` on PATH | `rda_node_binding` skips |
| no `node_modules/koffi` | `rda_node_binding` skips — `npm install` in the engine root enables it |
| no `dotnet` on PATH | `rda_csharp_binding` skips, and is not built |

Each says which, at configure time, in one line.

## `rda_command_invoke`

A command normally arrives from a click, and a test has no fingers. `rda_command_invoke`
asks for one directly — which is not a test hook: `Commands::invoke` has always been what
a C++ application calls for a menu item or a hotkey, and the C ABI was simply missing it.
Exposing it closed a gap and made the command path testable in the same stroke.

## More

[the architecture notes on testing](../../docs/architecture/testing.md#what-is-tested) — the four tiers and
what each is for.
