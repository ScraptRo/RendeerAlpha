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
// How the window is dressed goes through a separate entry point each, so a binding that
// forgot to declare one fails here rather than in somebody's application. The size is
// the engine's own default, stated rather than changed: the fixture's layout is written
// against it.
rda.init({
	name: 'node binding checks',
	width: 1280, height: 800,
	minWidth: 320, minHeight: 200, maxWidth: 1600, maxHeight: 1200,
	resizable: false, opacity: 0.95, x: 120, y: 120,
})
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

// ---- the window, the clipboard, and measuring (ABI 1.2) --------------------------------
console.log('window and clipboard')
rda.setTitle('binding test \u2014 renamed')
check('the title can be set while open', rda.running(), true)

rda.setClipboard('round trip \u2014 dash')
check('the clipboard round-trips', rda.clipboard(), 'round trip \u2014 dash')

const oneWide = rda.measureText('M')
const tenWide = rda.measureText('MMMMMMMMMM')
check('ten monospace characters are ten times one', Math.round(tenWide.width / oneWide.width), 10)
check('and a line has a height', oneWide.height > 0, true)

// ---- pictures from memory --------------------------------------------------------------
// A 2x2 PNG written out here, so the check needs no image library to make one.
console.log('images')
const PNG = Buffer.from(
	'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFUlEQVR4nGM8YeP2n4GBgYEJRIAwACNKAk3nXZn3AAAAAElFTkSuQmCC',
	'base64')

rda.defineImage('fromBytes', PNG)
check('a picture registered from encoded bytes', rda.running(), true)
rda.defineImagePixels('fromPixels', Buffer.alloc(16, 0xC8), 2, 2)
check('and one from raw pixels', rda.running(), true)
rda.defineImage('fromBytes', PNG)
check('registering the same name again is a replace, not an error', rda.running(), true)
rda.forgetImage('fromBytes')
rda.forgetImage('never registered')
check('forgetting one, or one that was never there, is fine', rda.running(), true)

refused('bytes that are not a picture are refused',
	() => rda.defineImage('bad', Buffer.from('not a picture')))
refused('no bytes at all is refused', () => rda.defineImage('bad', Buffer.alloc(0)))
refused('a picture with no name is refused', () => rda.defineImage('', PNG))
refused('too few pixels for the size is refused',
	() => rda.defineImagePixels('bad', Buffer.alloc(4), 2, 2))

// ---- streams and effects ----------------------------------------------------------------
// A 2x2 frame, and a filter over it. Nothing shows either -- what is under test is that the
// calls cross, that a surface is reused rather than rebuilt, and that a shader compiles.
console.log('streams and effects')
const FRAME = Buffer.alloc(16, 0x80)

rda.pushFramePixels('feed', FRAME, 2, 2)
check('a frame pushed from raw pixels', rda.running(), true)
rda.pushFrame('feed', PNG)
check('and one from an encoded frame', rda.running(), true)
check('a stream nothing has drawn yet is still wanted', rda.streamWanted('feed'), true)

refused('a frame with no bytes is refused', () => rda.pushFrame('feed', Buffer.alloc(0)))
refused('too few pixels for the size is refused',
	() => rda.pushFramePixels('feed', Buffer.alloc(4), 2, 2))

rda.defineEffect('grey',
	'void main() { vec4 c = texture(src, uv()); store(vec4(vec3(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722))), c.a)); }')
check('a filter compiled', rda.running(), true)
rda.applyEffect('grey', 'feed', 'feed.grey')
check('and ran over the feed', rda.running(), true)
rda.applyEffect('grey', 'feed', 'feed.grey', [0.5, 1.0])
check('with parameters', rda.running(), true)

refused('a shader that will not compile is refused',
	() => rda.defineEffect('bad', 'void main() { not glsl }'))
refused('an effect nothing defined is refused',
	() => rda.applyEffect('nope', 'feed', 'feed.out'))
refused('reading and writing one stream is refused',
	() => rda.applyEffect('grey', 'feed', 'feed'))

// Several pictures into one filter, and the answer read back rather than looked at.
rda.pushFramePixels('a', Buffer.from([255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255]), 2, 2)
rda.pushFramePixels('b', Buffer.from([0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255]), 2, 2)
rda.defineEffect('blend', 'void main() { store(mix(tap(0, uv()), tap(1, uv()), param(0))); }')
rda.applyEffect('blend', ['a', 'b'], 'mixed', [0.0])
const first = rda.readFrame('mixed')
check('a two-input blend read back at the right size', `${first.width}x${first.height}`, '2x2')
check('and at 0 it is the first picture', Array.from(first.pixels.subarray(0, 4)).join(','), '255,0,0,255')
rda.applyEffect('blend', ['a', 'b'], 'mixed', [1.0])
const second = rda.readFrame('mixed')
check('at 1 it is the second', Array.from(second.pixels.subarray(0, 4)).join(','), '0,0,255,255')

refused('more than four sources is refused',
	() => rda.applyEffect('blend', ['a', 'b', 'a', 'b', 'a'], 'out'))
refused('no sources at all is refused', () => rda.applyEffect('blend', [], 'out'))
refused('writing into one of the sources is refused',
	() => rda.applyEffect('blend', ['a', 'b'], 'b'))
refused('reading a stream that is not there is refused', () => rda.readFrame('nope'))

rda.forgetEffect('grey')
rda.closeStream('feed')
check('forgetting both is fine', rda.running(), true)

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
