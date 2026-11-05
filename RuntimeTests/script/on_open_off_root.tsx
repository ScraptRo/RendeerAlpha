// For the rda_on_open_off_root test: refused, because a widget has no opening of its own.
export default function OffRoot() {
  return <stack id="root"><panel id="inner" onOpen={() => state.count = 1} /></stack>
}
