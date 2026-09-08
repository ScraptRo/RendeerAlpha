// The first screen. Small, but it reads every kind of signal and asks for the command,
// so loading it resolves the names the backend declared -- which is half of what the
// test is checking. A name the backend forgot warns in the log here.

export default function First() {
  return (
    <stack id="root" arrange="vertical" spacing={8} padding={12}>
      <label id="n" height="content" text={() => `count ${state.count}`} />
      <label id="t" height="content" text={() => state.text} />
      <label id="f" height="content" text={() => state.flag ? "on" : "off"} />
      <button id="bump" text="Bump" height="content" width={120}
              onClick={() => commands.bump()} />

      <list id="items" of="items" height="fill" rowHeight={22} spacing={2}
            row={(item) => (
              <stack id="row" arrange="horizontal" spacing={8} padding={2} vAlign="center">
                <label id="l" width="fill" height="content" text={() => item.label} />
                <label id="v" width={80} height="content" hAlign="end"
                       text={() => `${item.value}`} />
                <label id="o" width={40} height="content"
                       text={() => item.on ? "yes" : "no"} />
              </stack>
            )} />
    </stack>
  )
}
