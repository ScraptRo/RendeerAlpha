# How RendeerAlpha fits together

A written record of what the pieces are, what runs when, and what is dormant. Written
against the code as it stands — every path below was read out of the source, not
remembered.

RendeerAlpha is glue between an application's backend and its interface. The interface is
written in TypeScript, compiled ahead of time into a flat binary blueprint, and loaded by
an application that contains no parser and no JavaScript engine. The backend is ordinary
C++ that owns the device directly, in its own process — or, through a C ABI, anything
with an FFI.

The thing to hold onto: **TypeScript is a build-time language here, not a runtime one.**
Nothing in a shipped binary evaluates JavaScript. What ships is a blueprint and a loader.

This folder is the reasoning. [setup/](../setup/README.md), [frontend/](../frontend/README.md)
and [backend/](../backend/README.md) are the reference.

## The shape of it

| | |
| --- | --- |
| [The two halves, and what the compiler does](two-halves.md) | build time and run time, and the line between them |
| [Starting up](starting-up.md) | what happens between `main` and the first frame |
| [One frame, and why there is no diffing](one-frame.md) | the loop, on-demand redraw, and why a retained tree needs no reconciliation |
| [Reading and writing state from C++](state-from-cpp.md) | the generated header, and what an accessor costs |
| [Hot reload](hot-reload.md) | editing a layout while it runs, and why the state survives |

## The language, from the inside

| | |
| --- | --- |
| [Components, imports and colour helpers](components.md) | what is expanded at build time, and how |
| [State a layout owns](layout-state.md) | `signal()`, its scope, and where the checking stops |
| [Screens](screens.md) | routes as state, params as signals, and where the swap happens |
| [Asking the application to do something](commands.md) | commands, and why a binding may not call one |

## Drawing

| | |
| --- | --- |
| [Things that move](motion/README.md) | animation as a layer: what it costs, and the four things that had to be right |
| [Type](type.md) | baked sizes, synthesised bold, a character is not a byte, theme and language fields that do not exist, wrapping |
| [Stacks, and what a size means](stacks.md) | the rule that decides a declared size, and alignment at three levels |
| [Pictures, pages and choices](widgets.md) | `<image>` and texture lifetime, `<tabs>`, `<select>` and drawing above everything |
| [Scrolling, tables and large data](large-data.md) | where a fixed tree stops scaling, and the row template that fixes it |
| [Docking](docking.md) | the tree, the chrome, and the theme that colours it |

## The project

| | |
| --- | --- |
| [The repository](repository.md) | what each folder is, and what is not vendored |
| [Testing](testing.md) | driving an interface without opening one, and the four tiers |
| [The bindings, and the seam out of C++](bindings.md) | out-of-process hosting and why it is gone; the C ABI; Python, Node and C# |
| [Where things stand](where-things-stand.md) | what is load-bearing, what is optional, and the known loose ends |

## Running it

```bash
scripts/build.bat && scripts/test.bat
```

That builds the engine and runs its tests. This repository has no application to start:
[your first interface](../setup/first-interface/README.md) is where one comes from, and
[after cloning](../setup/after-cloning/windows.md) is what to expect from the build.

---

Back to [all documentation](../README.md)
