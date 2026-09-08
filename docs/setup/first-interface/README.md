# Your first interface

A counter, a text field and a button that asks the backend to do something. Small enough
to type in, and it exercises the whole path: a declaration both sides are generated from,
a theme, a layout with bindings and handlers, and a backend answering a command.

Every language starts from the same three files under `res/`. This page is those files.
The one after it is the backend, in the language you are writing:
[C++](cpp.md) · [Python](python.md) · [Node](node.md) · [C#](csharp.md).

What sits beside them — a `CMakeLists.txt` for C++, a `package.json` for Node, a
`.csproj` for C#, nothing at all for Python — is in
[adding to a project](../adding-to-a-project/README.md), one page per language. Set
that up first, after the engine's bring-up script has run: the declaration below is
TypeScript, and turning it into code both sides use needs the `rda` tool and the
esbuild the bring-up put in the checkout's `bin/`.

## `res/state.ts` — what both sides agree on

```ts
export const state = {
  count: { value: 0,  doc: "how many times the button has been pressed" },
  notes: { value: "", doc: "what is typed in the field" },
}

export const commands = {
  save: "write the notes somewhere",
}
```

Declared once, here. The build generates every other side from it: the C++ header, the
Python module, the JavaScript module, the C# class, and the TypeScript a layout is checked
against. So `state.count` in the layout and `State::count()` in C++ (or `State.count`
anywhere else) are the same signal by construction, and a misspelling on either side is a
compile error rather than a value that quietly holds zero forever.

A field's type is whatever its initial value is. The long form `{ value, doc }` is
optional; the documentation reaches every generated side and the tooltip your editor shows.

## `res/themes/app.ts` — the look

```ts
export const button: RdaButtonTheme = {
  default: { transitionMs: 140, easing: "out" },
  primary: {
    normal: "#3A6AD0", hovered: "#4C7CE6", pressed: "#2A54B4",
    text: "#FFFFFF", radius: 6,
  },
}

export const label: RdaLabelTheme = {
  heading: { color: "#DDE3EC", fontSize: 22, weight: "bold" },
}

export const textfield: RdaTextfieldTheme = {
  default: { transitionMs: 140, easing: "out" },
  notes: {
    mode: "document", background: "#1C1E24", text: "#DDE1E8",
    border: "#333A46", borderWidth: 1, radius: 8, padding: 14,
  },
}
```

Each export names a widget type; each key inside it is a variant, chosen by a widget with
`variant="..."`. A field left out inherits from that widget's `default`, so every entry is
only the decisions it makes. `transitionMs` on the `default` variant is why a button's
colour follows the pointer instead of snapping.

The `: Rda...Theme` annotations come from the generated `rda.d.ts`, so a misspelled field
is a red squiggle before it is a build error.

## `res/layouts/home.tsx` — the interface

```tsx
export default function Home() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12} animate={160}>
      <label id="title" variant="heading" height="content"
             text={() => `Clicked ${state.count} ${state.count === 1 ? "time" : "times"}`} />

      <stack id="buttons" arrange="horizontal" spacing={6} height="content">
        <button id="add"   variant="primary" text="Add one" width={110} height="content"
                onClick={() => state.count++} />
        <button id="reset" text="Reset" width={110} height="content"
                onClick={() => state.count = 0} />
      </stack>

      <textfield id="notes" mode="document" variant="notes" height="fill"
                 text={() => state.notes}
                 onChange={(typed) => state.notes = typed} />

      <button id="save" text="Save" width={110} height="content"
              onClick={() => commands.save()} />
    </stack>
  )
}
```

Three kinds of value, and this file has all of them. `text="Reset"` is a constant, written
into the blueprint. `text={() => ...}` is a **binding**: parsed at build time into stack
operations over `state`, evaluated when a signal it reads changes and at no other time.
`onClick={() => ...}` is a **handler**, the same machinery pointed the other way. None of
them is a closure that ships — the file is evaluated once, at build time, and what the
application loads is a flat blueprint with no parser and no JavaScript engine.

`commands.save()` names work for the backend to do. The layout says *which*; the backend
says *what*.

## `res/layouts/tsconfig.json` — for the editor only

```json
{
  "compilerOptions": {
    "noEmit": true, "strict": true, "target": "ES2020", "module": "ESNext",
    "moduleResolution": "bundler", "jsx": "react", "jsxFactory": "h",
    "types": [], "skipLibCheck": true
  },
  "include": ["rda.d.ts", "*.tsx", "../themes/*.ts"]
}
```

Nothing emits from it, and it is worth more than it looks. An unknown element or an
unknown property **compiles**: the build does not check names against the widget schema,
so `<div>` and `colour="#fff"` both produce a blueprint, and the loader then skips the
element and ignores the property with a line in `RDA_DEBUG.txt`. The editor is where that
typo is meant to be caught, and this file plus the generated `rda.d.ts` is what catches
it.

## What you should see

A heading reading *Clicked 0 times*, two buttons, a text area, and a Save button. *Add one*
counts; *Reset* returns to zero; *Save* prints whatever is in the field, from the backend.

Then, with the application still running, change `"Add one"` to `"Add"` in `home.tsx` and
save the file. The interface rebuilds in place and the count survives — state lives in the
application, not inside the thing being replaced.

Now the backend: [C++](cpp.md) · [Python](python.md) · [Node](node.md) · [C#](csharp.md).

---

Back to [setup](../README.md) · [all documentation](../../README.md)
