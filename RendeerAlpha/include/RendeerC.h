#ifndef RENDEER_C_H
#define RENDEER_C_H

#include <stdint.h>

// The engine as a C ABI, so a backend can be written in something other than C++.
//
// This is the seam that replaced the wire protocol. The protocol carried a *draw list*,
// which made another language a client that draws; this carries what a backend actually
// does -- load a compiled interface, write state, answer commands -- so Python, Node or
// anything else with an FFI gets the whole interface layer rather than a canvas.
//
// Nothing here is a second engine. Every call reaches the same signals, the same
// commands and the same blueprint loader a C++ application uses, and a layout cannot
// tell which language is behind it.
//
// ---- threading ----
//
// rda_init() spawns the loop thread and returns. Every other call in this file may
// therefore be made from a different thread than the one the engine runs on -- and the
// signal table is not thread-safe, so none of them touch it directly. Each hands its
// work to the loop thread and waits for it to run, which is what LoopWork exists for.
//
// Two consequences worth knowing:
//
//   * A call costs a round trip to the next frame. That is the right price for a
//     backend, which reacts to events; it is the wrong price for a render path, which
//     is why there is no drawing here at all.
//   * An idle window is woken first. On-demand redraw means the loop can be parked in
//     the event pump with no frame due for seconds, and a request that waited for one
//     would look like a hang.
//
// Callbacks run *on* the loop thread. A call made from inside one runs inline, so
// loading the interface from on_start costs nothing extra.
//
// Two exceptions to the hop, both deliberate: the lifecycle calls (running, stop, wait)
// only read an atomic, and rda_poll_command reads a queue of its own -- see the command
// section at the bottom for why a runtime might need that instead of a callback.
//
// ---- errors ----
//
// Functions returning int return 1 for success and 0 for failure; rda_last_error()
// says what went wrong. A failure is never a crash: a name that does not exist, a file
// that will not load and a call made before rda_init() all return 0.

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
	#if defined(RDA_C_BUILDING)
		#define RDA_C_API __declspec(dllexport)
	#else
		#define RDA_C_API __declspec(dllimport)
	#endif
#else
	#define RDA_C_API __attribute__((visibility("default")))
#endif

// ---- which ABI this is ------------------------------------------------------------
//
// A package and the engine it opens are separately installed and separately updated: a
// Python binding is registered once by path and the shared library is rebuilt underneath
// it, so the two versions drift by design. Without a way to ask, a mismatch is a missing
// symbol at best and a function that quietly means something else at worst.
//
// Two numbers, and the pair of them is the compatibility promise rather than a label on
// it:
//
//     MAJOR  changes when something here is removed, renamed, given a different
//            signature, or made to mean something else. Nothing written against an
//            earlier major keeps working, and it is not expected to.
//
//     MINOR  changes when something is added and nothing else moves. Every package
//            written against an earlier minor of the same major keeps working, unchanged,
//            against this engine.
//
// So a binding asks two questions:
//
//     rda_abi_major() == the major it was built for     // the same language
//     rda_abi_minor() >= the minor it was built for     // and new enough to have it all
//
// The first is equality rather than a range, in both directions: an engine one major
// ahead has removed something the package calls, and an engine one major behind has never
// had it. The second is one-sided, which is the whole point of the split -- an engine
// that has moved 1.0 -> 1.7 still answers every package built against 1.0, and that
// sentence is now readable off the number instead of being a second function's opinion.
//
// The meaning-change case is the one people get wrong: a function that keeps its name and
// its signature but starts doing something else is a MAJOR change. The compiler will not
// catch it and neither will a loader, so the version has to.
//
// These two functions may never change, since they are what a binding calls before it
// trusts anything else here.
#define RDA_ABI_MAJOR 1
#define RDA_ABI_MINOR 1
RDA_C_API int rda_abi_major(void);
RDA_C_API int rda_abi_minor(void);

typedef struct rda_config rda_config;

// `user` is whatever was handed to the function that registered this. The engine never
// looks inside it.
typedef void (*rda_callback)(void* user);
typedef void (*rda_update_callback)(float dt_seconds, void* user);

// ---- what went wrong -------------------------------------------------------------
// The message from the last call that returned 0, or "" if none has. Owned by the
// engine and valid until the next failing call on the same thread.
RDA_C_API const char* rda_last_error(void);

// ---- configuration ---------------------------------------------------------------
// A handle rather than a struct, so adding a field to AppConfig does not change this
// file's ABI and every caller does not have to be rebuilt.
RDA_C_API rda_config* rda_config_new(void);
RDA_C_API void rda_config_free(rda_config* config);

RDA_C_API void rda_config_set_name(rda_config* config, const char* name);
// The compiled theme (.rdth). Empty or unset leaves the engine's built-in look.
RDA_C_API void rda_config_set_theme(rda_config* config, const char* path);
// Syntax languages beyond the built-in "python" (res/themes/languages.xml).
RDA_C_API void rda_config_set_languages(rda_config* config, const char* path);
RDA_C_API void rda_config_set_vsync(rda_config* config, int on);
// The font the interface is drawn in, and the pixel height its body text is baked at.
// A path of NULL or "" keeps the engine's own; a height of 0 keeps the default. The
// other sizes a theme asks for are baked alongside it, so this is one number rather
// than a list.
RDA_C_API void rda_config_set_font(rda_config* config, const char* path, float height);

// Once, after the window and renderer are up. This is where an interface is loaded.
RDA_C_API void rda_config_on_start(rda_config* config, rda_callback fn, void* user);
// Once per frame, before it is drawn.
RDA_C_API void rda_config_on_update(rda_config* config, rda_update_callback fn, void* user);
// Once, after the loop ends and before teardown.
RDA_C_API void rda_config_on_shutdown(rda_config* config, rda_callback fn, void* user);

// ---- lifecycle -------------------------------------------------------------------
// Starts the engine on a thread of its own and returns once it is up -- meaning the
// window, the renderer and the on_start callback have all finished. So the line after
// this one may write state and expect it to exist, which is the only contract worth
// having: the alternative is every backend hand-rolling the same handshake, and getting
// it wrong once each.
//
// Returns 0 if the engine failed to start, or if on_start stopped it. The config may be
// freed afterwards. Calling this twice without a rda_wait() in between fails.
RDA_C_API int rda_init(rda_config* config);
// Whether the loop is still going. False after every window has closed.
RDA_C_API int rda_running(void);
// Asks the loop to end. Safe from any thread, and safe before rda_init().
RDA_C_API void rda_stop(void);
// Blocks until the loop thread has ended and been joined.
RDA_C_API void rda_wait(void);

// ---- the interface ---------------------------------------------------------------
// Loads a compiled layout (.rdab) as the interface, replacing whatever was showing.
// `source_path` may be empty; where a build can compile, it is the .tsx to watch.
RDA_C_API int rda_load_interface(const char* blueprint_path, const char* source_path);

// ---- screens ---------------------------------------------------------------------
// The other way to show an interface: several layouts, one showing at a time, with the
// name of that one held in a signal called `route`. Navigating is then an ordinary
// write -- rda_signal_set_text(route, "catalogue") -- because "which screen is showing"
// is state, and this engine has one answer for state.
//
// Opening routes replaces anything rda_load_interface put up, and vice versa: they are
// two ways to answer the same question and only one of them can be right at a time.
//
// The four arrays are parallel and `count` long. `params[i]` names the signals that are
// screen i's arguments, comma-separated ("productId,page"), or NULL for none; going back
// restores them along with the route, which is the whole reason they are named. Passed
// as one call rather than built up over several, because each call is a hop to the loop
// thread and a route table is known all at once.
//
// `layout_dir` is where the blueprints are (`<layout>.rdab`); `source_dir` may be empty,
// and where a build can compile it is where their .tsx are, for reloading on a change.
RDA_C_API int rda_open_routes(const char* const* names, const char* const* layouts,
                              const char* const* params, int count,
                              const char* layout_dir, const char* source_dir);

// How long one screen takes to cross-fade into the next, in milliseconds. Both trees
// exist for the length of it, so zero -- an instant swap -- is the default.
//
// Safe from any thread and before rda_init(): the value is held here and applied by the
// loop, rather than written into the router from whichever thread asked.
RDA_C_API void rda_set_transition_ms(float ms);

// History. The declaration that names routes also declares `back` and `forward` as
// commands, and the router binds them itself, so a layout gets a working back button
// without anybody writing one. These are the same thing from outside the interface.
RDA_C_API int rda_route_back(void);
RDA_C_API int rda_route_forward(void);
// Whether there is anywhere to go. 1 yes, 0 no.
RDA_C_API int rda_route_can_go_back(void);
RDA_C_API int rda_route_can_go_forward(void);

// ---- declaring what the interface reads -------------------------------------------
// A layout is compiled against a state declaration, and the signals it names have to
// exist before it loads. In C++ the generated `State::define()` does this; in another
// language its generated equivalent calls these.
//
// Defining an existing name returns the same id rather than a second signal, so calling
// define twice is harmless. Redeclaring one with a different type is refused.
RDA_C_API uint32_t rda_define_number(const char* name, double value);
RDA_C_API uint32_t rda_define_bool(const char* name, int value);
RDA_C_API uint32_t rda_define_text(const char* name, const char* value);
RDA_C_API int rda_define_command(const char* name);
RDA_C_API int rda_define_table(const char* name);
// `type` is 0 number, 1 bool, 2 text -- the same order as rda_signal_type reports.
RDA_C_API int rda_define_column(const char* table, const char* column, int type);

// ---- signals ---------------------------------------------------------------------
// The id of a declared signal, or RDA_NO_SIGNAL. Ids are stable for the life of the
// process, so a binding looks a name up once and keeps the number.
#define RDA_NO_SIGNAL 0xFFFFFFFFu

RDA_C_API uint32_t rda_signal_find(const char* name);
// 0 number, 1 bool, 2 text; -1 when the signal does not exist.
RDA_C_API int rda_signal_type(uint32_t signal);

RDA_C_API int rda_signal_get_number(uint32_t signal, double* out);
RDA_C_API int rda_signal_set_number(uint32_t signal, double value);
RDA_C_API int rda_signal_get_bool(uint32_t signal, int* out);
RDA_C_API int rda_signal_set_bool(uint32_t signal, int value);
// Writes at most `capacity` bytes including the terminator and reports the length the
// text actually is, so a caller that guessed too small can ask again with the right
// size. -1 means the signal does not exist.
RDA_C_API int rda_signal_get_text(uint32_t signal, char* buffer, int capacity);
RDA_C_API int rda_signal_set_text(uint32_t signal, const char* value);

// ---- tables ----------------------------------------------------------------------
// The rows a <list> shows. A signal holds one value; a catalogue holds ten thousand,
// and the point of a table is that showing them costs about as many widgets as fit on
// screen rather than ten thousand.
//
// The writers take a run of values rather than one, and that is not an optimisation
// detail -- it is why this is usable at all. Every call here crosses to the loop thread
// and waits, so filling ten thousand rows one cell at a time would be thirty thousand
// round trips. Filled a column at a time it is three.
#define RDA_NO_TABLE 0xFFFFFFFFu

RDA_C_API uint32_t rda_table_find(const char* name);
// The index of a declared column, or -1. Columns are addressed by index because that is
// what a run of values is written against, and the index never moves once declared.
RDA_C_API int rda_table_column(uint32_t table, const char* column);
// How many rows it holds, or -1 if there is no such table.
RDA_C_API int rda_table_rows(uint32_t table);
// Grows or shrinks it. New rows are zero, false and empty.
RDA_C_API int rda_table_resize(uint32_t table, int rows);

// `count` values into `column`, starting at row `first`. Rows past the end are ignored
// rather than growing the table: how big it is stays something the caller said once.
RDA_C_API int rda_table_set_numbers(uint32_t table, int column, int first,
                                    const double* values, int count);
RDA_C_API int rda_table_set_bools(uint32_t table, int column, int first,
                                  const int* values, int count);
RDA_C_API int rda_table_set_texts(uint32_t table, int column, int first,
                                  const char* const* values, int count);

RDA_C_API int rda_table_get_number(uint32_t table, int column, int row, double* out);
RDA_C_API int rda_table_get_bool(uint32_t table, int column, int row, int* out);
// Same two-call shape as rda_signal_get_text: reports the length the text actually is,
// so a caller that guessed too small can ask again.
RDA_C_API int rda_table_get_text(uint32_t table, int column, int row,
                                 char* buffer, int capacity);

// ---- commands --------------------------------------------------------------------
// Binds a declared command to a callback, replacing any previous binding. The callback
// runs on the loop thread, inside the frame the interface invoked it in.
RDA_C_API int rda_command_bind(const char* name, rda_callback fn, void* user);

// Asks for a command as if the interface had. The interface is not the only thing that
// can want work done: a backend with a menu, a hotkey or a scheduled job wants the same
// handler to run, and `Commands::invoke` is what a C++ application already calls for it.
//
// Returns 0 when no command has that name, or when the name exists and nothing is bound
// to it -- which is a mistake worth reporting rather than a silent nothing.
//
// For a watched command (below) this queues it, exactly as a click would, so the same
// handler runs on the same thread whichever asked.
RDA_C_API int rda_command_invoke(const char* name);

// ---- commands, for a runtime that owns its own thread ------------------------------
//
// rda_command_bind hands the engine a function pointer and the engine calls it on the
// loop thread. That is the natural shape for C, C++ and Python, and it is *impossible*
// for a runtime whose values may only be touched on its own thread. Node is the case
// this exists for: an FFI callback there does not crash, it deadlocks -- the engine's
// thread blocks trying to reach the JavaScript thread, which is itself blocked inside
// the call that is waiting for the engine.
//
// So a command may instead be watched. The engine records that it fired and returns at
// once; the backend collects it whenever its own loop next comes round, and runs the
// handler on its own thread where its values are legal.
//
// The difference from binding is one of timing and nothing else. A bound handler runs
// inside the frame the interface invoked it in; a watched one runs whenever the backend
// next looks. Neither is on the interface's critical path -- a command never returns a
// value, which is what makes deferring it sound.
RDA_C_API int rda_command_watch(const char* name);

// The next command that fired, or 0 when none is waiting. Writes at most `capacity`
// bytes including the terminator.
//
// Alone among the calls that carry something back out of the engine, this one does not
// cross to the loop thread: it reads a queue of its own behind a mutex, so a backend may
// poll it as often as its loop turns without paying a frame each time. (The lifecycle
// calls -- running, stop, wait -- skip the hop too, but they only read an atomic.)
// Returns -1 on a bad argument.
//
// The queue holds a bounded number of pending commands; a backend that stops polling
// entirely loses the oldest, and the log says so once.
RDA_C_API int rda_poll_command(char* buffer, int capacity);

// ---- viewports -------------------------------------------------------------------
//
// A <viewport name="..."> is a hole in the interface for the application to fill. What
// C++ gets there is the command buffer and the device; what reaches this ABI is a list
// of 2D commands the engine draws -- rectangles, lines and text, in the viewport's own
// coordinates, with 0,0 at its top-left corner.
//
// The line between the two is deliberate rather than unfinished. Handing a Vulkan
// command buffer across this boundary would mean a backend owning GPU memory, obeying a
// frame's timing, and being on the render thread at the right microsecond -- none of
// which a Python or Node program is in a position to do. A list of drawings is something
// any language can produce, and it covers what a backend actually wants a viewport for:
// a plot, a timeline, a map, a board.
#define RDA_DRAW_CLEAR 0 // fills the viewport; only `color` is read
#define RDA_DRAW_RECT  1 // a, b, c, d = x, y, w, h; e = corner radius
#define RDA_DRAW_LINE  2 // a, b -> c, d; e = width (0 means 1)
#define RDA_DRAW_TEXT  3 // a, b = top-left; e = font size (0 = the interface's own)

// One drawing instruction. The fields are named for their position rather than their
// meaning because the meaning is the op's: a struct per shape would be a union, and a
// union is what an FFI is worst at.
typedef struct rda_draw_cmd {
	int32_t  op;
	uint32_t color;  // 0xAABBGGRR, the same packing a theme compiles to
	float    a, b, c, d;
	float    e;
	const char* text; // RDA_DRAW_TEXT only; copied, so it need not outlive the call
} rda_draw_cmd;

// Replaces everything the named viewport was drawing, and it keeps drawing that until
// replaced. One call for the whole picture, for the same reason a table is filled a
// column at a time: each call crosses to the loop thread and waits.
//
// `count` of 0 clears it -- a viewport drawn empty is blank, which is different from one
// no backend has ever touched, where the engine's own scene shows instead.
RDA_C_API int rda_viewport_draw(const char* name, const rda_draw_cmd* commands, int count);

// Swaps the whole look, while the window stays open. The compiled .rdth replaces what
// was there rather than layering on top of it, so a theme that simply does not mention
// `button.danger` does not inherit the last one's.
//
// Every colour and radius that the new theme moves travels there over the transitionMs
// the new theme gives it, because a widget's colour was already an animated value and
// this only changes where it is heading. A theme whose variants say 0 swaps instantly.
//
// Added in ABI 1.1. Refused, with the reason, if the file will not load -- and the
// interface keeps the theme it had rather than falling back to the built-in look.
RDA_C_API int rda_set_theme(const char* path);

// The size the viewport was last laid out at, in the same pixels the drawing uses. Both
// zero until a frame has placed it, so a backend that wants to fit its drawing to the
// surface should draw again when it changes rather than only once at start-up.
RDA_C_API int rda_viewport_size(const char* name, float* out_width, float* out_height);

// A circle is a rect with equal sides and a radius of half of them; there is no separate
// op for it, and one fewer thing to learn.

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RENDEER_C_H
