# Recipes

Complete layouts. Each one compiles as written; change the names to yours.

## A screen with a toolbar

`spread="spaceBetween"` puts the title against one edge and the buttons against the
other without positioning anything by hand.

```tsx
export default function Home() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12}>
      <stack id="bar" arrange="horizontal" height="content" spacing={8}
             vAlign="center" spread="spaceBetween">
        <label id="title" variant="heading" width="content" height="content"
               text={() => state.folder} />
        <stack id="actions" arrange="horizontal" width="content" height="content" spacing={6}>
          <button id="up" text="Up" width={60} height="content" variant="quiet"
                  onClick={() => commands.up()} />
          <button id="go" text="Refresh" width={90} height="content" variant="primary"
                  onClick={() => commands.refresh()} />
        </stack>
      </stack>

      <panel id="body" height="fill" />
    </stack>
  )
}
```

## A form

One `<stack arrange="horizontal">` per row, a fixed-width label, a field that fills.

```tsx
export default function Form() {
  return (
    <stack id="root" arrange="vertical" spacing={6} padding={12}>
      <stack id="r1" arrange="horizontal" height="content" spacing={8} vAlign="center">
        <label id="l1" width={90} height="content" text="Folder" />
        <textfield id="f1" width="fill" height="content"
                   text={() => state.folder}
                   onChange={(typed) => state.folder = typed} />
      </stack>

      <stack id="r2" arrange="horizontal" height="content" spacing={8} vAlign="center">
        <label id="l2" width={90} height="content" text="Filter" />
        <textfield id="f2" width="fill" height="content"
                   text={() => state.filter}
                   onChange={(typed) => state.filter = typed} />
        <checkbox id="h" width="content" height="content" label="dot-files"
                  value={() => state.hidden}
                  onChange={(on) => state.hidden = on} />
      </stack>

      <stack id="r3" arrange="horizontal" height="content" spacing={8} hAlign="end">
        <button id="save" text="Save" width={100} height="content" variant="primary"
                onClick={() => commands.save()} />
      </stack>
    </stack>
  )
}
```

## Rows from a table

`of` names a table from `res/state.ts`. The `row` template is built **once**; the list
keeps about as many widgets as fit and re-binds them while scrolling. `item.<column>` is
the only way to read a cell.

```tsx
export default function List() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12}>
      <list id="entries" of="entries" height="fill" rowHeight={26} spacing={2}
            row={(item) => (
              <stack id="row" arrange="horizontal" spacing={10} padding={4} vAlign="center">
                <label id="kind" variant="caption" width={70} height="content"
                       text={() => item.kind} />
                <label id="name" width="fill" height="content" text={() => item.name} />
                <label id="kb" variant="number" width={90} height="content" hAlign="end"
                       text={() => `${item.kb} KB`} />
              </stack>
            )} />

      <label id="count" variant="caption" height="content"
             text={() => `${state.fileCount} entries`} />
    </stack>
  )
}
```

The backend fills it in one call:

```python
Tables.entries.fill([
    {"name": "notes.txt", "kind": "txt", "kb": 1.2},
    {"name": "src", "kind": "folder", "kb": 0},
])
```

## More than one screen

Declare `routes` in `res/state.ts`, and the `route` signal and `back`/`forward` commands
exist. Navigating is a write.

```tsx
export default function About() {
  return (
    <stack id="root" arrange="vertical" spacing={10} padding={16}>
      <label id="t" variant="heading" height="content" text="About" />
      <label id="b" height="content" wrap={true}
             text="The window, the widgets and the animation are the engine's." />
      <stack id="nav" arrange="horizontal" height="content" spacing={6}>
        <button id="back" text="Back" width={90} height="content"
                onClick={() => commands.back()} />
        <button id="home" text="Files" width={90} height="content" variant="primary"
                onClick={() => state.route = "files"} />
      </stack>
    </stack>
  )
}
```

## Showing and hiding

`visible` takes a binding. Inside a `<stack>` an invisible child collapses and the rest
close up over the `animate` duration; elsewhere it simply disappears.

```tsx
signal("expanded", false)

export default function Details() {
  return (
    <stack id="root" arrange="vertical" spacing={6} padding={12} animate={160}>
      <checkbox id="toggle" height="content" label="Details"
                value={() => state.expanded}
                onChange={(on) => state.expanded = on} />

      <panel id="more" height="content" visible={() => state.expanded}>
        <stack id="inner" arrange="vertical" spacing={4} padding={8}>
          <label id="a" height="content" text={() => `${state.fileCount} entries`} />
          <label id="b" height="content" text={() => `${state.totalKb} KB`} />
        </stack>
      </panel>
    </stack>
  )
}
```

## Tabs

```tsx
signal("page", 0)

export default function Pages() {
  return (
    <tabs id="pages" height="fill" value={() => state.page}
          onChange={(i) => state.page = i}>
      <tab id="one" title="Summary">
        <stack id="s1" arrange="vertical" spacing={6} padding={12}>
          <label id="a" height="content" text={() => `${state.fileCount} entries`} />
        </stack>
      </tab>
      <tab id="two" title="Settings">
        <stack id="s2" arrange="vertical" spacing={6} padding={12}>
          <checkbox id="h" height="content" label="Show hidden"
                    value={() => state.hidden}
                    onChange={(on) => state.hidden = on} />
        </stack>
      </tab>
    </tabs>
  )
}
```

## A dropdown

```tsx
export default function Pick() {
  return (
    <stack id="root" arrange="horizontal" height="content" spacing={8} padding={12}
           vAlign="center">
      <label id="l" width={80} height="content" text="Sort by" />
      <select id="sort" width={160} height="content" placeholder="choose"
              value={() => state.sort}
              onChange={(v) => state.sort = v}>
        <option id="n" text="Name" value="name" />
        <option id="s" text="Size" value="size" />
        <option id="k" text="Kind" value="kind" />
      </select>
    </stack>
  )
}
```

## A long column that scrolls

For a fixed set of children. For rows from a table use `<list>`, which stays cheap at ten
thousand.

```tsx
export default function Long() {
  return (
    <scroll id="root" height="fill" spacing={6} padding={12}>
      <label id="a" height="content" text="One" />
      <label id="b" height="content" text="Two" />
      <panel id="c" height={200} />
      <label id="d" height="content" text="Three" />
    </scroll>
  )
}
```

## Movable panels

```tsx
export default function Workspace() {
  return (
    <dockspace id="space" height="fill" persist="dock_layout.ini">
      <dock id="files" title="Files" side="left" size={240}>
        <list id="rows" of="entries" height="fill" rowHeight={24}
              row={(item) => (
                <label id="n" height="content" text={() => item.name} />
              )} />
      </dock>
      <dock id="main" title="Editor" side="center">
        <textfield id="editor" mode="code" height="fill"
                   text={() => state.notes}
                   onChange={(typed) => state.notes = typed} />
      </dock>
    </dockspace>
  )
}
```

## A reusable row

A component is called while the layout is compiled, so it costs nothing at run time. A
prop is substituted, not read from a binding — pass a thunk if the caller wants one.

```tsx
function Stat(props: { label: string, value: RdaBound<string> }) {
  return (
    <stack arrange="horizontal" height="content" spacing={6}>
      <label variant="muted" width={110} height="content" text={props.label} />
      <label width="fill" height="content" text={props.value} />
    </stack>
  )
}

export default function Stats() {
  return (
    <stack id="root" arrange="vertical" spacing={4} padding={12}>
      <Stat id="a" label="Entries" value={() => `${state.fileCount}`} />
      <Stat id="b" label="Total" value={() => `${state.totalKb} KB`} />
    </stack>
  )
}
```
