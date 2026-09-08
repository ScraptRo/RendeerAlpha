// The second screen, so there is something to navigate to and something for history to
// restore. `count` is this route's param, which is what makes going back put it back.

export default function Second() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12} hAlign="center">
      <label id="h" variant="heading" height="content" text="second" />
      <label id="n" height="content" text={() => `count was ${state.count}`} />
      <label id="note" height="content" wrap={true} width={400} text={() => state.note} />

      {/* A surface for the backend to draw into. The checks measure it and put a
          drawing in it, which is the whole of what this ABI offers a viewport. */}
      <viewport id="canvas" name="canvas" width={400} height={120} />
    </stack>
  )
}
