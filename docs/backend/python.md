# Python

`bindings/python/`, ctypes over the C ABI. There is no extension module to build: the
engine is a shared library and the package is a description of it, so `pip` has nothing to
compile and nothing to go wrong.

```python
import rda
from state import State, Commands, Tables, ROUTES, define

config = rda.StartupConfig()
config.name = "My application"
config.theme = "res/themes/app.rdth"

@Commands.save
def on_save():
    print(State.title)

@config.on_start
def opened():
    define()
    rda.open_routes(ROUTES, "res/layouts")

rda.init(config)
rda.wait()
```

## State

Every signal is a **real property** on a generated class, carrying the type and the
documentation the declaration gave it:

```python
State.count = 7
State.count += 1
print(State.title)

State.notAThing = 1      # AttributeError, where it is written
```

Generated rather than dynamic on purpose. A `__getattr__` that forwarded any name would
turn a typo into a value that quietly stays zero, which is the failure the whole
declaration exists to prevent.

## Commands

```python
@Commands.save
def on_save():
    ...
```

A decorator at module level runs at **import** — long before `init()`, and before the
declaration has created the command to bind to. Refusing that would make every application
bury its handlers inside a callback, so instead they are remembered and applied once
`on_start` has declared them. Binding after the engine is up works too, and happens at
once.

## Tables

```python
Tables.products.fill([
    {"title": "Widget", "price": 9.0, "inStock": True},
    {"title": "Gadget", "price": 12.5, "inStock": False},
])

Tables.products.rows            # 2
Tables.products.title(0)        # "Widget"
Tables.products.set_price([19.0], first=1)
Tables.products.rows = 100      # grow; new rows are zero, false and empty
```

A row may be a **mapping**, an **object with those attributes**, or a **sequence in column
order** — whichever the application already has, whether that is a database row, a
dataclass or a CSV line.

`fill()` writes a column at a time, and that is not an optimisation detail. Every call
crosses to the engine's loop thread and waits, so ten thousand rows of three columns is
three round trips this way and thirty thousand the other. Measured: **10,000 rows in about
35 ms.**

## Screens

```python
rda.set_transition_ms(180)
rda.open_routes(ROUTES, "res/layouts")

State.route = "catalogue"      # navigate
rda.back()                     # or drive history from outside the interface
rda.can_go_forward()
```

`ROUTES` is generated from the declaration, in declaration order, so the first entry is
where the application opens unless `route` already says otherwise.

## Threading

`init()` **returns**. The engine runs on its own thread, so the interpreter stays the
application's: a REPL, a web server or a data pipeline carries on while a window is open.
`wait()` is there for when there is nothing left to do but let it live.

Every call in the package is safe from any thread — each hands its work to the loop thread
and waits, which costs a frame. Handlers run *on* the loop thread, so calls made from
inside one are free.

## Finding the engine

`_engine.py` looks in this order: `RDA_ENGINE` (a file or a directory), beside the
package, the checkout's `bin/` (the package is `bindings/python/rda`, three levels below
it), beside the working directory, then whatever the loader can find. The bring-up
stages the library into `bin/`, so none of that needs setting. `python -m rda where`
prints the answer.

---

Back to [the backend index](README.md) · [all documentation](../README.md)
