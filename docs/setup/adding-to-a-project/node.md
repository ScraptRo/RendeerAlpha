# A Node project

Same shape as the Python one, with `npm` where Python has a `.pth` file: the `rda` package
is a path dependency on the engine checkout.

## The tree

```
MyApp/
├── package.json         one dependency: rda, by path
├── app.mjs
├── state.mjs            generated -- what `rda build` writes from res/state.ts
└── res/                 exactly as the others
```

`.mjs` rather than `.js` so the file is an ES module whatever any package.json says.
`state.mjs` is generated, so it belongs in `.gitignore` with `node_modules/`.

## package.json

```json
{
  "name": "myapp",
  "private": true,
  "type": "module",
  "scripts": { "start": "rda build . && node app.mjs" },
  "dependencies": { "rda": "file:../RendeerAlpha/bindings/node" }
}
```

`npm install` here, once. npm answers a `file:` dependency with a **link** rather than a
copy, so `node_modules/rda` points into the checkout, and a rebuilt engine is seen at
once. The package's own dependency, koffi, is installed inside it by the bring-up — Node
has no FFI of its own, and koffi ships a prebuilt binary per platform, so that install
downloads rather than compiles.

## Two commands

```bash
npx rda build .
node app.mjs
```

The first runs the engine's `rda build` over `res/`: every theme to its `.rdth`, every
layout to its `.rdab`, `res/layouts/rda.d.ts`, and `state.mjs` into the project
directory. Anything already newer than its source is skipped. `npm start`, with the
script above, does both.

The second runs from the project directory, because `res/` resolves relative to the
working directory. The engine's own font does not need to be there: the package opens
the shared library from the checkout's `bin/`, and the engine finds its assets beside
itself.

`npx rda where` prints the engine and tool the project would use; `RDA_ENGINE` overrides
both.

[The first interface in Node](../first-interface/node.md) is the `app.mjs` that goes
with this.

---

Back to [adding to a project](README.md) · [setup](../README.md) · [all documentation](../../README.md)
