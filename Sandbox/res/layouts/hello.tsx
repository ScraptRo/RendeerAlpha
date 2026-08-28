// The interface, and everything it does.
//
// This file is never read by the application that shows it. The build runs esbuild over
// it, evaluates it once, and writes hello.rdab -- a flat array of nodes plus compiled
// bytecode for every expression below. What ships is that file and a loader.
//
// The `{() => ...}` thunks are bindings. They are parsed at build time into stack
// operations over `state`, so the running program is reactive without containing a
// JavaScript engine: writing state.count marks exactly the bindings that read it, and
// the next frame evaluates only those.
//
// Try this: change a label, or make a button do something else, and build.

type Props = { title?: string }

// One thing a binding cannot do: close over a variable from the code around it.
//
// A binding is compiled from its own source text, so `() => state.count += step` inside
// a .map() is refused -- `step` never comes along. Build-time loops are still free to
// generate *structure*; it is the expressions inside them that see only state and
// literals. The two buttons below are written out for that reason.

export default function Hello(props: Props) {
  return (
    <stack id="root" vertical={true} spacing={8} padding={12}>
      {/* Bound to state.count: the compiler records the dependency, so this label is
          rewritten when -- and only when -- that signal changes. */}
      <label
        id="title"
        height="content"
        text={() => `Clicked ${state.count} ${state.count === 1 ? "time" : "times"}`}
      />
      <label
        id="subtitle"
        height="content"
        variant="muted"
        text={props.title ?? "Edit this line while it runs - the count above will not reset."}
      />

      <stack id="buttons" vertical={false} spacing={6} height="content">
        <button
          id="add-one"
          text="Add one"
          variant="primary"
          width={110}
          onClick={() => state.count++}
        />
        <button
          id="add-ten"
          text="Add ten"
          variant="ghost"
          width={110}
          onClick={() => state.count += 10}
        />
        <button
          id="reset"
          text="Reset"
          variant="danger"
          width={110}
          onClick={() => state.count = 0}
        />
        <label id="filler" text="" width="fill" height="content" />
      </stack>

      {/* The checkbox drives a signal; the signal drives the checkbox and the two rows
          below it. Neither knows about the other. */}
      <checkbox
        id="details"
        label="Show the arithmetic"
        height="content"
        value={() => state.details}
        onChange={() => state.details = !state.details}
      />
      <label
        id="detail"
        height="content"
        variant="muted"
        visible={() => state.details}
        text={() => `${state.count} doubled is ${state.count * 2}, halved is ${state.count / 2}`}
      />
      <label
        id="verdict"
        height="content"
        visible={() => state.details}
        text={() => state.count > 20 ? "that is a lot of clicking" : "keep going"}
      />

      <textfield
        id="notes"
        mode="document"
        variant="notes"
        text="Every expression in this file was compiled to bytecode by the build."
        height="fill"
        minHeight={54}
      />
    </stack>
  )
}
