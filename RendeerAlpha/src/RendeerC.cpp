#include <RendeerC.h>

#include <RendeerAlpha.h>
#include <Core/Commands.h>
#include <Core/FileDialog.h>
#include <Core/LoopWork.h>
#include <Core/Signals.h>
#include <Core/Tables.h>
#include <GraphicalObjects/Gui.h>
#include <GraphicalObjects/Viewports.h>
#include <GraphicalObjects/Images.h>
#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Effects.h>
#include <GraphicalObjects/Window.h>
#include <Layout/LayoutHost.h>
#include <Layout/Router.h>
#include <Logger/Logger.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The C ABI's implementation. Thin on purpose: every function here is a name lookup, a
// hop to the loop thread, and a call into the same C++ an application would have made.
//
// The hop is the whole substance of this file. Signals are a vector of strings and
// observer lists -- writing one from another thread while the loop reads it is a data
// race with a reallocating std::string in it, which is the kind that corrupts rather
// than the kind that returns the wrong number. So nothing here touches engine state on
// the caller's thread; it all goes through LoopWork, which the engine already had for
// exactly this reason.
namespace {

	// One message per thread, so two threads failing at once do not overwrite each
	// other's explanation before either has read it.
	thread_local std::string tLastError;

	bool fail(const char* what) {
		tLastError = what;
		return false;
	}

	// Runs `work` on the loop thread and waits for it.
	//
	// The wake matters as much as the queue. With on-demand redraw the loop can be
	// parked in the event pump with no frame due for seconds; without a nudge the
	// request would sit in the queue until something else happened, and a backend
	// setting a value would look like a hang. glfwPostEmptyEvent is safe from any
	// thread, which is what rendeerRequestRedraw() does with it.
	//
	// Called from the loop thread itself -- from inside a command callback, say --
	// LoopWork::request runs the work inline, so this costs nothing there.
	bool onLoop(const std::function<void()>& work) {
		if (!rendeerRunning()) return fail("the engine is not running");
		rendeerRequestRedraw();
		if (!RDA::loopWork().request(work)) {
			return fail("the loop did not service the request; it may be shutting down");
		}
		return true;
	}

	// Where a loaded interface lives. One host, replaced whenever a new interface is
	// loaded -- the previous tree goes with it, which is what makes loading twice mean
	// "show this instead" rather than "show both".
	RDA::Layout::LayoutHost& host() {
		static RDA::Layout::LayoutHost sHost;
		return sHost;
	}

	// The other way to show an interface: a set of screens with one showing. Kept beside
	// the host rather than inside it because they are alternatives -- whichever was asked
	// for last is the one the frame update drives, and the other is torn down.
	RDA::Layout::Router& router() {
		static RDA::Layout::Router sRouter;
		return sRouter;
	}

	// Touched only on the loop thread: set by rda_open_routes and rda_load_interface,
	// which both run their work there, and read by the frame update, which is there too.
	bool gRoutesOpen = false;

	// Whether anything ever asked for an interface. Unlike C++, a backend reaching the
	// engine through this ABI has exactly two ways to put something on screen, so a
	// start-up that called neither is a mistake rather than a choice -- and the way it
	// shows is an empty window with a completely clean log, which is the least helpful
	// thing this could do to somebody's first afternoon. Atomic because the call may
	// come from any thread while the loop thread reads it after on_start.
	std::atomic<bool> gInterfaceAsked{ false };
	// Set the moment the engine tells the caller it is up, which is when an application
	// that loads its interface *after* rda_init gets its turn. Read by the frame update.
	std::atomic<bool> gHandedOver{ false };
	// Both touched only by the frame update, which is the loop thread.
	std::chrono::steady_clock::time_point gEmptySince{};
	bool gWarnedEmpty = false;

	std::atomic<bool> gStarted{ false };

	// How rda_init knows the engine is actually up.
	//
	// rendeerRun in Owned mode spawns the thread and returns at once, so without this a
	// caller that set a signal on the next line would be racing the device, the window
	// and the font atlas -- and would usually lose, because the state it is writing to
	// does not exist until on_start has declared it. Making every backend hand-roll a
	// handshake for that is making every backend get it wrong once.
	std::mutex              gReadyLock;
	std::condition_variable gReady;
	bool                    gReadyFlag = false;

	// How long one screen takes to cross-fade into the next. Set from any thread, read
	// by the loop.
	std::atomic<float> gTransitionMs{ 0.0f };

	// Commands that fired and have not been collected yet.
	//
	// Written on the loop thread by the handler rda_command_watch binds, read on whatever
	// thread polls -- so it is the one piece of state here with a lock of its own rather
	// than a hop to the loop. That is the whole point: a backend polling its own queue
	// must not cost a frame per look.
	//
	// Bounded, because a backend that stops polling must not turn a held-down button into
	// unbounded memory. Dropping the oldest keeps the most recent intent, which is the
	// half worth keeping.
	constexpr size_t kMaxPending = 256;
	std::mutex               gQueueLock;
	std::vector<std::string> gPending;

	// Paths dropped on the window, waiting to be asked for. Its own lock: a drop arrives
	// on the loop thread while a backend may be draining from anywhere, and the command
	// queue's lock has nothing to do with it.
	std::mutex               gDropLock;
	std::vector<std::string> gDropped;
	bool                     gWarnedDropsLost = false;
	bool                     gWarnedOverflow = false;

	void markReady() {
		gHandedOver.store(true);
		{
			std::lock_guard<std::mutex> held(gReadyLock);
			gReadyFlag = true;
		}
		gReady.notify_all();
	}
}

struct rda_config {
	RDA::AppConfig       config;
	rda_callback         onStart = nullptr;
	void*                onStartUser = nullptr;
	rda_update_callback  onUpdate = nullptr;
	void*                onUpdateUser = nullptr;
	rda_callback         onShutdown = nullptr;
	void*                onShutdownUser = nullptr;
};

extern "C" {

const char* rda_last_error(void) { return tLastError.c_str(); }

// ---- configuration ---------------------------------------------------------------
rda_config* rda_config_new(void) {
	auto* handle = new (std::nothrow) rda_config();
	if (!handle) return nullptr;
	handle->config.gui.enabled = true;
	handle->config.threadMode = RDA::ThreadMode::Owned; // this ABI always spawns
	return handle;
}

void rda_config_free(rda_config* config) { delete config; }

void rda_config_set_name(rda_config* config, const char* name) {
	if (config && name) config->config.app.name = name;
}
void rda_config_set_theme(rda_config* config, const char* path) {
	if (config && path) config->config.gui.themePath = path;
}
void rda_config_set_languages(rda_config* config, const char* path) {
	if (config && path) config->config.gui.languagesPath = path;
}
void rda_config_set_vsync(rda_config* config, int on) {
	if (config) config->config.vsync = on != 0;
}

void rda_config_set_size(rda_config* config, int width, int height) {
	if (!config) return;
	// Negatives are the same answer as zero -- the engine's default -- rather than an
	// error: this is a hint, and refusing one is worse than ignoring it.
	config->config.windowWidth = width > 0 ? static_cast<uint32_t>(width) : 0u;
	config->config.windowHeight = height > 0 ? static_cast<uint32_t>(height) : 0u;
}
void rda_config_set_icon(rda_config* config, const char* path) {
	if (config) config->config.window.icon = path ? path : "";
}
void rda_config_set_decorated(rda_config* config, int on) {
	if (config) config->config.window.decorated = on != 0;
}
void rda_config_set_resizable(rda_config* config, int on) {
	if (config) config->config.window.resizable = on != 0;
}
void rda_config_set_maximized(rda_config* config, int on) {
	if (config) config->config.window.maximized = on != 0;
}
void rda_config_set_fullscreen(rda_config* config, int on) {
	if (config) config->config.window.fullscreen = on != 0;
}
void rda_config_set_always_on_top(rda_config* config, int on) {
	if (config) config->config.window.alwaysOnTop = on != 0;
}
void rda_config_set_transparent(rda_config* config, int on) {
	if (config) config->config.window.transparent = on != 0;
}
void rda_config_set_opacity(rda_config* config, float value) {
	if (config) config->config.window.opacity = value;
}
void rda_config_set_size_limits(rda_config* config, int min_width, int min_height,
                                int max_width, int max_height) {
	if (!config) return;
	// Negatives read as zero -- no bound -- rather than as an error, the same answer
	// rda_config_set_size gives a negative size.
	RDA::WindowStyle& window = config->config.window;
	window.minWidth  = min_width  > 0 ? static_cast<unsigned>(min_width)  : 0u;
	window.minHeight = min_height > 0 ? static_cast<unsigned>(min_height) : 0u;
	window.maxWidth  = max_width  > 0 ? static_cast<unsigned>(max_width)  : 0u;
	window.maxHeight = max_height > 0 ? static_cast<unsigned>(max_height) : 0u;
}
void rda_config_set_position(rda_config* config, int x, int y) {
	if (!config) return;
	config->config.window.x = x;
	config->config.window.y = y;
}

void rda_config_set_font(rda_config* config, const char* path, float height) {
	if (!config) return;
	if (path && *path) config->config.gui.fontPath = path;
	if (height > 0.0f) config->config.gui.fontHeight = height;
}
void rda_config_on_start(rda_config* config, rda_callback fn, void* user) {
	if (config) { config->onStart = fn; config->onStartUser = user; }
}
void rda_config_on_update(rda_config* config, rda_update_callback fn, void* user) {
	if (config) { config->onUpdate = fn; config->onUpdateUser = user; }
}
void rda_config_on_shutdown(rda_config* config, rda_callback fn, void* user) {
	if (config) { config->onShutdown = fn; config->onShutdownUser = user; }
}

// ---- lifecycle -------------------------------------------------------------------
// ---- which ABI this is ---------------------------------------------------------------
//
// Deliberately the plainest functions in the file: no state, no loop hop, nothing that
// could fail. A binding calls them before it has decided whether it can talk to this
// engine at all, so they have to work even when nothing else here would.
int rda_abi_major(void) { return RDA_ABI_MAJOR; }

// Raised when something is added and nothing else moves, which is what makes every
// package built against an earlier minor of this major keep working.
int rda_abi_minor(void) { return RDA_ABI_MINOR; }

int rda_init(rda_config* handle) {
	if (!handle) return fail("no config");
	if (gStarted.exchange(true)) {
		gStarted.store(true);
		return fail("the engine is already running; call rda_wait() before starting again");
	}

	// Said once, in the log the backend is told to read. A binding refuses a mismatch
	// before it gets here, so this is not the check -- it is what makes a report of some
	// later oddity answerable without asking which two halves were in the room.
	RDA_LOG_INFO("C ABI " << RDA_ABI_MAJOR << "." << RDA_ABI_MINOR);

	// Copied, because the caller may free its handle the moment this returns and the
	// callbacks outlive it by the length of the program.
	// Every drop is remembered whether or not anybody is asking yet: the files land in
	// one frame and a backend polls on its own schedule, so dropping them on the floor
	// because nothing had asked in that millisecond would lose what somebody just did.
	handle->config.input.onFilesDropped = [](const std::vector<std::string>& paths) {
		std::lock_guard<std::mutex> held(gDropLock);
		for (const std::string& path : paths) {
			if (gDropped.size() >= kMaxPending) {
				if (!gWarnedDropsLost) {
					gWarnedDropsLost = true;
					RDA_LOG_WARNING("dropped files are piling up unread; the oldest are being "
					                "discarded. Call rda_poll_dropped_file after a drop.");
				}
				gDropped.erase(gDropped.begin());
			}
			gDropped.push_back(path);
		}
		rendeerRequestRedraw();
	};

	RDA::AppConfig config = handle->config;
	const rda_callback        onStart = handle->onStart;
	void* const               onStartUser = handle->onStartUser;
	const rda_update_callback onUpdate = handle->onUpdate;
	void* const               onUpdateUser = handle->onUpdateUser;
	const rda_callback        onShutdown = handle->onShutdown;
	void* const               onShutdownUser = handle->onShutdownUser;

	// Always wrapped, whether or not the caller wanted a callback: this is also where
	// "the engine is up" is announced, and that has to happen either way.
	config.onStart = [onStart, onStartUser] {
		if (onStart) onStart(onStartUser);
		markReady();
	};
	config.onUpdate = [onUpdate, onUpdateUser](float dt) {
		// An empty window with a clean log is the least helpful thing this can do to
		// somebody's first afternoon, so it says so -- but not before the application
		// has had a chance. A backend reaching the engine through this ABI has exactly
		// two ways to put anything on screen, and it may use either from on_start (as
		// Python does) or on the line after rda_init returns (as Node and C# do). Two
		// seconds is long past both and still while somebody is looking at the window.
		if (gHandedOver.load() && !gInterfaceAsked.load() && !gWarnedEmpty) {
			// Timed from the first frame after the hand-over rather than from the start
			// of the loop. Bringing up a device, a window and a font atlas takes a
			// couple of seconds on its own, and every one of them is time the
			// application has not had yet -- measured at 2.1s here, which a grace
			// period counted from the loop would have spent before the caller's next
			// line ran.
			const auto now = std::chrono::steady_clock::now();
			if (gEmptySince == std::chrono::steady_clock::time_point{}) {
				gEmptySince = now;
			} else if (now - gEmptySince > std::chrono::seconds(2)) {
				gWarnedEmpty = true;
				RDA_LOG_WARNING(
					"nothing has loaded an interface, so this window is empty. Call "
					"rda_load_interface(\"res/layouts/<name>.rdab\") for one screen, or "
					"rda_open_routes(ROUTES, \"res/layouts\") for a set of them -- in "
					"Python rda.load_interface(...) / rda.open_routes(...), in Node "
					"rda.loadInterface(...) / rda.openRoutes(...), in C# "
					"Rda.LoadInterface(...) / Rda.OpenRoutes(...). Declaring the state "
					"is not enough on its own.");
			}
		}

		// The interface is reloaded here for the same reason a C++ application does it
		// here: swapping a tree from inside a widget callback tears down the walk that
		// callback is in. Router::update() is the same call for a set of screens -- it
		// swaps when `route` has moved and reloads when a file has.
		if (gRoutesOpen) {
			router().setTransitionMs(gTransitionMs.load());
			router().update();
		} else {
			host().reloadIfChanged();
		}
		if (onUpdate) onUpdate(dt, onUpdateUser);
	};
	config.onShutdown = [onShutdown, onShutdownUser] {
		// A start-up that failed, or an on_start that called rda_stop, ends the loop
		// without anyone ever becoming ready. Released here so rda_init returns and
		// reports it rather than waiting out its timeout.
		markReady();
		if (onShutdown) onShutdown(onShutdownUser);
		// Before the loop tears down, so the bindings the screens registered are gone
		// while the signal table they registered with is still there.
		if (gRoutesOpen) {
			router() = RDA::Layout::Router{};
			gRoutesOpen = false;
		}
		host() = RDA::Layout::LayoutHost{};
	};

	gInterfaceAsked.store(false);
	gHandedOver.store(false);
	gEmptySince = {};
	gWarnedEmpty = false;
	{
		std::lock_guard<std::mutex> held(gReadyLock);
		gReadyFlag = false;
	}
	{
		// Whatever the last run left uncollected belongs to the last run.
		std::lock_guard<std::mutex> held(gQueueLock);
		gPending.clear();
		gWarnedOverflow = false;
	}
	rendeerRun(config); // ThreadMode::Owned, so this returns once the thread is spawned

	// And then wait for the engine to actually be up. The timeout is not a guess about
	// how long start-up takes -- it is the only way out if bring-up dies without
	// reaching either callback, which is what a missing device or a missing font does.
	{
		std::unique_lock<std::mutex> held(gReadyLock);
		if (!gReady.wait_for(held, std::chrono::seconds(30), [] { return gReadyFlag; })) {
			held.unlock();
			rendeerStop();
			rendeerWait();
			gStarted.store(false);
			return fail("the engine did not start; see the log");
		}
	}
	// on_start may itself have stopped the engine -- a headless check that did its work
	// there, or a start-up that found a bad argument. Saying it started would be a lie
	// the next call would discover.
	if (!rendeerRunning()) {
		rendeerWait();
		gStarted.store(false);
		return fail("the engine stopped during start-up; see the log");
	}
	return true;
}

int rda_running(void) { return rendeerRunning() ? 1 : 0; }

void rda_stop(void) { rendeerStop(); }

void rda_wait(void) {
	rendeerWait();
	gStarted.store(false);
}

// ---- the interface ---------------------------------------------------------------
int rda_load_interface(const char* blueprintPath, const char* sourcePath) {
	gInterfaceAsked.store(true);
	if (!blueprintPath || !*blueprintPath) return fail("no blueprint path");
	const std::string blueprint = blueprintPath;
	const std::string source = sourcePath ? sourcePath : "";

	bool opened = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window) return;
		// Whatever was showing goes first, whichever of the two put it there.
		if (gRoutesOpen) {
			router() = RDA::Layout::Router{};
			gRoutesOpen = false;
		}
		host() = RDA::Layout::LayoutHost{};
		opened = host().open(window->gui().retained(), blueprint, source);
		if (opened) rendeerInterfaceChanged();
	})) {
		return false;
	}
	if (!opened) return fail(host().lastError().empty() ? "the interface did not load"
	                                                    : host().lastError().c_str());
	return true;
}

// ---- screens ---------------------------------------------------------------------
int rda_open_routes(const char* const* names, const char* const* layouts,
                    const char* const* params, int count,
                    const char* layoutDir, const char* sourceDir) {
	gInterfaceAsked.store(true);
	if (!names || !layouts || count <= 0) return fail("no routes");
	if (!layoutDir || !*layoutDir) return fail("no layout directory");

	// Copied out of the caller's arrays here, on the caller's thread, because the work
	// below runs on another one and the caller is free to release them the moment this
	// returns.
	std::vector<RDA::Layout::Router::Route> routes;
	routes.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		if (!names[i] || !layouts[i]) return fail("a route with no name or no layout");
		RDA::Layout::Router::Route route;
		route.name = names[i];
		route.layout = layouts[i];
		// "productId,page" -- one string rather than an array of arrays, because a
		// parameter is an identifier and there is nothing in one to escape.
		if (params && params[i] && *params[i]) {
			const std::string list = params[i];
			size_t at = 0;
			while (at <= list.size()) {
				const size_t comma = list.find(',', at);
				const size_t end = comma == std::string::npos ? list.size() : comma;
				std::string one = list.substr(at, end - at);
				// Tolerant of "a, b": a space around a comma is a typo, not a name.
				while (!one.empty() && one.front() == ' ') one.erase(one.begin());
				while (!one.empty() && one.back() == ' ') one.pop_back();
				if (!one.empty()) route.params.push_back(std::move(one));
				if (comma == std::string::npos) break;
				at = comma + 1;
			}
		}
		routes.push_back(std::move(route));
	}
	const std::string directory = layoutDir;
	const std::string sources = sourceDir ? sourceDir : "";

	bool opened = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window) return;
		host() = RDA::Layout::LayoutHost{}; // a single interface, if one was showing
		// Assigned rather than opened over the top: a router that has already opened
		// holds a screen under the parent, and opening again would leave it there.
		router() = RDA::Layout::Router{};
		router().setTransitionMs(gTransitionMs.load());
		gRoutesOpen = router().open(window->gui().retained(), std::move(routes),
		                            directory, sources);
		opened = gRoutesOpen;
	})) {
		return false;
	}
	if (!opened) return fail(router().lastError().empty() ? "no screen opened"
	                                                      : router().lastError().c_str());
	return true;
}

void rda_set_transition_ms(float ms) {
	// Held rather than written through: the router reads it on the loop thread, and a
	// float written from another one is a race however harmless it looks. Applied by the
	// frame update, which also means this may be called before there is an engine.
	gTransitionMs.store(ms < 0.0f ? 0.0f : ms);
	rendeerRequestRedraw();
}

int rda_route_back(void) {
	bool ok = false;
	if (!onLoop([&] { ok = gRoutesOpen && router().back(); })) return false;
	return ok ? 1 : fail("there is nowhere back to go");
}

int rda_route_forward(void) {
	bool ok = false;
	if (!onLoop([&] { ok = gRoutesOpen && router().forward(); })) return false;
	return ok ? 1 : fail("there is nowhere forward to go");
}

int rda_route_can_go_back(void) {
	bool ok = false;
	if (!onLoop([&] { ok = gRoutesOpen && router().canGoBack(); })) return 0;
	return ok ? 1 : 0;
}

int rda_route_can_go_forward(void) {
	bool ok = false;
	if (!onLoop([&] { ok = gRoutesOpen && router().canGoForward(); })) return 0;
	return ok ? 1 : 0;
}

// ---- declaring what the interface reads -------------------------------------------
uint32_t rda_define_number(const char* name, double value) {
	if (!name) return RDA_NO_SIGNAL;
	const std::string wanted = name;
	uint32_t id = RDA_NO_SIGNAL;
	if (!onLoop([&] { id = RDA::signals().define(wanted, value); })) return RDA_NO_SIGNAL;
	return id;
}

uint32_t rda_define_bool(const char* name, int value) {
	if (!name) return RDA_NO_SIGNAL;
	const std::string wanted = name;
	uint32_t id = RDA_NO_SIGNAL;
	if (!onLoop([&] { id = RDA::signals().define(wanted, value != 0); })) return RDA_NO_SIGNAL;
	return id;
}

uint32_t rda_define_text(const char* name, const char* value) {
	if (!name) return RDA_NO_SIGNAL;
	const std::string wanted = name;
	const std::string initial = value ? value : "";
	uint32_t id = RDA_NO_SIGNAL;
	if (!onLoop([&] {
		id = RDA::signals().define(wanted, std::string_view(initial));
	})) return RDA_NO_SIGNAL;
	return id;
}

int rda_define_command(const char* name) {
	if (!name) return fail("no command name");
	const std::string wanted = name;
	return onLoop([&] { RDA::commands().define(wanted); }) ? 1 : 0;
}

int rda_define_table(const char* name) {
	if (!name) return fail("no table name");
	const std::string wanted = name;
	return onLoop([&] { RDA::tables().define(wanted); }) ? 1 : 0;
}

int rda_define_column(const char* table, const char* column, int type) {
	if (!table || !column) return fail("no table or column name");
	const std::string wantedTable = table;
	const std::string wantedColumn = column;
	const RDA::ColumnType kind = type == 1 ? RDA::ColumnType::Bool
	                           : type == 2 ? RDA::ColumnType::Text
	                                       : RDA::ColumnType::Number;
	bool ok = false;
	if (!onLoop([&] {
		const uint32_t id = RDA::tables().find(wantedTable);
		if (id == RDA::kNoTable) return;
		RDA::tables().at(id).defineColumn(wantedColumn, kind);
		ok = true;
	})) return false;
	return ok ? 1 : fail("no table by that name; define it first");
}

// ---- signals ---------------------------------------------------------------------
// find() and type() read a name table that only grows, and they are the two calls a
// binding makes once rather than per write -- but they still go through the loop,
// because "only grows" is not the same as "safe to read during a push_back".
uint32_t rda_signal_find(const char* name) {
	if (!name) return RDA_NO_SIGNAL;
	uint32_t id = RDA_NO_SIGNAL;
	const std::string wanted = name;
	if (!onLoop([&] { id = RDA::signals().find(wanted); })) return RDA_NO_SIGNAL;
	if (id == RDA_NO_SIGNAL) fail("no signal by that name");
	return id;
}

int rda_signal_type(uint32_t signal) {
	int type = -1;
	if (!onLoop([&] {
		if (!RDA::signals().valid(signal)) return;
		type = static_cast<int>(RDA::signals().type(signal));
	})) return -1;
	if (type < 0) fail("no such signal");
	return type;
}

int rda_signal_get_number(uint32_t signal, double* out) {
	if (!out) return fail("no destination");
	bool ok = false;
	if (!onLoop([&] {
		if (!(ok = RDA::signals().valid(signal))) return;
		*out = RDA::signals().number(signal);
	})) return false;
	return ok ? 1 : fail("no such signal");
}

int rda_signal_set_number(uint32_t signal, double value) {
	bool ok = false;
	if (!onLoop([&] {
		if (!(ok = RDA::signals().valid(signal))) return;
		RDA::signals().set(signal, value);
	})) return false;
	return ok ? 1 : fail("no such signal");
}

int rda_signal_get_bool(uint32_t signal, int* out) {
	if (!out) return fail("no destination");
	bool ok = false;
	if (!onLoop([&] {
		if (!(ok = RDA::signals().valid(signal))) return;
		*out = RDA::signals().boolean(signal) ? 1 : 0;
	})) return false;
	return ok ? 1 : fail("no such signal");
}

int rda_signal_set_bool(uint32_t signal, int value) {
	bool ok = false;
	if (!onLoop([&] {
		if (!(ok = RDA::signals().valid(signal))) return;
		RDA::signals().set(signal, value != 0);
	})) return false;
	return ok ? 1 : fail("no such signal");
}

int rda_signal_get_text(uint32_t signal, char* buffer, int capacity) {
	std::string value;
	bool ok = false;
	if (!onLoop([&] {
		if (!(ok = RDA::signals().valid(signal))) return;
		value = std::string(RDA::signals().text(signal)); // copied on the loop thread
	})) return -1;
	if (!ok) { fail("no such signal"); return -1; }

	const int length = static_cast<int>(value.size());
	if (buffer && capacity > 0) {
		const int room = capacity - 1 < length ? capacity - 1 : length;
		std::memcpy(buffer, value.data(), static_cast<size_t>(room));
		buffer[room] = '\0';
	}
	return length; // what it is, not what fitted, so a short buffer can be retried
}

int rda_signal_set_text(uint32_t signal, const char* value) {
	if (!value) return fail("no value");
	const std::string text = value;
	bool ok = false;
	if (!onLoop([&] {
		if (!(ok = RDA::signals().valid(signal))) return;
		RDA::signals().set(signal, std::string_view(text));
	})) return false;
	return ok ? 1 : fail("no such signal");
}

// ---- tables ----------------------------------------------------------------------
namespace {
	// A table id is an index into a vector that only grows, so this cannot dangle -- but
	// it is still read on the loop thread, like everything else here.
	bool withTable(uint32_t id, const std::function<void(RDA::Table&)>& work) {
		bool found = false;
		if (!onLoop([&] {
			// Checked rather than inferred from the shape: Tables::at hands back a shared
			// empty table for a bad id, and an empty table is also what a real one that
			// nobody has filled looks like.
			if (!RDA::tables().valid(id)) return;
			found = true;
			work(RDA::tables().at(id));
		})) return false;
		if (!found) fail("no such table");
		return found;
	}
}

uint32_t rda_table_find(const char* name) {
	if (!name) return RDA_NO_TABLE;
	const std::string wanted = name;
	uint32_t id = RDA_NO_TABLE;
	if (!onLoop([&] { id = RDA::tables().find(wanted); })) return RDA_NO_TABLE;
	if (id == RDA::kNoTable) { fail("no table by that name"); return RDA_NO_TABLE; }
	return id;
}

int rda_table_column(uint32_t table, const char* column) {
	if (!column) { fail("no column name"); return -1; }
	const std::string wanted = column;
	int index = -1;
	if (!withTable(table, [&](RDA::Table& t) {
		const uint32_t found = t.column(wanted);
		if (found != RDA::kNoColumn) index = static_cast<int>(found);
	})) return -1;
	if (index < 0) fail("no column by that name");
	return index;
}

int rda_table_rows(uint32_t table) {
	int rows = -1;
	if (!withTable(table, [&](RDA::Table& t) { rows = static_cast<int>(t.rows()); })) return -1;
	return rows;
}

int rda_table_resize(uint32_t table, int rows) {
	if (rows < 0) return fail("a negative number of rows");
	return withTable(table, [&](RDA::Table& t) {
		t.resize(static_cast<size_t>(rows));
	}) ? 1 : 0;
}

namespace {
	// Shared by the three writers: the bounds nobody should have to repeat, and the copy
	// that has to happen before the work crosses threads.
	bool writable(int column, int first, int count) {
		if (column < 0) return fail("no such column");
		if (first < 0)  return fail("a negative first row");
		if (count < 0)  return fail("a negative count");
		return true;
	}
}

int rda_table_set_numbers(uint32_t table, int column, int first,
                          const double* values, int count) {
	if (!writable(column, first, count)) return 0;
	if (count == 0) return 1;
	if (!values) return fail("no values");
	// Copied here rather than read on the loop thread: the caller's array is the
	// caller's, and it may be a temporary that dies the moment this returns.
	const std::vector<double> copy(values, values + count);
	return withTable(table, [&](RDA::Table& t) {
		for (int i = 0; i < count; ++i) {
			t.setNumber(static_cast<size_t>(first + i), static_cast<uint32_t>(column), copy[i]);
		}
	}) ? 1 : 0;
}

int rda_table_set_bools(uint32_t table, int column, int first,
                        const int* values, int count) {
	if (!writable(column, first, count)) return 0;
	if (count == 0) return 1;
	if (!values) return fail("no values");
	const std::vector<int> copy(values, values + count);
	return withTable(table, [&](RDA::Table& t) {
		for (int i = 0; i < count; ++i) {
			t.setBool(static_cast<size_t>(first + i), static_cast<uint32_t>(column), copy[i] != 0);
		}
	}) ? 1 : 0;
}

int rda_table_set_texts(uint32_t table, int column, int first,
                        const char* const* values, int count) {
	if (!writable(column, first, count)) return 0;
	if (count == 0) return 1;
	if (!values) return fail("no values");
	std::vector<std::string> copy;
	copy.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) copy.emplace_back(values[i] ? values[i] : "");
	return withTable(table, [&](RDA::Table& t) {
		for (int i = 0; i < count; ++i) {
			t.setText(static_cast<size_t>(first + i), static_cast<uint32_t>(column), copy[i]);
		}
	}) ? 1 : 0;
}

int rda_table_get_number(uint32_t table, int column, int row, double* out) {
	if (!out) return fail("no destination");
	if (column < 0 || row < 0) return fail("no such cell");
	bool inside = false;
	if (!withTable(table, [&](RDA::Table& t) {
		if (static_cast<size_t>(row) >= t.rows()) return;
		inside = true;
		*out = t.number(static_cast<size_t>(row), static_cast<uint32_t>(column));
	})) return 0;
	return inside ? 1 : fail("that row is past the end");
}

int rda_table_get_bool(uint32_t table, int column, int row, int* out) {
	if (!out) return fail("no destination");
	if (column < 0 || row < 0) return fail("no such cell");
	bool inside = false;
	if (!withTable(table, [&](RDA::Table& t) {
		if (static_cast<size_t>(row) >= t.rows()) return;
		inside = true;
		*out = t.boolean(static_cast<size_t>(row), static_cast<uint32_t>(column)) ? 1 : 0;
	})) return 0;
	return inside ? 1 : fail("that row is past the end");
}

int rda_table_get_text(uint32_t table, int column, int row, char* buffer, int capacity) {
	if (column < 0 || row < 0) { fail("no such cell"); return -1; }
	std::string value;
	bool inside = false;
	if (!withTable(table, [&](RDA::Table& t) {
		if (static_cast<size_t>(row) >= t.rows()) return;
		inside = true;
		value = std::string(t.text(static_cast<size_t>(row), static_cast<uint32_t>(column)));
	})) return -1;
	if (!inside) { fail("that row is past the end"); return -1; }

	const int length = static_cast<int>(value.size());
	if (buffer && capacity > 0) {
		const int room = capacity - 1 < length ? capacity - 1 : length;
		std::memcpy(buffer, value.data(), static_cast<size_t>(room));
		buffer[room] = '\0';
	}
	return length;
}

// ---- commands --------------------------------------------------------------------
int rda_command_bind(const char* name, rda_callback fn, void* user) {
	if (!name) return fail("no command name");
	if (!fn) return fail("no callback");
	const std::string wanted = name;
	bool ok = false;
	if (!onLoop([&] {
		const uint32_t command = RDA::commands().find(wanted);
		if (command == RDA::kNoCommand) return;
		RDA::commands().bind(command, [fn, user] { fn(user); });
		ok = true;
	})) return false;
	return ok ? 1 : fail("no command by that name; declaring one is what creates it");
}

int rda_command_invoke(const char* name) {
	if (!name) return fail("no command name");
	const std::string wanted = name;
	bool found = false;
	bool ran = false;
	if (!onLoop([&] {
		const uint32_t command = RDA::commands().find(wanted);
		if (command == RDA::kNoCommand) return;
		found = true;
		// On the loop thread, which is where a command invoked by the interface runs --
		// so a handler cannot tell which of the two asked, and does not have to.
		ran = RDA::commands().invoke(command);
	})) return false;
	if (!found) return fail("no command by that name; declaring one is what creates it");
	return ran ? 1 : fail("that command exists but nothing is bound to it");
}

// ---- commands, for a runtime that owns its own thread ------------------------------
int rda_command_watch(const char* name) {
	if (!name) return fail("no command name");
	const std::string wanted = name;
	bool ok = false;
	if (!onLoop([&] {
		const uint32_t command = RDA::commands().find(wanted);
		if (command == RDA::kNoCommand) return;
		// The handler runs on the loop thread and does the least it can: take a lock,
		// push a string, return. Anything more would be work the interface waits for.
		RDA::commands().bind(command, [wanted] {
			std::lock_guard<std::mutex> held(gQueueLock);
			if (gPending.size() >= kMaxPending) {
				if (!gWarnedOverflow) {
					gWarnedOverflow = true;
					RDA_LOG_WARNING("rda: " << kMaxPending << " commands are waiting to be "
					                "collected and the oldest are being dropped; is the "
					                "backend still calling rda_poll_command?");
				}
				gPending.erase(gPending.begin());
			}
			gPending.push_back(wanted);
		});
		ok = true;
	})) return false;
	return ok ? 1 : fail("no command by that name; declaring one is what creates it");
}

int rda_poll_command(char* buffer, int capacity) {
	if (!buffer || capacity <= 0) { fail("no buffer"); return -1; }
	std::string next;
	{
		// No hop to the loop thread: this is a queue of its own, and a backend polling
		// its own queue must not cost a frame every time it looks.
		std::lock_guard<std::mutex> held(gQueueLock);
		if (gPending.empty()) return 0;
		next = std::move(gPending.front());
		gPending.erase(gPending.begin());
	}
	const int length = static_cast<int>(next.size());
	const int room = capacity - 1 < length ? capacity - 1 : length;
	std::memcpy(buffer, next.data(), static_cast<size_t>(room));
	buffer[room] = '\0';
	return 1;
}


// ---- viewports -------------------------------------------------------------------

int rda_viewport_draw(const char* name, const rda_draw_cmd* commands, int count) {
	if (!name || !*name) return fail("no viewport name");
	if (count < 0) return fail("a negative number of commands");
	if (count > 0 && !commands) return fail("no commands");

	// Translated on the caller's thread and moved across, so the loop thread does no
	// parsing and holds no pointer into the caller's memory -- including the strings,
	// which a Python list would be free to collect the moment this returns.
	std::vector<RDA::DrawCommand> list;
	list.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		const rda_draw_cmd& in = commands[i];
		if (in.op < RDA_DRAW_CLEAR || in.op > RDA_DRAW_TEXT) {
			// Named rather than counted: a list built in a loop makes the index unhelpful,
			// and the op is the thing that was wrong.
			return fail("a drawing command has an op this engine does not know");
		}
		RDA::DrawCommand out;
		out.op = static_cast<RDA::DrawOp>(in.op);
		out.color = in.color;
		out.a = in.a; out.b = in.b; out.c = in.c; out.d = in.d; out.e = in.e;
		if (in.op == RDA_DRAW_TEXT && in.text) out.text = in.text;
		list.push_back(std::move(out));
	}

	const std::string wanted = name;
	if (!onLoop([&] { RDA::viewports().setCommands(wanted, std::move(list)); })) return false;
	return 1;
}

int rda_set_theme(const char* path) {
	if (!path || !*path) return fail("no theme path");
	const std::string wanted = path;
	bool ok = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window) return;
		ok = window->gui().theme().replaceWithFile(wanted);
	})) return false;
	return ok ? 1 : fail("cannot load that theme; the log says why");
}

int rda_set_icon(const char* path) {
	const std::string wanted = path ? path : "";
	bool ok = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window) return;
		ok = window->setIcon(wanted);
	})) {
		return 0;
	}
	// The engine has already said in the log what was wrong with the file; this is only
	// the caller's half of it.
	return ok ? 1 : fail("cannot read that icon");
}

int rda_set_title(const char* title) {
	if (!title) return fail("no title");
	const std::string wanted = title;
	bool ok = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window || !window->getGLFW()) return;
		glfwSetWindowTitle(window->getGLFW(), wanted.c_str());
		ok = true;
	})) return false;
	return ok ? 1 : fail("there is no window to title");
}

int rda_focus(const char* id) {
	const std::string wanted = (id && *id) ? id : std::string();
	bool ok = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window) return;
		// An empty name means "nobody", which is what a click on nothing does.
		window->gui().setFocus(wanted.empty() ? 0u : window->gui().widgetId(wanted.c_str()));
		ok = true;
	})) return false;
	return ok ? 1 : fail("there is no window to focus in");
}

// Deliberately not through onLoop.
//
// Everything else here marshals to the loop thread and waits, which is what makes a call
// from any thread safe. A dialog would then hold the loop for as long as somebody took to
// find their file, and the window would stop drawing -- so these run where they were
// called and the engine goes on painting behind them. Nothing they touch is engine state.
static int pickInto(bool folders, const char* title, const char* start, const char* filter,
                    char* buffer, int capacity) {
	if (capacity < 0 || (capacity > 0 && !buffer)) { fail("no buffer"); return -1; }
	if (!rendeerRunning()) { fail("the engine is not running"); return -1; }

	std::string chosen;
	const bool answered = folders
		? RDA::pickFolder(title ? title : "", start ? start : "", chosen)
		: RDA::pickFile(title ? title : "", start ? start : "", filter ? filter : "", chosen);
	if (!answered) return 0;   // cancelled, or no chooser: an answer, not an error

	const int length = static_cast<int>(chosen.size());
	if (!buffer || capacity <= 0) return length;
	const int room = capacity - 1 < length ? capacity - 1 : length;
	std::memcpy(buffer, chosen.data(), static_cast<size_t>(room));
	buffer[room] = '\0';
	return length;
}

int rda_poll_dropped_file(char* buffer, int capacity) {
	if (!buffer || capacity <= 0) { fail("no buffer"); return -1; }
	std::string next;
	{
		std::lock_guard<std::mutex> held(gDropLock);
		if (gDropped.empty()) return 0;
		next = std::move(gDropped.front());
		gDropped.erase(gDropped.begin());
	}
	const int length = static_cast<int>(next.size());
	const int room = capacity - 1 < length ? capacity - 1 : length;
	std::memcpy(buffer, next.data(), static_cast<size_t>(room));
	buffer[room] = '\0';
	return 1;
}

int rda_image_define(const char* name, const void* bytes, int size) {
	if (!name || !*name) return fail("no image name");
	if (!bytes || size <= 0) return fail("no image bytes");
	// Copied before the hop, not after: the caller owns that buffer and is free to let it
	// go the moment this returns, while the loop may not run for another frame.
	const std::string wanted = name;
	const std::vector<unsigned char> copy(static_cast<const unsigned char*>(bytes),
	                                      static_cast<const unsigned char*>(bytes) + size);
	bool ok = false;
	if (!onLoop([&] { ok = RDA::images().define(wanted, copy.data(), copy.size()); })) {
		return false;
	}
	return ok ? 1 : fail("those bytes are not a picture this engine can read");
}

int rda_image_define_pixels(const char* name, const void* pixels, int width, int height) {
	if (!name || !*name) return fail("no image name");
	if (!pixels) return fail("no pixels");
	if (width <= 0 || height <= 0) return fail("an image needs a width and a height");
	const std::string wanted = name;
	const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
	const std::vector<unsigned char> copy(static_cast<const unsigned char*>(pixels),
	                                      static_cast<const unsigned char*>(pixels) + bytes);
	bool ok = false;
	if (!onLoop([&] {
		ok = RDA::images().definePixels(wanted, copy.data(), static_cast<uint32_t>(width),
		                                static_cast<uint32_t>(height));
	})) return false;
	return ok ? 1 : fail("cannot make a texture that size");
}

int rda_image_forget(const char* name) {
	if (!name || !*name) return fail("no image name");
	const std::string wanted = name;
	if (!onLoop([&] { RDA::images().forget(wanted); })) return false;
	return 1;
}

int rda_stream_push(const char* name, const void* pixels, int width, int height) {
	if (!name || !*name) return fail("no stream name");
	if (!pixels) return fail("no pixels");
	if (width <= 0 || height <= 0) return fail("a frame needs a width and a height");
	const std::string wanted = name;
	const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
	// Copied before the hop: the caller owns that buffer and may let it go the moment
	// this returns, while the loop may not run for another frame.
	const std::vector<unsigned char> copy(static_cast<const unsigned char*>(pixels),
	                                      static_cast<const unsigned char*>(pixels) + bytes);
	bool ok = false;
	if (!onLoop([&] {
		ok = RDA::streams().push(wanted, copy.data(), static_cast<uint32_t>(width),
		                         static_cast<uint32_t>(height));
	})) return false;
	return ok ? 1 : fail("cannot push that frame");
}

int rda_stream_push_encoded(const char* name, const void* bytes, int size) {
	if (!name || !*name) return fail("no stream name");
	if (!bytes || size <= 0) return fail("no frame bytes");
	const std::string wanted = name;
	const std::vector<unsigned char> copy(static_cast<const unsigned char*>(bytes),
	                                      static_cast<const unsigned char*>(bytes) + size);
	bool ok = false;
	if (!onLoop([&] { ok = RDA::streams().pushEncoded(wanted, copy.data(), copy.size()); })) {
		return false;
	}
	return ok ? 1 : fail("those bytes are not a frame this engine can read");
}

int rda_stream_wanted(const char* name) {
	if (!name || !*name) return fail("no stream name");
	// Does not hop: it reads two numbers, and a producer asks this on every turn of its
	// own loop. A round trip to the next frame for a question about whether to do work
	// would cost more than the work.
	return RDA::streams().wanted(name) ? 1 : 0;
}

int rda_stream_counts(const char* name, long long* pushed, long long* shown) {
	if (!name || !*name) return fail("no stream name");
	uint64_t a = 0, b = 0;
	RDA::streams().counts(name, a, b);
	if (pushed) *pushed = static_cast<long long>(a);
	if (shown)  *shown  = static_cast<long long>(b);
	return 1;
}

int rda_stream_close(const char* name) {
	if (!name || !*name) return fail("no stream name");
	const std::string wanted = name;
	if (!onLoop([&] { RDA::streams().close(wanted); })) return false;
	return 1;
}

int rda_effect_define(const char* name, const char* glsl) {
	if (!name || !*name) return fail("no effect name");
	if (!glsl || !*glsl) return fail("no shader");
	const std::string wanted = name;
	const std::string source = glsl;
	bool ok = false;
	if (!onLoop([&] { ok = RDA::effects().define(wanted, source); })) return false;
	return ok ? 1 : fail("that shader will not compile; the log has the compiler's message");
}

int rda_effect_apply(const char* name, const char* source, const char* into,
                     const float* params, int count) {
	if (!name || !*name) return fail("no effect name");
	if (!source || !*source) return fail("no source stream");
	if (!into || !*into) return fail("no destination stream");
	const std::string wantedName = name;
	const std::string wantedFrom = source;
	const std::string wantedInto = into;
	RDA::EffectParams values;
	const int take = count < 0 ? 0 : (count > 8 ? 8 : count);
	for (int i = 0; params && i < take; ++i) values.value[i] = params[i];
	bool ok = false;
	if (!onLoop([&] {
		ok = RDA::effects().apply(wantedName, wantedFrom, wantedInto, values);
	})) return false;
	return ok ? 1 : fail("cannot run that effect; the log says why");
}

int rda_effect_apply_many(const char* name, const char* const* sources, int sourceCount,
                          const char* into, const float* params, int paramCount) {
	if (!name || !*name) return fail("no effect name");
	if (!sources || sourceCount <= 0) return fail("no source streams");
	if (!into || !*into) return fail("no destination stream");
	std::vector<std::string> from;
	from.reserve(static_cast<size_t>(sourceCount));
	for (int i = 0; i < sourceCount; ++i) {
		if (!sources[i] || !*sources[i]) return fail("a source stream has no name");
		from.emplace_back(sources[i]);
	}
	const std::string wantedName = name;
	const std::string wantedInto = into;
	RDA::EffectParams values;
	const int take = paramCount < 0 ? 0 : (paramCount > 8 ? 8 : paramCount);
	for (int i = 0; params && i < take; ++i) values.value[i] = params[i];
	bool ok = false;
	if (!onLoop([&] {
		ok = RDA::effects().apply(wantedName, from, wantedInto, values);
	})) return false;
	return ok ? 1 : fail("cannot run that effect; the log says why");
}

int rda_stream_read(const char* name, unsigned char* buffer, int capacity,
                    int* width, int* height) {
	if (!name || !*name) { fail("no stream name"); return -1; }
	const std::string wanted = name;
	std::vector<unsigned char> pixels;
	uint32_t w = 0, h = 0;
	bool ok = false;
	if (!onLoop([&] { ok = RDA::streams().read(wanted, pixels, w, h); })) return -1;
	if (!ok) { fail("there is no frame to read"); return -1; }

	if (width)  *width  = static_cast<int>(w);
	if (height) *height = static_cast<int>(h);
	const int needed = static_cast<int>(pixels.size());
	// Asked with no buffer: this is the size to make one.
	if (!buffer || capacity <= 0) return needed;
	if (capacity < needed) {
		fail("that buffer is too small for the frame");
		return needed;   // the real size, so a caller can make one and ask again
	}
	std::memcpy(buffer, pixels.data(), pixels.size());
	return needed;
}

int rda_effect_forget(const char* name) {
	if (!name || !*name) return fail("no effect name");
	const std::string wanted = name;
	if (!onLoop([&] { RDA::effects().forget(wanted); })) return false;
	return 1;
}

int rda_pick_folder(const char* title, const char* start, char* buffer, int capacity) {
	return pickInto(true, title, start, nullptr, buffer, capacity);
}

int rda_pick_file(const char* title, const char* start, const char* filter,
                  char* buffer, int capacity) {
	return pickInto(false, title, start, filter, buffer, capacity);
}

int rda_clipboard_set(const char* text) {
	if (!text) return fail("no text");
	const std::string wanted = text;
	bool ok = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window || !window->getGLFW()) return;
		glfwSetClipboardString(window->getGLFW(), wanted.c_str());
		ok = true;
	})) return false;
	return ok ? 1 : fail("there is no window to reach the clipboard through");
}

int rda_clipboard_get(char* buffer, int capacity) {
	if (capacity < 0 || (capacity > 0 && !buffer)) return fail("no buffer");
	std::string held;
	bool ok = false;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window || !window->getGLFW()) return;
		const char* text = glfwGetClipboardString(window->getGLFW());
		if (text) held = text;
		ok = true; // an empty clipboard is an answer, not a failure
	})) return -1;
	if (!ok) { fail("there is no window to reach the clipboard through"); return -1; }
	// Same two-call shape as rda_signal_get_text: the length first, then the bytes.
	const int length = static_cast<int>(held.size());
	if (!buffer || capacity <= 0) return length;
	const int room = capacity - 1 < length ? capacity - 1 : length;
	std::memcpy(buffer, held.data(), static_cast<size_t>(room));
	buffer[room] = '\0';
	return length;
}

int rda_measure_text(const char* text, float size, float* out_width, float* out_height) {
	if (!text) return fail("no text");
	if (!out_width || !out_height) return fail("no destination");
	bool ok = false;
	float width = 0.0f, height = 0.0f;
	if (!onLoop([&] {
		RDA::Window* window = getMainWindow();
		if (!window) return;
		RDA::TextStyle style;
		style.size = size > 0.0f ? size : 0.0f; // 0 is the interface's own size
		width = window->gui().measureText(text, style);
		height = window->gui().lineHeight(style);
		ok = true;
	})) return false;
	*out_width = width;
	*out_height = height;
	return ok ? 1 : fail("there is no window to measure against");
}

int rda_viewport_size(const char* name, float* out_width, float* out_height) {
	if (!name || !*name) return fail("no viewport name");
	if (!out_width || !out_height) return fail("no destination");
	const std::string wanted = name;
	glm::vec2 size{ 0.0f, 0.0f };
	if (!onLoop([&] { size = RDA::viewports().size(wanted); })) return false;
	*out_width = size.x;
	*out_height = size.y;
	return 1;
}

} // extern "C"
