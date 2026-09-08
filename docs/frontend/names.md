# The four names a layout starts with

## `state`

Every signal the application declared, plus any the layout declared for itself. Reading
one is a dependency; writing one, in a handler, is what makes the interface change.

```tsx
<label text={() => `${state.count} items`} />
<button onClick={() => state.count = 0} />
```

Typed from `res/state.ts`, so a misspelling is an error in the editor and in the build.

## `signal(name, value)`

State a *layout* owns: a panel being open, which section is expanded — things the
application has no reason to know about.

```tsx
signal("expanded", true)

<button onClick={() => state.expanded = !state.expanded} />
<stack visible={() => state.expanded}> … </stack>
```

Scoped to the file, so two layouts may both have an `expanded` without being the same
signal. `signal("theme", "dark", { global: true })` puts it in the namespace the
application shares instead — then it is the same signal C++ and every other layout see.

## `commands.name()`

Work the interface asks the application to do. The layout names which work; the
application supplies what it is.

```tsx
<button text="Save" onClick={() => commands.save()} />
```

Declared in `res/state.ts`, so a command that does not exist does not compile. Commands
take no arguments — write what one needs into state first, in the same handler:

```tsx
onClick={() => { state.exportPath = "out.csv"; commands.exportAll() }}
```

## `row`, inside a `<list>`

The row template's parameter. See [`<list>`](elements/list.md).

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
