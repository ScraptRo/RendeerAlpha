# Node

`bindings/node/`, koffi over the C ABI. koffi ships a prebuilt binary for each platform,
so installing it downloads rather than compiles — no node-gyp and no compiler. It is the
one thing a Node backend needs that a Python one does not, because Node has no FFI of its
own.

```js
import * as rda from 'rda'
import { State, Commands, Tables, ROUTES, define } from './state.mjs'

rda.init({ name: 'My application', theme: 'res/themes/app.rdth' })
define()
rda.openRoutes(ROUTES, 'res/layouts')

Commands.save(() => console.log(State.title))

await rda.run()
```

Two things are different from Python, and both come from the same fact: **Node already
owns an event loop.** Nothing else about the seam changes.

## `init()` returns, and there is no `on_start`

Python nests its set-up in a callback because that is where a C++ application does it.
Node does not need to: `rda_init` waits until the window and the renderer are up, so the
line after `init()` may declare state and load an interface. Flat top-level code, and the
engine keeps running on its own thread while Node carries on.

## A command handler is not a callback

This is the design decision worth reading, because it is the only place the shape of the
binding is dictated rather than chosen.

A JavaScript value may only be touched on the thread that owns it. The obvious spelling —
hand `rda_command_bind` an FFI trampoline — therefore cannot work, and the way it fails is
worth knowing: **it does not crash, it deadlocks.** The engine's loop thread blocks trying
to reach the JS thread, and the JS thread is itself blocked inside the FFI call that is
waiting for the engine. The process simply stops, with no error and no stack.

So the ABI grew a second way to answer a command:

```c
RDA_C_API int rda_command_watch(const char* name);
RDA_C_API int rda_poll_command(char* buffer, int capacity);
```

`rda_command_watch` binds an internal handler that does the least it possibly can — take a
lock, push a name, return — so the interface is never waiting on a backend's thread.
`rda_poll_command` is the exception to the rule at the top of this section: alone among
the calls that carry data back out of the engine, it does **not** cross to the loop
thread. It reads a queue of its own behind a mutex, so a backend may poll it as often as
its loop turns without paying a frame each time. (`rda_running`, `rda_stop` and
`rda_wait` skip the hop as well, but they only read an atomic.)

`run()` is what does that polling, on a timer, and resolves when the window closes:

```js
await rda.run()          // or: setInterval(rda.poll, 8) and own the timing yourself
```

The difference from a bound handler is about a millisecond, and one gain. A handler here
is not inside a frame, so it may do anything Node can do:

```js
Commands.ping(async () => {
	const answer = await fetch(`http://127.0.0.1:${PORT}/health`)
	State.status = `the server said ${answer.status}`
})
```

That is not a workaround made to look like a feature — an `await` inside a C++ command
handler would be holding up the frame the interface asked in. It is the same reason
commands never return a value: a command is a request, not a question.

The queue is bounded. A backend that stops polling loses the oldest waiting command and
the log says so once, rather than turning a held-down button into unbounded memory.

## State

```js
State.count = 7
State.count += 1
State.notAThing = 1      // TypeError, where it is written
```

A real object of accessors rather than a Proxy: an editor completes the names and infers
each one's type from its getter, and the object is made non-extensible — ES modules are
strict, so assigning an undeclared name throws. Reading one is `undefined` at run time and
an error in the editor, which is the same guarantee TypeScript gives the frontend.

## Tables

```js
Tables.products.fill([
	{ title: 'Widget', price: 9.0, inStock: true },
	{ title: 'Gadget', price: 12.5, inStock: false },
])

Tables.products.rows           // 2
Tables.products.title(0)       // 'Widget'
Tables.products.setPrice([19.0], 1)
```

A row may be an object with the declared keys or an array in column order. `fill()` writes
a column at a time for the same reason Python's does.

## One thing to watch

Every call is synchronous and costs a round trip to the engine's next frame. Reading a
signal back in a hot path — a request handler, a tight loop — pays that every time. Keep
the value in a JavaScript variable as well and write it through:

```js
let served = 0
// ...
State.served = ++served       // one call, not two
```

---

Back to [the backend index](README.md) · [all documentation](../README.md)
