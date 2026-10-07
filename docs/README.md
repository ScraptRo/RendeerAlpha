# RendeerAlpha documentation

Four folders of reference, and a series that teaches the same thing in order. Start with
whichever you are: the first folder if you want to get it running, the last one if you would
rather be walked through it.

| | |
| --- | --- |
| [setup/](setup/README.md) | What to do after cloning, on Windows and on Linux. Adding the engine to a project in each of the four languages. Your first interface, file by file, in C++, Python, Node or C#. |
| [frontend/](frontend/README.md) | The layout language: what a property may be, what an expression may contain, and every one of the twenty-three elements with its properties. |
| [backend/](backend/README.md) | The other half. What a backend does, the one declaration everything is generated from, and how C++, Python, Node, C# — or anything with an FFI — reaches it. |
| [architecture/](architecture/README.md) | How it fits together and why it is built this way. The reasoning behind the reference, in full. |
| [tutorials/](tutorials/README.md) | The same material taught in order rather than looked up -- six pages from what it is to a backend in four languages. Published on the site, which reads them from here as they are written. |

The short version of the whole thing: an interface is written in TypeScript and compiled
ahead of time into a flat blueprint. The application that loads it contains no parser and
no JavaScript engine. A backend declares state, writes it, answers commands and fills
tables — and draws nothing except inside a `<viewport>`. That seam is what lets the
backend be in any language.

## For a language model

[`skills/rendeer-alpha/`](../skills/rendeer-alpha/README.md) is the same material written
for a model rather than a person: the rules, the complete element and property tables, and
what a binding may contain, in pages small enough for a local model's context. It comes
with an MCP server so a model can compile a layout and read the real error rather than be
told whether it was right.
