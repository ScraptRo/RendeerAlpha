# The backend, in four languages

The backend declares the state, answers the commands, fills the tables, and opens the
interface. It never touches a widget: there is no `getElementById` here, and a frame is
not an event.

Everything typed — `State`, `Commands`, `Tables`, `ROUTES`, `define()` — comes from the
**generated** state module. Never edit it; change `res/state.ts` and build again.

## Python

```python
import rda
from state import State, Commands, Tables, ROUTES, define

config = rda.StartupConfig()
config.name = "Files"
config.theme = "res/themes/app.rdth"

@Commands.refresh                 # at module level: bound before the engine exists
def on_refresh():
    State.status = "read it again"
    Tables.entries.fill([{"name": "a.txt", "kb": 1.2}])

@config.on_start
def opened():
    define()                                      # the names, before any layout
    rda.set_transition_ms(180)
    rda.open_routes(ROUTES, "res/layouts", "res/layouts")
    on_refresh()

rda.init(config)                  # returns once the engine is up
rda.wait()                        # ...and this waits for the window to close
```

Run it: `python -m rda build .` then `python app.py`, from the project directory.

## Node

```js
import * as rda from 'rda'
import { State, Commands, Tables, ROUTES, define } from './state.mjs'

rda.init({ name: 'Requests', theme: 'res/themes/app.rdth' })
define()
rda.setTransitionMs(180)
rda.openRoutes(ROUTES, 'res/layouts', 'res/layouts')

Commands.refresh(async () => {           // may await: it runs on Node's loop
  State.status = 'read it again'
  Tables.log.fill([{ path: '/', code: 200 }])
})

await rda.run()                          // collects commands, resolves when the window closes
```

Run it: `npx rda build .` then `node app.mjs`.

**A Node command handler is not called by the engine.** A JavaScript value may only be
touched on the thread that owns it, so an FFI callback from the engine's thread
deadlocks. The engine records that a command fired and `run()` collects it here, about a
millisecond later. That is why a handler may `await`.

## C#

```csharp
using Rendeer;
using RdaState;

Rda.Init(new StartupConfig { Name = "My application", Theme = "res/themes/app.rdth" });
Schema.Define();
Rda.LoadInterface("res/layouts/home.rdab", "res/layouts/home.tsx");

Commands.save(() => Console.WriteLine($"saving: {State.notes}"));

Rda.Wait();
```

Run it: `dotnet run`, which runs the tool over `res/` first. A handler here **is** a real
callback, on the engine's loop thread, inside the frame the interface asked in — so keep
it quick and hand slow work to a `Task`.

Rows are a generated struct, so a misspelled column does not compile:

```csharp
Tables.products.Fill(new[] {
    new Tables.products.Row { title = "Widget", price = 9.0, inStock = true },
});
```

## C++

```cpp
#include <RendeerAlpha.h>
#include <Layout/LayoutHost.h>
#include <RdaState.h>            // generated from res/state.ts

namespace { RDA::Layout::LayoutHost gHost; }

int main() {
    RDA::AppConfig config;
    config.app.name = "My application";
    config.gui.themePath = "res/themes/app.rdth";

    config.onStart = [] {
        RDA::State::define();
        RDA::State::onSave([] { /* ... */ });
        gHost.open(getMainWindow()->gui().retained(),
                   "res/layouts/home.rdab", "res/layouts/home.tsx");
    };
    // Reloading restructures the tree, so it happens here and not in a widget callback.
    config.onUpdate = [](float) { gHost.reloadIfChanged(); };

    rendeerRun(config);
}
```

`rendeerRun` blocks until the window closes. Accessors are `RDA::State::count()` and
`RDA::State::setCount(...)`; a command is `RDA::State::onSave([]{ ... })`.

Run it: `cmake --build build --config Debug`, then start it from `build/bin/Debug`.

## The rules all four share

**Declare before loading.** `define()` creates the signals, commands and tables. A layout
opened first resolves names that do not exist yet, guesses a number, and says so in the
log.

**`init()` returns** (C++: `rendeerRun` blocks, or runs `Owned`). Python, Node and C# get
the engine on a thread of its own and keep their own interpreter — a REPL, a web server
or a watcher carries on while the window is open.

**Every call is safe from any thread**, and costs a round trip to the engine's next
frame. Reads are the ones worth noticing: keep a number you write often in an ordinary
variable too, rather than reading it back.

**Fill a table with `fill()`**, one call, taking all the rows. It writes a column at a
time; per cell it would be one round trip each. A row may be a mapping, an object with
those attributes, or a sequence in column order — whichever the application already has.

**Screens.** `open_routes(ROUTES, layoutDir, sourceDir)` with the generated table. The
second directory is the `.tsx` sources: pass it in development and a saved layout
rebuilds in place; leave it out in a shipped build. After that, navigating is
`State.route = "about"` from either side.

**The working directory matters.** `res/` resolves relative to it, so start the program
from the project directory. The engine's own font is found beside its shared library and
needs nothing.

**The engine and the binding are versioned together**, as `major.minor`. A different
major is a different ABI and is refused in both directions; the same major with a newer
engine minor is fine, which is what makes a package built against 1.0 keep working against
an engine at 1.7. Each binding checks at load and refuses with a sentence saying which
half is behind. If you see that, rebuild the engine (the bring-up script) or reinstall the
package from the same checkout.

**Read `RDA_DEBUG.txt`** after a run. A layout that silently shows nothing has a reason
written there.

## Changing the look while it runs

`rda.set_theme("res/themes/warm.rdth")` — Node `rda.setTheme`, C# `Rda.SetTheme`, C++
`getMainWindow()->gui().theme().replaceWithFile`. It replaces rather than layers, and every
colour and radius eases to its new value over the new theme's `transitionMs`.

## Drawing, in a viewport and nowhere else

A `<viewport name="chart">` is a hole the backend fills. Python, Node and C# build a list
of 2D commands and send it in one call; it stays until replaced, and an empty list takes
it back down.

```python
w, h = rda.viewport_size("chart")          # 0, 0 before a frame has placed it
d = rda.Drawing()
d.clear("#11141A")
d.rect(8, 8, 80, 40, "#3A6AD0", radius=4)  # circle(x, y, r, c) is this with equal sides
d.line(0, h / 2, w, h / 2, "#2A3140", 1)
d.text(12, 60, "live", "#DCE0E7", 12)
rda.draw("chart", d)
```

Node is `new rda.Drawing()` / `rda.draw(name, d)` / `rda.viewportSize(name)`; C# is
`new Drawing()` / `Rda.Draw(name, d)` / `Rda.ViewportSize(name)`. Colours are what a theme
takes. Coordinates are the viewport's own, `0, 0` at its top-left, and anything past its
edge is clipped.

**C++ gets Vulkan instead**: `RDA::viewports().onDraw(name, [](const RDA::ViewportFrame&
frame) { ... })` hands over the command buffer inside the render pass, with
`RDA::viewportGpu()` supplying the device, allocator, queue and render pass to build
against. It needs `config.viewportMode = RDA::ViewportMode::Widget`; the 2D half needs no
config. One viewport at a time can hold the Vulkan target.
