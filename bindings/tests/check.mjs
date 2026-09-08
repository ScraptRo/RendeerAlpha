/**
 * What the Node binding does, checked against a running engine.
 *
 * Run by ctest as `rda_node_binding`. It opens a real window on a real device, because
 * that is what the thing under test does -- a binding that cannot be exercised without
 * one is a binding whose bugs only show up in an application.
 *
 * The checklist here and the one in check.py are deliberately the same, in the same
 * order. Two bindings over one ABI should be able to answer the same questions, and a
 * line that appears in one output and not the other is a gap worth seeing.
 */

import * as rda from 'rda'
import { State, Commands, Tables, ROUTES, define } from './state.mjs'

const failures = []

/**
 * A value as one short, printable line. Truncated, because five thousand x's in a test
 * log is five thousand x's nobody reads.
 */
function shown(value) {
	const text = JSON.stringify(value) ?? String(value)
	return text.length <= 60 ? text : `${text.slice(0, 57)}... (${text.length} chars)`
}

function check(what, got, want) {
	const ok = JSON.stringify(got) === JSON.stringify(want)
	console.log(ok ? '  ok   ' : '  FAIL ', what, '->', shown(got),
		ok ? '' : `(wanted ${shown(want)})`)
	if (!ok) failures.push(what)
}

/** A call that must fail, and must say so rather than quietly doing nothing. */
function refused(what, work) {
	try {
		work()
		check(what, 'accepted', 'refused')
	} catch (problem) {
		console.log('  ok   ', what, '->', problem.constructor.name)
	}
}

const settle = (ms = 500) => new Promise((r) => setTimeout(r, ms))

// ---- lifecycle -----------------------------------------------------------------------
console.log('lifecycle')
rda.init({ name: 'node binding checks' })
check('init returns with the engine up', rda.running(), true)

define()
rda.loadInterface('res/layouts/first.rdab')
check('an interface loads', true, true)
refused('a blueprint that is not there is refused',
	() => rda.loadInterface('res/layouts/nope.rdab'))

// ---- signals -------------------------------------------------------------------------
console.log('signals')
State.count = 41
check('numbers', State.count, 41)
State.count += 1
check('read-modify-write', State.count, 42)
State.flag = true
check('booleans', State.flag, true)
State.text = 'written'
check('text', State.text, 'written')
State.text = 'éàü — non-ascii'
check('text is utf-8 on the way out and back', State.text, 'éàü — non-ascii')

// Longer than the buffer the binding guesses at, so the length-then-retry path runs.
const longText = 'x'.repeat(5000)
State.note = longText
check('text longer than the guess comes back whole', State.note, longText)
check('and is the length it should be', State.note.length, 5000)

refused('a misspelled signal is refused', () => { State.notAThing = 1 })

// ---- tables --------------------------------------------------------------------------
console.log('tables')
Tables.items.fill([
	{ label: 'one', value: 1.5, on: true },
	{ label: 'two', value: 2.5, on: false },
])
check('fill sets the count', Tables.items.rows, 2)
check('text came back', Tables.items.label(0), 'one')
check('numbers came back', Tables.items.value(1), 2.5)
check('booleans came back', Tables.items.on(0), true)
check('and a row that was not set is false', Tables.items.on(1), false)

// The other row shape this binding promises to take.
Tables.items.fill([['array', 3.0, true]])
check('a sequence in column order works', Tables.items.label(0), 'array')
Tables.items.fill([{ label: 'object', value: 4.0, on: false }])
check('an object with those attributes works', Tables.items.label(0), 'object')

Tables.items.rows = 5
check('resizing grows it', Tables.items.rows, 5)
check('and the new rows are empty', Tables.items.label(4), '')
Tables.items.setLabel(['a', 'b'], 3)
check('a run can start anywhere', Tables.items.label(3), 'a')
check('and leaves the row before it alone', Tables.items.label(2), '')

refused('a row past the end is refused', () => Tables.items.label(999))

Tables.items.fill(Array.from({ length: 10000 }, (_, i) =>
	({ label: `r${i}`, value: i, on: i % 2 === 0 })))
check('ten thousand rows land', Tables.items.rows, 10000)
check('the last of them is right', Tables.items.label(9999), 'r9999')

// ---- screens -------------------------------------------------------------------------
console.log('screens')
rda.setTransitionMs(0)            // instant, so a check cannot race a cross-fade
rda.openRoutes(ROUTES, 'res/layouts')
await settle()
check('the first declared route is where it opens', State.route, 'first')
check('with nothing to go back to', rda.canGoBack(), false)

State.route = 'second'
await settle()
check('writing the signal navigates', State.route, 'second')
check('and that is history', rda.canGoBack(), true)
check('back returns', rda.back(), true)
await settle()
check('to where it was', State.route, 'first')
check('and forward is open', rda.canGoForward(), true)
check('forward returns', rda.forward(), true)
await settle()
check('to where it went', State.route, 'second')

// ---- commands ------------------------------------------------------------------------
console.log('commands')
const asked = []
Commands.bump(() => { asked.push(State.count) })

// The one place this list differs from check.py in *how*. A JavaScript value may only be
// touched on the thread that owns it, so the engine cannot call this handler -- it
// records that the command fired and poll() runs it here. invoke() therefore queues, and
// the handler has run by the next poll rather than by the time invoke returns.
State.count = 7
rda.invoke('bump')
check('invoke queues rather than calling', asked, [])
rda.poll()
check('and the poll runs the handler', asked, [7])

refused('invoking a command nothing is bound to is refused', () => rda.invoke('unused'))
refused('invoking a command that does not exist is refused', () => rda.invoke('nope'))

// ---- the theme ------------------------------------------------------------------------
console.log('theme')
refused('changing to a theme that is not there is refused', () => rda.setTheme('layouts/nope.rdth'))
refused('changing to a theme with no name is refused', () => rda.setTheme(''))

// ---- the viewport --------------------------------------------------------------------
// The second screen is showing, and it has <viewport name="canvas"> on it.
console.log('viewport')
await settle(300)
const canvas = rda.viewportSize('canvas')
check('a placed viewport reports its size', [canvas.width, canvas.height], [400, 120])
const nowhere = rda.viewportSize('nowhere')
check('one nothing has drawn is zero', [nowhere.width, nowhere.height], [0, 0])

const drawing = new rda.Drawing()
drawing.clear('#11141A')
drawing.rect(8, 8, 80, 40, '#3A6AD0', 4)
drawing.line(0, 0, canvas.width, canvas.height, '#4C7CE6', 2)
drawing.circle(200, 60, 24, '#6ED09C')
drawing.text(12, 60, 'drawn from Node', '#DCE0E7', 12)
check('five shapes built', drawing.length, 5)
rda.draw('canvas', drawing)
await settle(300)
check('the drawing was accepted', rda.running(), true)

rda.draw('canvas', new rda.Drawing())   // an empty one takes it back down
refused('a colour that is not one is refused', () => new rda.Drawing().clear('mauve'))
refused('drawing in a viewport without a name is refused', () => rda.draw('', drawing))

// ---- shutdown ------------------------------------------------------------------------
console.log('shutdown')
rda.stop()
await rda.run()                   // drains the last commands, joins the thread
check('the engine stops', rda.running(), false)

console.log()
console.log(failures.length
	? 'FAILED: ' + failures.join(', ')
	: 'node binding: all checks passed')
process.exit(failures.length ? 1 : 0)
