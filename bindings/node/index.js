/**
 * rda -- write a RendeerAlpha backend in Node.
 *
 *     import * as rda from 'rda'
 *     import { State, Commands, define } from './state.mjs'   // by `rda node`
 *
 *     rda.init({ name: 'My application', theme: 'res/themes/app.rdth' })
 *     define()
 *     rda.loadInterface('res/layouts/home.rdab')
 *
 *     Commands.save(() => console.log(State.notes))
 *
 *     await rda.run()
 *
 * Node is a *backend* here, which is what the engine is for. The interface is a compiled
 * layout -- written in TSX, checked against a generated schema, styled by a theme, bound
 * to signals and animated by the engine. Node does not draw it; it loads it, writes the
 * state it reads, and answers the work it asks for. That is the same job C++ has, and the
 * layout cannot tell which of them is behind it.
 *
 * ---- the two loops ----
 *
 * The engine owns a thread and a loop of its own; Node owns its own. They do not merge,
 * and the whole shape of this package follows from that:
 *
 *   * `init()` returns once the engine is up, so the line after it may write state. The
 *     engine keeps running on its thread while Node carries on -- a server, a watcher or
 *     a REPL is not blocked by an open window.
 *   * Every call here is *synchronous*, and costs a round trip to the engine's next
 *     frame. That is the right price for a backend and the wrong one for a render path,
 *     which is why there is no drawing in this package at all.
 *   * A command handler is **not** a callback the engine invokes. It cannot be: a
 *     JavaScript value may only be touched on the thread that owns it, and an FFI
 *     trampoline called from the engine's thread deadlocks rather than crashing. So the
 *     engine records that a command fired and `run()` collects it here, on Node's loop.
 *
 * `run()` is what ties that together: it polls for commands and resolves when the window
 * closes. Await it, or ignore it and call `poll()` from a loop of your own.
 */

import * as engine from './engine.js'

export { RdaError, poll, onCommand, invoke, load } from './engine.js'

let started = false

/**
 * Starts the engine and returns once it is up.
 *
 * Up means the window and the renderer are ready, so the line after this may declare
 * state and load an interface. The engine runs on a thread of its own, which is why this
 * returns at all.
 *
 * @param {{name?: string, theme?: string, languages?: string, vsync?: boolean,
 *          font?: string, fontHeight?: number}} [config]
 */
export function init(config = {}) {
	const fn = engine.calls()
	if (started) throw new engine.RdaError('the engine is already running')

	const handle = fn.configNew()
	if (!handle) throw new engine.RdaError('cannot allocate a config')
	try {
		fn.setName(handle, String(config.name ?? 'RendeerAlpha'))
		if (config.theme) fn.setTheme(handle, String(config.theme))
		if (config.languages) fn.setLanguages(handle, String(config.languages))
		fn.setVsync(handle, config.vsync === false ? 0 : 1)
		if (config.font || config.fontHeight) {
			fn.setFont(handle, String(config.font ?? ''), Number(config.fontHeight ?? 0))
		}
		if (!fn.init(handle)) {
			throw new engine.RdaError(`the engine did not start: ${fn.lastError()}`)
		}
		started = true
	} finally {
		// rda_init copies what it needs, so this is freed on both paths.
		fn.configFree(handle)
	}
}

/**
 * Shows a compiled layout (.rdab), replacing whatever was showing.
 *
 * `source` is the .tsx it came from. Where the build can compile one, naming it is what
 * lets the engine reload the interface when the file changes.
 */
export function loadInterface(blueprint, source = '') {
	const fn = engine.calls()
	if (!fn.loadInterface(String(blueprint), String(source))) {
		throw new engine.RdaError(`cannot load '${blueprint}': ${fn.lastError()}`)
	}
}

/**
 * Shows a set of screens, one at a time, with `state.route` naming which.
 *
 * `routes` is the ROUTES table a generated state module exports -- an object of name to
 * `{ layout, params }`, in the order they were declared, because the first is where the
 * application opens unless `route` already says otherwise.
 *
 * After this, navigating is an ordinary write: `State.route = 'catalogue'`.
 */
export function openRoutes(routes, layoutDir, sourceDir = '') {
	const fn = engine.calls()
	const names = []
	const layouts = []
	const params = []
	for (const [name, route] of Object.entries(routes)) {
		names.push(name)
		if (typeof route === 'string') {          // { home: 'hello' }, the short spelling
			layouts.push(route)
			params.push('')
		} else {
			layouts.push(String(route.layout))
			params.push((route.params ?? []).join(','))
		}
	}
	if (!names.length) throw new engine.RdaError('no routes to open')
	if (!fn.openRoutes(names, layouts, params, names.length, String(layoutDir), String(sourceDir))) {
		throw new engine.RdaError(`cannot open the screens: ${fn.lastError()}`)
	}
}

/** How long one screen takes to cross-fade into the next. Zero is an instant swap. */
export function setTransitionMs(ms) {
	engine.calls().setTransitionMs(Number(ms))
}

/** One step back through the screens. False when there is nowhere to go. */
export function back() { return engine.calls().routeBack() !== 0 }
/** One step forward again. False when there is nowhere to go. */
export function forward() { return engine.calls().routeForward() !== 0 }
export function canGoBack() { return engine.calls().canGoBack() !== 0 }
export function canGoForward() { return engine.calls().canGoForward() !== 0 }

/** Whether the window is still open. */
export function running() { return engine.calls().running() !== 0 }

/** Asks the engine to close. Safe from anywhere, including before init(). */
export function stop() { engine.calls().stop() }

/** Blocks until the engine has stopped and its thread has been joined. */
export function wait() { engine.calls().wait() }

/**
 * Keeps the application alive: collects commands, and resolves once the window closes.
 *
 * This is where a Node backend differs from a C++ one, and it is not a workaround. Node
 * already has a loop; the engine has its own. `run()` is the meeting point -- a timer on
 * Node's loop that drains what the engine's loop recorded. Everything else Node is doing
 * -- a server answering requests, a file watcher, a queue consumer -- carries on between
 * ticks, because nothing here blocks for longer than one poll.
 *
 * @param {{intervalMs?: number}} [options] how often to look. 8ms is faster than a frame,
 *        so a click is answered before the interface has drawn the next one.
 */
export function run({ intervalMs = 8 } = {}) {
	return new Promise((resolve) => {
		const timer = setInterval(() => {
			engine.poll()
			if (!running()) {
				clearInterval(timer)
				engine.poll()   // whatever arrived on the last frame still runs
				wait()
				started = false
				resolve()
			}
		}, intervalMs)
		// Nothing else keeps this process alive on purpose: the window is the application,
		// so the timer holding the loop open is exactly right. unref() would let Node exit
		// with a window still on screen.
	})
}

/**
 * Swaps the whole look while the window stays open.
 *
 * The compiled `.rdth` replaces what was there rather than layering on top, so a theme
 * that does not mention a variant does not inherit the last one's. Colours and radii
 * travel to their new values over the transitionMs the new theme gives them.
 */
export function setTheme(path) {
	const fn = engine.calls()
	if (!fn.setTheme(path)) throw new engine.RdaError(`cannot use the theme '${path}': ${fn.lastError()}`)
}

// ---- viewports -----------------------------------------------------------------------

/** A colour as the engine packs them: 0xAABBGGRR. Takes what a theme takes. */
function packColor(value) {
	if (typeof value === 'number') return value >>> 0
	let text = String(value).replace(/^#/, '')
	if (text.length === 3) text = [...text].map((c) => c + c).join('')
	if (text.length === 6) text += 'FF'
	if (text.length !== 8) throw new engine.RdaError(`a colour is #RGB, #RRGGBB or #RRGGBBAA, not ${value}`)
	const [r, g, b, a] = [0, 2, 4, 6].map((i) => parseInt(text.slice(i, i + 2), 16))
	return ((r | (g << 8) | (b << 16) | (a << 24)) >>> 0)
}

/**
 * What to draw in a `<viewport>`, in its own coordinates: 0,0 is its top-left corner.
 *
 * Built up here and sent in one call, because every call into the engine crosses to its
 * loop thread and waits -- a shape at a time would be a frame at a time. What is sent
 * stays on screen until another drawing replaces it.
 *
 *     const d = new Drawing()
 *     d.clear('#11141A')
 *     samples.forEach((v, i) => d.rect(i * 12, h - v, 8, v, '#3A6AD0', 2))
 *     draw('chart', d)
 */
export class Drawing {
	#commands = []

	get length() { return this.#commands.length }
	get commands() { return this.#commands }

	/** Fills the whole viewport, whatever size it turned out to be. */
	clear(color) { return this.#add(engine.DRAW_CLEAR, color) }

	rect(x, y, w, h, color, radius = 0) {
		return this.#add(engine.DRAW_RECT, color, x, y, w, h, radius)
	}

	/** A rect with equal sides and a radius of half of them. Centred on x, y. */
	circle(x, y, radius, color) {
		return this.#add(engine.DRAW_RECT, color, x - radius, y - radius, radius * 2, radius * 2, radius)
	}

	line(x1, y1, x2, y2, color, width = 1) {
		return this.#add(engine.DRAW_LINE, color, x1, y1, x2, y2, width)
	}

	/** Top-left at x, y. A size of 0 is the interface's own. */
	text(x, y, text, color, size = 0) {
		return this.#add(engine.DRAW_TEXT, color, x, y, 0, 0, size, String(text))
	}

	#add(op, color, a = 0, b = 0, c = 0, d = 0, e = 0, text = null) {
		this.#commands.push({ op, color: packColor(color), a, b, c, d, e, text })
		return this
	}
}

/**
 * Puts `drawing` in the `<viewport name="...">` and leaves it there.
 *
 * An empty Drawing takes it back down; a viewport nothing has ever drawn into shows the
 * engine's own 3D scene instead, which is what this element used to be for.
 */
export function draw(name, drawing) {
	const commands = drawing?.commands
	if (!Array.isArray(commands)) throw new engine.RdaError('draw() takes a Drawing')
	const fn = engine.calls()
	if (!fn.viewportDraw(name, commands, commands.length)) {
		throw new engine.RdaError(`cannot draw in '${name}': ${fn.lastError()}`)
	}
}

/** `{ width, height }` in the pixels a drawing uses; zeroes before a frame placed it. */
export function viewportSize(name = 'main') {
	const width = [0]
	const height = [0]
	const fn = engine.calls()
	if (!fn.viewportSize(name, width, height)) {
		throw new engine.RdaError(`cannot measure '${name}': ${fn.lastError()}`)
	}
	return { width: width[0], height: height[0] }
}
