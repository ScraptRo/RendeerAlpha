# bin/ — the built engine, for everything that is not C++

This folder is written by the build (`--target stage`, which the bring-up scripts run)
and is not tracked. Delete it and build again to refresh it; `BUILD.txt` says which
configuration is in here.

| | |
| --- | --- |
| `rendeer_c.dll` / `librendeer_c.so` | the engine as a shared library — what ctypes, koffi and P/Invoke open |
| `rendeer_c.lib`, `rendeer_c.pdb` | Windows only: the import library and the symbols |
| `rda` / `rda.exe` | the toolchain. `rda build <project> --python` compiles a project's interface |
| `esbuild` / `esbuild.exe` | what the toolchain transforms TypeScript with; put here by the bring-up |
| `include/RendeerC.h` | the C ABI, for a C or C++ program that opens the library rather than linking the engine |
| `res/` | the engine's own font and syntax definitions. Found from here automatically |

## How each language reaches it

**Python** — `python -m rda build .` then `python app.py`, once `import rda` resolves to
`../bindings/python`. The bring-up registers it with your interpreter; in a virtual
environment, `python <checkout>/bindings/python/register.py` or
`pip install -e <checkout>/bindings/python`.

**Node** — `npm install <checkout>/bindings/node` in your project, then `npx rda build .`
and `node app.mjs`.

**C#** — compile `../bindings/csharp/*.cs` into your project, generate `state.cs` with
`rda build . --csharp`, and copy `rendeer_c.dll` beside your executable (or set
`RDA_ENGINE` to this folder).

**Anything else with an FFI** — open the shared library and call what `include/RendeerC.h`
declares. `docs/backend/c-abi.md` in the checkout is the contract.

Every binding also honours `RDA_ENGINE=<this folder>` as an override, for when the
library is somewhere no convention would guess.
