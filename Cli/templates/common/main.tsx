// The interface. `state` and `commands` are the names declared in res/state.ts.
export default function Main() {
  return (
    <stack id="root" arrange="vertical" spacing={10} padding={16} animate={160}>
      <label id="title" variant="heading" height="content"
             text={`Clicked ${state.count} ${state.count === 1 ? "time" : "times"}`} />

      <stack id="buttons" arrange="horizontal" spacing={6} height="content">
        <button id="add" variant="primary" text="Add one" width={110} height="content"
                onClick={() => state.count++} />
        <button id="report" text="Report" width={110} height="content"
                onClick={() => commands.report()} />
      </stack>
    </stack>
  )
}
