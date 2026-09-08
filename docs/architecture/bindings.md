# The bindings, and the seam out of C++

## Out-of-process hosting, and why it is gone

`src/Runtime/` held a wire protocol, a host that served it and a client that spoke it:
one process owning the device, applications connecting to it over a pipe and sending draw
lists. It was once how every application ran. 1,679 lines, plus 33 tests, removed.

It was never finished -- no target ever built the `RuntimeHost.exe` that
`connectOrStartRuntime` launches by name, so there was a host *class* and no host
*process*. But being unfinished is not why it went. It went because it answers the wrong
question.

The reason it existed was **language independence**: an application in any language could
have a Rendeer interface without linking a C++ Vulkan engine. That is a real goal and
still is. What the pipe carries, though, is a *draw list* -- so a client in another
language is a client that does its own drawing, calling `rect` and `text` by hand. That
was the right shape when it was written, before the interface layer existed.

It is the wrong shape now. An application's interface is a compiled blueprint: the layout
is written in TSX, checked against a generated schema, styled by a theme, bound to signals
and animated by the engine. A backend does not draw the interface. It writes state and
answers commands -- which is the whole of what `state.count = 3` means. Reaching that
through a draw-list protocol would give another language the pixels while denying it the
entire interface layer: no layouts, no router, no theme, no bindings, none of the
animation.

A backend does draw in one place, and the exception proves the rule rather than weakening
it. Inside a [`<viewport>`](../backend/viewport.md) there is no interface to deny it: a
plot is not made of widgets and no theme has an opinion about it, so the application draws
and the engine does not pretend otherwise. Everywhere else, the draw list is exactly the
wrong seam.

So the seam for another language is not the draw list. It is the same seam C++ uses --
signals, commands, and a blueprint to load -- reached through a C ABI, in one process.
That is a much smaller surface than a wire protocol, and it gives another language the
whole engine rather than a canvas.

## The C ABI

`RendeerC.h`, built as a shared library because that is what an FFI can open. Fifty
functions: configure, start, load an interface or a set of screens, define what it reads,
get and set a signal, fill a table, bind or watch a command. No drawing, which is the
point.

**Every call crosses to the loop thread.** The signal table is a vector of strings and
observer lists; writing one from another thread while the loop reads it is a race with a
reallocating `std::string` in it -- the kind that corrupts rather than the kind that
returns the wrong number. So nothing in the ABI touches engine state on the caller's
thread. Each call hands its work to `LoopWork`, which the engine already had for exactly
this, and waits.

Two things follow. A call costs a round trip to the next frame -- the right price for a
backend, the wrong one for a render path, which is why there is no drawing here. And an
idle window is woken first: with on-demand redraw the loop can be parked in the event
pump with no frame due for seconds, and a request that waited for one would look like a
hang. `glfwPostEmptyEvent` is safe from any thread and is what does it.

A call made from inside a handler runs inline, because handlers already run on the loop
thread. Loading the interface from `on_start` therefore costs nothing extra.

**`rda_init` waits for the engine to be up**, meaning the window, the renderer and
`on_start` have all finished. It did not at first -- `rendeerRun` in Owned mode spawns the
thread and returns at once -- and the C example in the documentation is what found it: a
backend that set a signal on the line after `init` was racing the device, and lost, because
the signal it was writing did not exist until `on_start` declared it. The alternative to
waiting is every backend hand-rolling the same handshake, and getting it wrong once each.
The timeout on that wait is not a guess about how long start-up takes; it is the only way
out if bring-up dies without reaching either callback.

**Rows are written in runs, not cells.** `rda_table_set_numbers(table, column, first,
values, count)` fills a whole column in one hop. That is not an optimisation detail -- it
is what makes tables usable across this seam at all. A cell-at-a-time API would make ten
thousand rows of three columns thirty thousand round trips; a column at a time makes it
three.

Measured at ten thousand rows: **35 ms from Python, 61 ms from Node.** The gap is not the
ABI — both make the same handful of calls — it is what each runtime costs to walk ten
thousand values and hand them over. Which is the right shape for the number to have: it
scales with the data the backend already has, not with how many times it crosses.

## Python

`bindings/python/`, and `rda python state.ts state.py` generates the rest.

    config = rda.StartupConfig()
    config.theme = "res/themes/app.rdth"

    @Commands.add_one
    def add_one():
        State.count += 1

    @config.on_start
    def opened():
        define()
        rda.load_interface("res/layouts/home.rdab")

    rda.init(config)

ctypes, so there is no extension module to build: the engine is a shared library and the
package is a description of it.

The state module is **generated rather than dynamic**. Every signal is a real property
with the type and documentation the declaration gave it, so `State.ARandomValu = 1` is an
`AttributeError` where it is written -- the same guarantee the generated C++ and
TypeScript give their languages, rather than a lookup that fails somewhere else later.

Two things the shape of Python forced:

- **A handler is bound before the engine exists.** `@Commands.add_one` at module level
  runs at import, long before `init()` and before the declaration has created the command
  to bind to. Refusing it would make every application move its handlers inside a
  callback, so instead they are remembered and applied once `on_start` has declared them.
- **`init()` returns.** The engine runs on its own thread, so the interpreter stays the
  application's: a REPL, a web server or a data pipeline carries on while a window is
  open. `wait()` is there for when there is nothing left to do but let it live.

A **table** is a class per declaration rather than a dictionary of them, for the same
reason the state is properties: a misspelled column is an error where it is written. Its
`fill()` takes a mapping, an object, or a sequence in column order -- whichever the
application already has, whether that is a database row, a dataclass or a CSV line -- and
transposes it into the column-at-a-time writes the ABI wants. Deciding that in the binding
rather than making every application convert first is the whole reason it is there.

**Screens** work the same as they do in C++. `rda.open_routes(ROUTES, "res/layouts")` with
the generated route table, and after that navigating is `State.route = "catalogue"` -- the
same sentence the interface itself writes, because "which screen is showing" is state.

Nothing is built in a Python project. The engine's own build stages the shared library,
the tool and the engine's assets into the checkout's `bin/`; the package is registered
with an interpreter by one `.pth` line, and looks for the library beside itself, then in
that `bin/`. `python -m rda build .` runs the tool over the project's `res/` and writes
`state.py` beside `app.py`. So `python app.py` works with nothing compiled and nothing
copied -- which was the point of ctypes over an extension module in the first place.

## Node, and the one thing that could not be done the same way

`bindings/node/`, koffi over the same C ABI, and a generator beside `emitStatePython`.
Most of it is the Python binding with different punctuation. One part is not.

**An FFI callback into Node deadlocks.** A JavaScript value may only be touched on the
thread that owns it, so handing `rda_command_bind` a trampoline cannot work -- and the way
it fails is worth knowing, because it is not the way anyone expects. It does not crash and
it does not throw: the engine's loop thread blocks trying to reach the JS thread, the JS
thread is blocked inside the FFI call waiting for the engine, and the process stops with
no error and no stack. Measured, with a five-line probe, before any of the binding was
written.

So the ABI grew a second way to answer a command. `rda_command_watch` binds an internal
handler that does the least it possibly can -- take a lock, push a name, return -- and
`rda_poll_command` collects it. That is the one call carrying data back out of the engine
that **does not cross to the loop thread**: it reads a queue of its own behind a mutex, so
a backend may poll it as often as its loop turns without paying a frame each time. Which
it has to -- a poll that cost a frame would be a backend spending its whole budget asking
whether anything happened.

What that costs is a millisecond of latency. What it buys is that a handler is no longer
inside a frame, so it may `await` -- which a C++ command handler categorically may not.
The demo application uses that: a button in the interface runs an `async` function that
makes an HTTP request to a server the same process is running, and the answer comes back
as a row in a list.

The queue is bounded at 256. A backend that stops polling loses the oldest waiting command
and the log says so once, rather than turning a held-down button into unbounded memory.

The second difference is smaller and follows from `rda_init` waiting: there is no
`on_start` in the Node binding at all. `init()` returns with the engine up, so set-up is
flat top-level code rather than a callback, and `run()` -- a poll on a timer that resolves
when the window closes -- is what keeps the process alive.

**Four languages, one seam.** The C ABI is now carrying C++, Python, Node and C#, and the
shape of it has been tested by all four rather than designed for one. The two functions
Node forced are the only ones any of them needed that the others did not, and they stay
useful to anything whose runtime is hostile to foreign-thread calls -- the JVM among
them. C# is the case that shows they were not a general tax: the CLR takes a
foreign-thread call happily, so the C# binding uses the plain callback path and never
touches the queue.

## C#, and the case that shows the queue was not a tax

`bindings/csharp/`, P/Invoke, and a `rda csharp` generator. Two source files rather than
a package, because that is what the binding is.

Worth writing down for one reason: **the CLR takes a call from a foreign thread**, which
was measured before the binding was written -- so C# uses the plain `rda_command_bind`
callback path and never touches the queue Node needed. That is the evidence that the two
functions Node forced were an accommodation for one runtime rather than a cost imposed on
the seam: three of the four bindings do not use them.

Two things P/Invoke gets wrong by default, both found by compiling the sketch that used
to be in this documentation rather than by reasoning about it. A bare `string` parameter
marshals as ANSI while this ABI reads UTF-8, so anything past plain ASCII is destroyed on
the way in -- silently, and only for the inputs an English test never uses. And a
delegate handed to native code is collected unless something holds it, which is the trap
every language with callbacks has.

What C# adds that the others cannot: the generated state is *checked by the consuming
compiler*. A row is a struct, so a misspelled column or a wrong type is a build error --
the same guarantee the TypeScript side gets, collected earlier than Python or Node can
collect it.

What is given up is process isolation: a backend that crashes now takes the window with
it, as it does for a C++ application today. That was the one case the protocol had left,
and it was never the case anyone asked for.

---

Back to [the architecture index](README.md) · [all documentation](../README.md)
