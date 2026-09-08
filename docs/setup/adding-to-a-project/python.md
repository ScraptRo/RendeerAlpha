# A Python project

There is no C++ in the project, and no build system either. The engine was built once,
in its checkout; the project compiles its own interface with one command and runs.

## The tree

```
MyApp/
├── app.py
├── state.py             generated -- what `rda build` writes from res/state.ts
└── res/
    ├── state.ts
    ├── themes/app.ts
    └── layouts/{rda.d.ts, tsconfig.json, home.tsx}
```

That is the whole project. `state.py` is generated, so it belongs in `.gitignore`
alongside `__pycache__/`; the compiled `.rdab` and `.rdth` beside their sources are
committed artefacts, like a generated parser, so a checkout without the toolchain still
has an interface to load.

## Two commands

```bash
python -m rda build .
python app.py
```

The first runs the engine's `rda build` over `res/`: every theme to its `.rdth`, every
layout to its `.rdab`, `res/layouts/rda.d.ts`, and `state.py` into the project
directory. Anything already newer than its source is skipped, so it costs nothing to run
before every start — which is what a `scripts/run.bat` two lines long does. Run it again
by hand whenever `res/state.ts` changes; a running Debug engine recompiles an edited
layout by itself.

The second runs from the project directory, because `res/` resolves relative to the
working directory. The engine's own font does not need to be there: the package opens
`librendeer_c.so` or `rendeer_c.dll` from the checkout's `bin/`, and the engine finds its
assets beside itself.

## Where `import rda` comes from

The package is `bindings/python/` in the checkout, and it is pure Python — ctypes against
the engine's C ABI, with nothing to compile. The bring-up registered it with the
interpreter on your `PATH` by writing one `.pth` file into your user site-packages.

In a virtual environment, register it there once:

```bash
python /path/to/RendeerAlpha/bindings/python/register.py
```

or `pip install -e /path/to/RendeerAlpha/bindings/python`, which is the same thing done
through pip. `register.py --check` says whether an interpreter has it, and
`python -m rda where` prints the engine and tool that interpreter would use.

Against a different build of the engine, point `RDA_ENGINE` at the folder holding the
library — a file or a directory both work.

[The first interface in Python](../first-interface/python.md) is the `app.py` that goes
with this.

---

Back to [adding to a project](README.md) · [setup](../README.md) · [all documentation](../../README.md)
