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
 *          font?: string, fontHeight?: number, width?: number, height?: number,
 *          icon?: string, decorated?: boolean, resizable?: boolean,
 *          maximized?: boolean, fullscreen?: boolean, alwaysOnTop?: boolean,
 *          transparent?: boolean, opacity?: number,
 *          minWidth?: number, minHeight?: number, maxWidth?: number, maxHeight?: number,
 *          x?: number, y?: number}} [config]
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
		if (config.width || config.height) {
			fn.setSize(handle, Number(config.width ?? 0), Number(config.height ?? 0))
		}

		// How the window is dressed. Each one is sent only when it differs from the
		// engine's own default, so a config nobody touched makes no calls at all.
		if (config.icon) fn.setIcon(handle, String(config.icon))
		if (config.decorated === false) fn.setDecorated(handle, 0)
		if (config.resizable === false) fn.setResizable(handle, 0)
		if (config.maximized) fn.setMaximized(handle, 1)
		if (config.fullscreen) fn.setFullscreen(handle, 1)
		if (config.alwaysOnTop) fn.setAlwaysOnTop(handle, 1)
		if (config.transparent) fn.setTransparent(handle, 1)
		if (config.opacity !== undefined && config.opacity !== 1) {
			fn.setOpacity(handle, Number(config.opacity))
		}
		if (config.minWidth || config.minHeight || config.maxWidth || config.maxHeight) {
			fn.setSizeLimits(handle, Number(config.minWidth ?? 0), Number(config.minHeight ?? 0),
			                 Number(config.maxWidth ?? 0), Number(config.maxHeight ?? 0))
		}
		if (config.x !== undefined || config.y !== undefined) {
			// An axis nobody set stays the platform's choice, which is not the same
			// answer as 0 -- that is a corner of the screen.
			const unplaced = -2147483648
			fn.setPosition(handle, config.x === undefined ? unplaced : Number(config.x),
			               config.y === undefined ? unplaced : Number(config.y))
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
/**
 * Registers a picture a backend made, under `name`.
 *
 * `data` is an encoded image -- PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PNM -- as a Buffer.
 * A layout shows it with `src="mem:<name>"`. Registering the same name again replaces
 * what every <image> naming it draws.
 */
export function defineImage(name, data) {
	const bytes = Buffer.isBuffer(data) ? data : Buffer.from(data)
	if (bytes.length === 0) throw new engine.RdaError('there are no bytes to make an image from')
	if (!engine.calls().imageDefine(name, bytes, bytes.length)) {
		throw new engine.RdaError(`cannot register the image '${name}'`)
	}
}

/**
 * The same, from raw pixels: four bytes each (R, G, B, A), rows tightly packed,
 * `width * height * 4` in all.
 */
export function defineImagePixels(name, pixels, width, height) {
	const bytes = Buffer.isBuffer(pixels) ? pixels : Buffer.from(pixels)
	const wanted = width * height * 4
	if (bytes.length < wanted) {
		throw new engine.RdaError(`a ${width}x${height} image needs ${wanted} bytes, and there are ${bytes.length}`)
	}
	if (!engine.calls().imageDefinePixels(name, bytes, width, height)) {
		throw new engine.RdaError(`cannot register the image '${name}'`)
	}
}

/**
 * Pushes an encoded frame -- PNG, JPEG, and the rest -- into the stream `name`, shown by
 * `<stream name="...">`. Unlike defineImage the surface is kept and written into, so a
 * feed does not build a texture per frame.
 */
export function pushFrame(name, data) {
	const bytes = Buffer.isBuffer(data) ? data : Buffer.from(data)
	if (bytes.length === 0) throw new engine.RdaError('there are no bytes to push')
	if (!engine.calls().streamPushEncoded(name, bytes, bytes.length)) {
		throw new engine.RdaError(`cannot push a frame into '${name}'`)
	}
}

/** The same, from raw pixels: four bytes each (R, G, B, A), rows tightly packed. */
export function pushFramePixels(name, pixels, width, height) {
	const bytes = Buffer.isBuffer(pixels) ? pixels : Buffer.from(pixels)
	const wanted = width * height * 4
	if (bytes.length < wanted) {
		throw new engine.RdaError(`a ${width}x${height} frame needs ${wanted} bytes, and there are ${bytes.length}`)
	}
	if (!engine.calls().streamPush(name, bytes, width, height)) {
		throw new engine.RdaError(`cannot push a frame into '${name}'`)
	}
}

/**
 * Whether anybody is looking at this stream. False for one on a screen that is not
 * showing -- ask before decoding the next frame.
 */
export function streamWanted(name) {
	return engine.calls().streamWanted(name) === 1
}

/** Drops a stream. A <stream> naming it then draws nothing. */
export function closeStream(name) {
	if (!engine.calls().streamClose(name)) {
		throw new engine.RdaError(`cannot close the stream '${name}'`)
	}
}

/**
 * Compiles a filter and keeps it under `name`. Write the filter, not the Vulkan around
 * it: `src` is the input, `store(c)` writes the result, and `uv()`, `coord()`, `size()`
 * and `param(i)` say where you are and what you were passed. Colours inside an effect are
 * linear light. Throws with the compiler's message if it will not build.
 */
export function defineEffect(name, glsl) {
	if (!engine.calls().effectDefine(name, glsl)) {
		throw new engine.RdaError(`cannot define the effect '${name}'`)
	}
}

/**
 * Runs the effect over `source` into the stream `into`, which is made if it is not there.
 *
 * `source` is one stream's name or an array of up to four; several are reachable in the
 * shader as `tap(0, at)` .. `tap(3, at)`, and `src` is the first. The output is the size
 * of the first, and the rest are sampled in 0..1. It may not be one of the sources.
 * `params` is up to eight numbers.
 */
export function applyEffect(name, source, into, params = []) {
	const names = typeof source === 'string' ? [source] : Array.from(source)
	if (names.length === 0) throw new engine.RdaError('an effect needs something to read')
	if (names.length > 4) {
		throw new engine.RdaError(
			`${names.length} sources, and a filter reads at most four. Chain two effects instead.`)
	}
	const values = params.slice(0, 8)
	const block = values.length ? new Float32Array(values) : null
	if (!engine.calls().effectApplyMany(name, names, names.length, into, block, values.length)) {
		throw new engine.RdaError(`cannot apply the effect '${name}'`)
	}
}

/**
 * The current frame of a stream, as `{pixels, width, height}`. RGBA8, rows tightly packed,
 * and as it looks on screen -- a surface an effect wrote is encoded on the way out.
 *
 * Costs a round trip to the GPU and a wait: right for saving a frame, wrong for every
 * frame.
 */
export function readFrame(name) {
	const fn = engine.calls()
	const width = new Int32Array(1)
	const height = new Int32Array(1)
	const needed = fn.streamRead(name, null, 0, width, height)
	if (needed < 0) throw new engine.RdaError(`cannot read the stream '${name}'`)
	if (needed === 0) return {pixels: Buffer.alloc(0), width: 0, height: 0}
	const pixels = Buffer.alloc(needed)
	if (fn.streamRead(name, pixels, needed, width, height) < 0) {
		throw new engine.RdaError(`cannot read the stream '${name}'`)
	}
	return {pixels, width: width[0], height: height[0]}
}

/** Drops a compiled effect. */
export function forgetEffect(name) {
	if (!engine.calls().effectForget(name)) {
		throw new engine.RdaError(`cannot forget the effect '${name}'`)
	}
}

/** Drops a registered picture. An <image> still naming it draws nothing. */
export function forgetImage(name) {
	if (!engine.calls().imageForget(name)) {
		throw new engine.RdaError(`cannot forget the image '${name}'`)
	}
}

/**
 * Every file let go over the window since the last call, as an array of paths.
 * Empty most of the time; a drop is queued rather than delivered, so nothing is lost
 * between one look and the next.
 */
export function droppedFiles() {
	const fn = engine.calls()
	const paths = []
	for (;;) {
		const buffer = Buffer.alloc(4096)
		if (fn.pollDropped(buffer, 4096) <= 0) break
		paths.push(buffer.toString('utf8', 0, buffer.indexOf(0)))
	}
	return paths
}

/**
 * Puts the keyboard on a widget, by the full id its layout gave it -- `root/composer`.
 * An empty name takes the keyboard away.
 */
export function focus(widgetId = '') {
	const fn = engine.calls()
	if (!fn.focus(widgetId)) throw new engine.RdaError(`cannot focus '${widgetId}': ${fn.lastError()}`)
}

function pick(call, args) {
	const length = call(...args, null, 0)
	if (length < 0) throw new engine.RdaError(`cannot open the chooser: ${engine.calls().lastError()}`)
	if (length === 0) return null            // cancelled, which is an answer
	const buffer = Buffer.alloc(length + 1)
	call(...args, buffer, length + 1)
	return buffer.toString('utf8', 0, length)
}

/** The platform's folder chooser. The path, or null if it was cancelled. Blocks. */
export function pickFolder(title = '', start = '') {
	return pick(engine.calls().pickFolder, [title, start])
}

/** The platform's file chooser. `filter` is `"Images|*.png;*.jpg"`; empty means any. */
export function pickFile(title = '', start = '', filter = '') {
	return pick(engine.calls().pickFile, [title, start, filter])
}

/** The window's title, after it has opened. `init({ name })` sets the first one. */
export function setTitle(text) {
	const fn = engine.calls()
	if (!fn.setTitle(String(text))) throw new engine.RdaError(`cannot set the title: ${fn.lastError()}`)
}

/**
 * The window's icon, after it has opened.
 *
 * A .png, or an .svg -- which is drawn at every size an OS picks from, so one file
 * covers the title bar, the alt-tab card and the taskbar. '' puts the platform's
 * default back. `init({ icon })` sets the first one.
 *
 * Not the executable's icon: that one is a resource inside the binary, put there when
 * the program is built rather than when it runs.
 */
export function setIcon(path = '') {
	const fn = engine.calls()
	if (!fn.setIconNow(String(path))) throw new engine.RdaError(`cannot set the icon: ${fn.lastError()}`)
}

/** Puts `text` on the OS clipboard. */
export function setClipboard(text) {
	const fn = engine.calls()
	if (!fn.clipboardSet(String(text))) throw new engine.RdaError(`cannot write the clipboard: ${fn.lastError()}`)
}

/** What is on the OS clipboard, as a string. Empty when it holds none. */
export function clipboard() {
	const fn = engine.calls()
	const length = fn.clipboardGet(null, 0)
	if (length < 0) throw new engine.RdaError(`cannot read the clipboard: ${fn.lastError()}`)
	if (length === 0) return ''
	const buffer = Buffer.alloc(length + 1)
	fn.clipboardGet(buffer, length + 1)
	return buffer.toString('utf8', 0, length)
}

/**
 * `{ width, height }` the text would be drawn at, in the pixels a layout uses.
 * A size of 0 is the interface's own.
 */
export function measureText(text, size = 0) {
	const fn = engine.calls()
	const width = [0]
	const height = [0]
	if (!fn.measureText(String(text), size, width, height)) {
		throw new engine.RdaError(`cannot measure text: ${fn.lastError()}`)
	}
	return { width: width[0], height: height[0] }
}

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
