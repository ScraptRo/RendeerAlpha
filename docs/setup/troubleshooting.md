# When it does not work

**"cannot find the engine (rendeer_c.dll)"** — Python or Node could not find the shared
library. It is looked for in `RDA_ENGINE`, beside the package, in the checkout's `bin/`,
and beside the working directory, in that order. An empty or missing `bin/` means the
bring-up has not been run (or `stage` has not) for this checkout; `python -m rda where`
and `npx rda where` print what would be used. C# looks beside the assembly, which is
where its `.csproj` copies the DLL, and honours `RDA_ENGINE` too.

**"cannot find the rda tool"** — the same thing, from `python -m rda build` or
`npx rda build`: `bin/` has no `rda` in it. Run the bring-up, or
`cmake --build build/<preset> --config Debug --target stage`.

**"Cannot find module 'rda'"** — `npm install` has not been run in the Node project, or
its `package.json` points at a path where there is no checkout.

**"Cannot find module 'koffi'"** — the checkout's `bindings/node` has no `node_modules`.
The bring-up installs it; by hand, `npm install` inside that folder.

**"No module named rda"** — this interpreter is not registered. Inside a virtual
environment run `python <checkout>/bindings/python/register.py` with it active, or
`pip install -e <checkout>/bindings/python`. `register.py --check` says what an
interpreter has.

**The font cannot be loaded, and start-up stops** — the engine looked for
`res/fonts/CascadiaMono.ttf` relative to the working directory and then beside its own
library, and found neither. From a project, that means `bin/res/` is missing (stage
again). From a C++ build tree, the program was started from somewhere other than the
directory `res/` was copied into.

**"esbuild not found"** — the tool transforms TypeScript with esbuild and has none. It
looks in `RDA_ESBUILD`, then beside itself (`bin/esbuild`), then in a `node_modules`
above the working directory. The bring-up puts one in `bin/`; by hand, `npm install` in
the checkout and stage again, or download the platform binary from the npm registry.

**"esbuild: ... exists but does not run (Permission denied)"** — a `node_modules` copied
from another machine: no execute bit, or the wrong platform's binary. The build treats it
as absent. `rm -rf node_modules && npm install` on this machine is the fix.

**On Linux, `BadAccess (attempt to access private resource denied)` and an abort after
the first frame** — the application was started with `sudo` under a desktop session. The
X server runs as you, and it refuses to attach shared memory a root process created —
which is how Mesa's software driver presents a frame. Nothing here needs root. If an
earlier `sudo` left root-owned files in the checkout or a project,
`sudo chown -R $USER:$USER` them, and run without `sudo` from then on.

**On Linux, "no DISPLAY or WAYLAND_DISPLAY"** — the binding tests and any application
need a screen. Run from a desktop session rather than over SSH.

**On Linux, `pip install` says "externally-managed-environment"** — Ubuntu's system
Python refuses pip. `register.py` does not use pip; or work in a virtual environment.

**The window opens and stays empty, and the log is clean** -- nothing ever loaded an
interface. Declaring the state is not enough: `on_start` has to call
`load_interface("res/layouts/home.rdab", "res/layouts/home.tsx")` for one screen, or
`open_routes(ROUTES, "res/layouts", "res/layouts")` for a set of them. A backend that
reaches the engine through the C ABI now says so itself, in a warning naming the call
for its language.

**A layout loads but a binding reads zero** — `define()` ran after the layout opened, or
not at all, in whichever language you are writing. A binding resolves the signals it reads
when it is created; one that does not exist yet is created as a number and complained
about in the log.

**`'row' is not in scope`** — a binding inside a `<list>` used a name the row template
does not have. The row is the parameter of the `row={(item) => …}` function; see
[`<list>`](../frontend/elements/list.md).

**Where the log is** — `RDA_DEBUG.txt`, written in the working directory of a Debug
build. Anything worse than `INFO` in it is worth reading; an application that exited
cleanly can still have warned that a binding found no signal.

---

Back to [setup](README.md) · [all documentation](../README.md)
