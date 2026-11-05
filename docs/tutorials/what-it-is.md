## The idea

RendeerAlpha is glue between an application’s backend and its interface. The interface is written in TypeScript with JSX and compiled **ahead of time** into a flat binary blueprint — a `.rdab` file. The program that loads that file parses nothing: it reads a buffer and builds widgets, and the functions a layout defines arrive as bytecode compiled when it was built.

```tsx title="res/layouts/home.tsx"
export default function Hello() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12}>
      <label id="title" height="content"
             text={() => `Clicked ${state.count} times`} />
      <button id="add" text="Add one" height="content"
              onClick={() => state.count++} />
    </stack>
  )
}
```

Those `{() => ...}` arrows are not closures that ship. They are parsed at build time into stack bytecode over `state`, so the running program is reactive without needing a JavaScript engine: writing `state.count` marks exactly the bindings that read it, and the next frame evaluates only those.

## The two halves

An application here is an interface and a backend, and the line between them is drawn in one specific place. A backend does five things and nothing else:

| A backend | means |
| --- | --- |
| Declares | the names the interface is written against — signals, commands, tables, screens |
| Writes state | `count = 3`, and every binding that reads it re-evaluates next frame |
| Answers commands | what `commands.save()` actually does |
| Fills tables | the rows a `<list>` shows |
| Draws | inside a `<viewport>` and nowhere else |

A backend lays nothing out, owns no widget and never sees one. That is the design rather than a gap in the bindings — and it is why the seam can be at the state instead of at the pixels. The consequence worth stating plainly: **a layout cannot tell which language is behind it.** The same `home.rdab` loads under C++, Python, Node and C#, and nothing in it mentions any of them.

## Why compile the interface

Everything the layout language allows has to survive being turned into data at build time. That constraint is where the rest of the design comes from:

- Nothing that ships contains a parser — esbuild is a build-time dependency and only that. The one script engine in the program is QuickJS, for the functions a layout defines, and it starts only if a layout has any.
- Every element and property is declared once, in the engine’s widget schema, and the `rda.d.ts` your layout is checked against is generated from that same table. If it is in autocomplete it exists at run time.
- A state declaration generates the C++ header, the Python module, the JavaScript module, the C# class and the TypeScript the layout is checked against — so a misspelling is a compile error on the side that made it.
- The backend owns the GPU device directly, in its own process, with no bridge in the way.

> The engine repository is the engine alone — no bundled demo application. Cloning it gets you a library, the `rda` toolchain, and the build rules that compile an interface.

## What the rest of this series covers

- **Installing the engine** — what you need, and the one bring-up script that builds it.
- **Adding it to a project** — the four project shapes: C++ through CMake, and Python, Node or C# pointing at `bin/`.
- **Your first interface** — the three files under `res/` that every interface starts from.
- **The layout language** — constants, bindings and handlers, the expression grammar, sizing, and the twenty-seven elements.
- **State and backends** — one declaration, four generated sides, and the same application written in each language.
