// Finding the engine, and the calls that reach it.
//
// Everything here is koffi against the C ABI in `RendeerC.h`. koffi ships a prebuilt
// binary for each platform, so installing it downloads rather than compiles: a Node
// application needs Node, the engine binary, and `npm install`.
//
// Nothing here is public. `index.js` is the API; a generated state module calls the
// accessors below by name.

import { createRequire } from 'node:module'
import { existsSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import process from 'node:process'

// koffi is CommonJS and ships a native binary. Loaded through createRequire rather than
// a bare import so this module stays ESM without depending on a dual-package export.
const require = createRequire(import.meta.url)
const koffi = require('koffi')

// The RendeerC.h this package was written against. The major has to match the engine's
// exactly and the engine's minor has to be at least this one -- the contract is in
// RendeerC.h.
export const ABI_MAJOR = 1
export const ABI_MINOR = 6

// Laid out exactly as rda_draw_cmd. Registered once, at module load, because koffi
// interns a struct type by name and registering it twice is an error.
export const DRAW_CLEAR = 0
export const DRAW_RECT = 1
export const DRAW_LINE = 2
export const DRAW_TEXT = 3
const DrawCmd = koffi.struct('rda_draw_cmd', {
	op: 'int32_t',
	color: 'uint32_t',
	a: 'float', b: 'float', c: 'float', d: 'float',
	e: 'float',
	text: 'const char *',
})

export const NO_SIGNAL = 0xFFFFFFFF
export const NO_TABLE = 0xFFFFFFFF

export class RdaError extends Error {
	constructor(message) {
		super(message)
		this.name = 'RdaError'
	}
}

let lib = null
let fn = null
const ids = new Map()      // signal name -> id; a name lookup is a round trip
const tables = new Map()   // table name -> id, and "table\0column" -> column index

function libraryName() {
	if (process.platform === 'win32') return 'rendeer_c.dll'
	if (process.platform === 'darwin') return 'librendeer_c.dylib'
	return 'librendeer_c.so'
}

// Where to look, nearest first. RDA_ENGINE is the escape hatch and comes first: someone
// running against a build tree has the binary somewhere no convention would guess.
function* candidates() {
	const name = libraryName()
	const fromEnv = process.env.RDA_ENGINE
	if (fromEnv) yield existsSync(fromEnv) && !fromEnv.endsWith(name) ? join(fromEnv, name) : fromEnv
	const here = dirname(fileURLToPath(import.meta.url))
	yield join(here, name)                          // beside the package
	// The checkout's bin/, where the build stages the engine: this file is
	// bindings/node/engine.js, two levels below the root. npm links a path dependency
	// rather than copying it, so import.meta.url is the real path and this resolves.
	yield join(here, '..', '..', 'bin', name)
	yield join(process.cwd(), name)                 // beside whatever is running
	yield name                                      // and whatever the loader finds
}

/** Loads the engine. Called for you by init(); call it early to fail early. */
export function load(path) {
	if (lib) return lib

	const tried = []
	for (const candidate of path ? [path] : candidates()) {
		try {
			lib = koffi.load(candidate)
			break
		} catch {
			tried.push(candidate)
		}
	}
	if (!lib) {
		throw new RdaError(
			`cannot find the engine (${libraryName()}). Set RDA_ENGINE to the file or the ` +
			`directory holding it. Looked in: ${tried.join(', ')}`)
	}
	checkAbi()
	declare()
	return lib
}

// Refuses an engine this package cannot talk to, before it declares anything else. The
// package and the engine are installed separately -- npm links this from the checkout and
// the shared library is rebuilt underneath it -- so a mismatch is ordinary rather than odd.
function checkAbi() {
	const wanted = `${ABI_MAJOR}.${ABI_MINOR}`
	let major, minor
	try {
		major = lib.func('int rda_abi_major()')()
		minor = lib.func('int rda_abi_minor()')()
	} catch {
		throw new RdaError(
			`this engine predates the ABI version check, so it is older than this package ` +
			`(which needs ${wanted}). Rebuild the engine: scripts/windows-bringup.bat or ` +
			`scripts/linux-bringup.sh in the checkout.`)
	}

	const found = `${major}.${minor}`
	if (major !== ABI_MAJOR) {
		// A different major in either direction: the engine has either removed something
		// this package calls or never had it. Which way it went decides which half moves.
		const which = major < ABI_MAJOR
			? 'The engine is behind the package: rebuild it, or use the package from the same checkout'
			: 'The package is behind the engine: npm install it again from this checkout'
		throw new RdaError(
			`the engine speaks ABI ${found} and this package speaks ${wanted}. A different ` +
			`major number is a different ABI, not an older one, so nothing here will work. ` +
			`${which}.`)
	}
	if (minor < ABI_MINOR) {
		throw new RdaError(
			`the engine offers ABI ${found} and this package needs ${wanted}. Same major, so ` +
			`nothing was removed -- the engine is simply missing what was added since: ` +
			`rebuild it, or use the package from the same checkout.`)
	}
}

// Prototypes, so koffi marshals rather than guesses. Written as C declarations because
// that is what they are -- the header is the specification and this is a transcription
// of it, which is the shape that makes the two easy to check against each other.
function declare() {
	const f = (proto) => lib.func(proto)
	fn = {
		lastError: f('const char *rda_last_error()'),

		configNew: f('void *rda_config_new()'),
		configFree: f('void rda_config_free(void *config)'),
		setName: f('void rda_config_set_name(void *config, const char *name)'),
		setTheme: f('void rda_config_set_theme(void *config, const char *path)'),
		setLanguages: f('void rda_config_set_languages(void *config, const char *path)'),
		setVsync: f('void rda_config_set_vsync(void *config, int on)'),
		setFont: f('void rda_config_set_font(void *config, const char *path, float height)'),

		init: f('int rda_init(void *config)'),
		running: f('int rda_running()'),
		stop: f('void rda_stop()'),
		wait: f('void rda_wait()'),

		loadInterface: f('int rda_load_interface(const char *blueprint, const char *source)'),
		openRoutes: f('int rda_open_routes(const char **names, const char **layouts, const char **params, int count, const char *layoutDir, const char *sourceDir)'),
		setTransitionMs: f('void rda_set_transition_ms(float ms)'),
		routeBack: f('int rda_route_back()'),
		routeForward: f('int rda_route_forward()'),
		canGoBack: f('int rda_route_can_go_back()'),
		canGoForward: f('int rda_route_can_go_forward()'),

		defineNumber: f('uint32_t rda_define_number(const char *name, double value)'),
		defineBool: f('uint32_t rda_define_bool(const char *name, int value)'),
		defineText: f('uint32_t rda_define_text(const char *name, const char *value)'),
		defineCommand: f('int rda_define_command(const char *name)'),
		defineTable: f('int rda_define_table(const char *name)'),
		defineColumn: f('int rda_define_column(const char *table, const char *column, int type)'),

		signalFind: f('uint32_t rda_signal_find(const char *name)'),
		signalType: f('int rda_signal_type(uint32_t signal)'),
		getNumber: f('int rda_signal_get_number(uint32_t signal, _Out_ double *out)'),
		setNumber: f('int rda_signal_set_number(uint32_t signal, double value)'),
		getBool: f('int rda_signal_get_bool(uint32_t signal, _Out_ int *out)'),
		setBool: f('int rda_signal_set_bool(uint32_t signal, int value)'),
		getText: f('int rda_signal_get_text(uint32_t signal, _Out_ char *buffer, int capacity)'),
		setText: f('int rda_signal_set_text(uint32_t signal, const char *value)'),

		tableFind: f('uint32_t rda_table_find(const char *name)'),
		tableColumn: f('int rda_table_column(uint32_t table, const char *column)'),
		tableRows: f('int rda_table_rows(uint32_t table)'),
		tableResize: f('int rda_table_resize(uint32_t table, int rows)'),
		setNumbers: f('int rda_table_set_numbers(uint32_t table, int column, int first, double *values, int count)'),
		setBools: f('int rda_table_set_bools(uint32_t table, int column, int first, int *values, int count)'),
		setTexts: f('int rda_table_set_texts(uint32_t table, int column, int first, const char **values, int count)'),
		getCellNumber: f('int rda_table_get_number(uint32_t table, int column, int row, _Out_ double *out)'),
		getCellBool: f('int rda_table_get_bool(uint32_t table, int column, int row, _Out_ int *out)'),
		getCellText: f('int rda_table_get_text(uint32_t table, int column, int row, _Out_ char *buffer, int capacity)'),

		setSize: f('void rda_config_set_size(void *config, int width, int height)'),
		setIcon: f('void rda_config_set_icon(void *config, const char *path)'),
		setDecorated: f('void rda_config_set_decorated(void *config, int on)'),
		setResizable: f('void rda_config_set_resizable(void *config, int on)'),
		setMaximized: f('void rda_config_set_maximized(void *config, int on)'),
		setFullscreen: f('void rda_config_set_fullscreen(void *config, int on)'),
		setAlwaysOnTop: f('void rda_config_set_always_on_top(void *config, int on)'),
		setTransparent: f('void rda_config_set_transparent(void *config, int on)'),
		setOpacity: f('void rda_config_set_opacity(void *config, float value)'),
		setSizeLimits: f('void rda_config_set_size_limits(void *config, int min_width, int min_height, int max_width, int max_height)'),
		setPosition: f('void rda_config_set_position(void *config, int x, int y)'),
		setTitle: f('int rda_set_title(const char *title)'),
		setIconNow: f('int rda_set_icon(const char *path)'),
		focus: f('int rda_focus(const char *id)'),
		pollDropped: f('int rda_poll_dropped_file(_Out_ char *buffer, int capacity)'),
		imageDefine: f('int rda_image_define(const char *name, void *bytes, int size)'),
		imageDefinePixels: f('int rda_image_define_pixels(const char *name, void *pixels, int width, int height)'),
		imageForget: f('int rda_image_forget(const char *name)'),
		streamPush: f('int rda_stream_push(const char *name, void *pixels, int width, int height)'),
		streamPushEncoded: f('int rda_stream_push_encoded(const char *name, void *bytes, int size)'),
		streamWanted: f('int rda_stream_wanted(const char *name)'),
		streamClose: f('int rda_stream_close(const char *name)'),
		effectDefine: f('int rda_effect_define(const char *name, const char *glsl)'),
		effectApply: f('int rda_effect_apply(const char *name, const char *source, const char *into, float *params, int count)'),
		effectForget: f('int rda_effect_forget(const char *name)'),
		effectApplyMany: f('int rda_effect_apply_many(const char *name, char **sources, int sourceCount, const char *into, float *params, int paramCount)'),
		streamRead: f('int rda_stream_read(const char *name, _Out_ uint8_t *buffer, int capacity, _Out_ int *width, _Out_ int *height)'),
		pickFolder: f('int rda_pick_folder(const char *title, const char *start, _Out_ char *buffer, int capacity)'),
		pickFile: f('int rda_pick_file(const char *title, const char *start, const char *filter, _Out_ char *buffer, int capacity)'),
		clipboardSet: f('int rda_clipboard_set(const char *text)'),
		clipboardGet: f('int rda_clipboard_get(_Out_ char *buffer, int capacity)'),
		measureText: f('int rda_measure_text(const char *text, float size, _Out_ float *w, _Out_ float *h)'),
		setTheme: f('int rda_set_theme(const char *path)'),
		viewportDraw: f('int rda_viewport_draw(const char *name, rda_draw_cmd *commands, int count)'),
		viewportSize: f('int rda_viewport_size(const char *name, _Out_ float *width, _Out_ float *height)'),

		commandInvoke: f('int rda_command_invoke(const char *name)'),
		commandWatch: f('int rda_command_watch(const char *name)'),
		pollCommand: f('int rda_poll_command(_Out_ char *buffer, int capacity)'),
	}
}

function fail(what) {
	const message = fn ? fn.lastError() : ''
	throw new RdaError(message ? `${what}: ${message}` : what)
}

// ---- configuration, used by index.js -------------------------------------------------
export function calls() {
	if (!fn) load()
	return fn
}

// ---- signals -------------------------------------------------------------------------

/** The id of a declared signal, looked up once and remembered. */
function signal(name) {
	const found = ids.get(name)
	if (found !== undefined) return found
	load()
	const id = fn.signalFind(name)
	if (id === NO_SIGNAL) fail(`no signal called '${name}'`)
	ids.set(name, id)
	return id
}

const out = [0]  // one scratch cell; every accessor below is synchronous

export function getNumber(name) {
	if (!fn.getNumber(signal(name), out)) fail(`cannot read '${name}'`)
	return out[0]
}

export function setNumber(name, value) {
	if (!fn.setNumber(signal(name), Number(value))) fail(`cannot write '${name}'`)
}

export function getBool(name) {
	if (!fn.getBool(signal(name), out)) fail(`cannot read '${name}'`)
	return out[0] !== 0
}

export function setBool(name, value) {
	if (!fn.setBool(signal(name), value ? 1 : 0)) fail(`cannot write '${name}'`)
}

// A guess, and a retry when it was too small. The engine reports what the text *is*
// rather than what fitted, so guessing wrong costs one more call and never a truncation
// nobody notices -- and guessing right, which is almost always, costs one call instead
// of the two a length-first protocol would always pay.
const kTextGuess = 256

export function getText(name) {
	const id = signal(name)
	let buffer = Buffer.allocUnsafe(kTextGuess)
	let length = fn.getText(id, buffer, buffer.length)
	if (length < 0) fail(`cannot read '${name}'`)
	if (length >= buffer.length) {
		buffer = Buffer.allocUnsafe(length + 1)
		length = fn.getText(id, buffer, buffer.length)
		if (length < 0) fail(`cannot read '${name}'`)
	}
	return buffer.toString('utf8', 0, length)
}

export function setText(name, value) {
	if (!fn.setText(signal(name), String(value))) fail(`cannot write '${name}'`)
}

// ---- declaring, used by a generated state module -------------------------------------

export function defineNumber(name, value) {
	load()
	fn.defineNumber(name, Number(value))
}

export function defineBool(name, value) {
	load()
	fn.defineBool(name, value ? 1 : 0)
}

export function defineText(name, value) {
	load()
	fn.defineText(name, String(value))
}

export function defineCommand(name) {
	load()
	if (!fn.defineCommand(name)) fail(`cannot define the command '${name}'`)
}

export function defineTable(name) {
	load()
	if (!fn.defineTable(name)) fail(`cannot define the table '${name}'`)
}

export function defineColumn(table, column, type) {
	load()
	if (!fn.defineColumn(table, column, type)) fail(`cannot define ${table}.${column}`)
}

// ---- tables --------------------------------------------------------------------------
//
// Written a column at a time rather than a cell at a time. Every call here crosses to the
// engine's loop thread and waits, so filling ten thousand rows of three columns is three
// round trips this way and thirty thousand the other.

function tableId(name) {
	const found = tables.get(name)
	if (found !== undefined) return found
	load()
	const id = fn.tableFind(name)
	if (id === NO_TABLE) fail(`no table called '${name}'`)
	tables.set(name, id)
	return id
}

function columnIndex(table, column) {
	const key = `${table} ${column}`
	const found = tables.get(key)
	if (found !== undefined) return found
	const index = fn.tableColumn(tableId(table), column)
	if (index < 0) fail(`no column '${column}' in '${table}'`)
	tables.set(key, index)
	return index
}

export function tableRows(table) {
	const rows = fn.tableRows(tableId(table))
	if (rows < 0) fail(`cannot read the size of '${table}'`)
	return rows
}

export function tableResize(table, rows) {
	if (!fn.tableResize(tableId(table), Math.max(0, Math.trunc(rows)))) {
		fail(`cannot resize '${table}'`)
	}
}

export function setNumbers(table, column, values, first = 0) {
	if (!values.length) return
	const block = Float64Array.from(values, Number)
	if (!fn.setNumbers(tableId(table), columnIndex(table, column), first, block, block.length)) {
		fail(`cannot write ${table}.${column}`)
	}
}

export function setBools(table, column, values, first = 0) {
	if (!values.length) return
	const block = Int32Array.from(values, (v) => (v ? 1 : 0))
	if (!fn.setBools(tableId(table), columnIndex(table, column), first, block, block.length)) {
		fail(`cannot write ${table}.${column}`)
	}
}

export function setTexts(table, column, values, first = 0) {
	if (!values.length) return
	const block = values.map((v) => (v === null || v === undefined ? '' : String(v)))
	if (!fn.setTexts(tableId(table), columnIndex(table, column), first, block, block.length)) {
		fail(`cannot write ${table}.${column}`)
	}
}

export function getCellNumber(table, column, row) {
	if (!fn.getCellNumber(tableId(table), columnIndex(table, column), row, out)) {
		fail(`cannot read ${table}.${column}[${row}]`)
	}
	return out[0]
}

export function getCellBool(table, column, row) {
	if (!fn.getCellBool(tableId(table), columnIndex(table, column), row, out)) {
		fail(`cannot read ${table}.${column}[${row}]`)
	}
	return out[0] !== 0
}

export function getCellText(table, column, row) {
	const id = tableId(table)
	const index = columnIndex(table, column)
	let buffer = Buffer.allocUnsafe(kTextGuess)
	let length = fn.getCellText(id, index, row, buffer, buffer.length)
	if (length < 0) fail(`cannot read ${table}.${column}[${row}]`)
	if (length >= buffer.length) {
		buffer = Buffer.allocUnsafe(length + 1)
		length = fn.getCellText(id, index, row, buffer, buffer.length)
		if (length < 0) fail(`cannot read ${table}.${column}[${row}]`)
	}
	return buffer.toString('utf8', 0, length)
}

/**
 * One field of a row, however the caller chose to spell a row.
 *
 * A generated fill() takes whatever is natural to the application: an array of objects
 * straight out of a query, or of arrays in column order. Deciding that here rather than
 * making every application convert first is the whole reason this exists.
 */
export function cell(row, name, index) {
	return Array.isArray(row) ? row[index] : row[name]
}

// ---- commands ------------------------------------------------------------------------
//
// Not a callback. The engine calls a bound callback on *its* thread, and a JavaScript
// value may only be touched on the thread that owns it -- an FFI trampoline invoked from
// the loop thread does not crash, it deadlocks: the engine's thread blocks trying to
// reach the JS thread, which is itself blocked inside the call that is waiting for the
// engine. Measured, not assumed.
//
// So the engine records that a command fired and this collects it, on Node's own loop,
// where running a handler is legal.

const handlers = new Map()

/** Registers `fn` for a declared command. The engine is told to watch the name. */
export function onCommand(name, handler) {
	if (typeof handler !== 'function') {
		throw new RdaError(`a handler for '${name}' has to be a function`)
	}
	load()
	handlers.set(name, handler)
	if (!fn.commandWatch(name)) fail(`cannot watch '${name}'`)
	return handler
}

/**
 * Asks for a command as if the interface had.
 *
 * For when the backend is the one that wants the work: a menu, a hotkey, a scheduled job.
 * A watched command is queued by this exactly as a click would queue it, so the handler
 * still runs on Node's loop and cannot tell who asked.
 */
export function invoke(name) {
	load()
	if (!fn.commandInvoke(name)) fail(`cannot invoke '${name}'`)
}

const pollBuffer = Buffer.allocUnsafe(256)

/**
 * Runs the handlers for every command that has fired since the last call.
 *
 * Returns how many ran. Called for you by `run()`; call it yourself from your own loop
 * if you would rather own the timing.
 */
export function poll() {
	if (!fn) return 0
	let ran = 0
	// Bounded, so a storm of commands cannot hold the event loop for an unbounded time.
	// Whatever is left is collected on the next turn, which is a millisecond away.
	for (let i = 0; i < 64; ++i) {
		if (fn.pollCommand(pollBuffer, pollBuffer.length) !== 1) break
		const end = pollBuffer.indexOf(0)
		const name = pollBuffer.toString('utf8', 0, end < 0 ? pollBuffer.length : end)
		const handler = handlers.get(name)
		if (handler) {
			++ran
			// A throwing handler must not stop the ones behind it, or stop polling.
			try {
				handler()
			} catch (problem) {
				queueMicrotask(() => { throw problem })
			}
		}
	}
	return ran
}
