# An application that already exists

Nothing in this folder requires a new project. An existing C++ program adds four lines to
its CMakeLists, one include, and two calls in its main loop:

1. `add_subdirectory` the engine and link `rendeer`.
2. `rda_add_state` / `rda_add_types` / `rda_add_layouts` over a `res/` you create.
3. `State::define()` and `LayoutHost::open()` once, at start-up.
4. `host.reloadIfChanged()` once a frame.

The engine owns the window and the loop, which is the one thing to know before starting:
`rendeerRun` either blocks (`ThreadMode::Caller`) or takes a thread of its own
(`ThreadMode::Owned`). A program with a main loop already runs the engine `Owned` and
talks to it through signals — every accessor in the generated `RdaState.h` is safe to call
from any thread, and hands its work to the loop.

For a program in Python, Node or C#, the same four steps apply with that language's
binding, and there is no main loop to reconcile: `init()` returns with the engine on its
own thread, and the interpreter stays yours. [The backend](../../backend/README.md) has
each language's threading contract.

---

Back to [adding to a project](README.md) · [setup](../README.md) · [all documentation](../../README.md)
