#pragma once
#include <cstddef>
#include <functional>
#include <string>

struct JSContext;
struct JSRuntime;

// One JavaScript engine for the build-time tools, and one way of saying what it threw.
//
// Every compiler here evaluates a little JavaScript while a file is built -- a layout, a
// theme, a mesh, state.ts -- and each used to create its own runtime and context and turn an
// exception into text by hand: seven copies of the first, and of the second three formatters
// and five places that evaluated a script and called one. They had drifted only where
// somebody had a reason to make them, and those reasons are the two parameters below. How
// much C stack the engine may use is a decision of the thread the compile runs on. How an
// exception is worded is a decision about who reads it: a person who wrote a file, or the
// compiler's own code that wants the message and nothing under it. Everything else was the
// same in every copy because nothing ever needed it to differ -- no memory limit, no
// interrupt handler, no module loader, every standard intrinsic.
//
// Internal to the layout tools, like ModuleTransform.h. The run-time script host is another
// engine in another layer, with a deadline and natives of its own; see
// runtime/src/Layout/Script.
namespace RDA::Layout {

	class SourceMap;

	// How an exception that has reached C++ is put into words.
	//
	// The default is what a person who wrote a file needs: the message, then the stack under
	// it, and each frame of the file `origins` maps moved to the line it was written on.
	struct ThrownFormat {
		// Where the lines of the script being evaluated came from. With none, the stack as
		// QuickJS wrote it -- for the engine's own helpers, whose lines are their own.
		const SourceMap* origins = nullptr;

		// The stack under the message. Off where the message already says what went wrong
		// and the frames would be the engine's own code calling itself.
		bool stack = true;

		// Drops a leading "Error: " from the message, for a message that is going to be put
		// in a sentence of the compiler's own.
		bool dropErrorPrefix = false;

		// What an exception that cannot be turned into text is said as.
		const char* unprintable = "<unprintable exception>";
	};

	// The exception pending in `ctx`, taken and said as `format` says. The three forms are
	// the same function: no map, a map, and every part of the format spelled out.
	std::string thrownText(JSContext* ctx);
	std::string thrownText(JSContext* ctx, const SourceMap& origins);
	std::string thrownText(JSContext* ctx, const ThrownFormat& format);

	// `source` evaluated as a global script named `name`, and its value dropped. False, with
	// the exception said in `error` as `format` says it, if it threw.
	//
	// For what is run for its effects: a prelude's helpers, the analysis. A script whose
	// value is wanted, or whose exception is the author's and wants a map, is evaluated by
	// hand and its exception said with thrownText.
	bool evalScript(JSContext* ctx, const char* source, std::size_t size, const char* name,
	                std::string& error, const ThrownFormat& format = ThrownFormat());

	// How much stack a compile runs on, and how much of it QuickJS may use.
	//
	// acorn is a recursive-descent parser running inside an interpreter that recurses in C
	// for every JavaScript call, and in a Debug build each of those frames is large -- so
	// reading even a small bundle went through the main thread's one megabyte and took
	// the process down with it, before QuickJS's own limit, measured from wherever the
	// runtime happened to be created, had said anything. A thread of its own with room to
	// spare, and the limit set just under it, makes running out an error like any other.
	//
	// Every compile that evaluates something its author wrote is run this way -- a layout,
	// a theme, a mesh, state.ts -- because each of them can recurse, and a recursion that
	// ends in a crash with no message is the one failure that names nothing.
	inline constexpr std::size_t kCompileStack = std::size_t{ 64 } * 1024 * 1024;
	inline constexpr std::size_t kCompileJsStack = std::size_t{ 56 } * 1024 * 1024;

	// Runs `work` on a thread with kCompileStack of stack, and waits for it. Reserved, not
	// committed: an untouched page costs nothing. False, with `work` not run, if no thread
	// could be made.
	bool onLargeStack(const std::function<void()>& work);

	// A JavaScript runtime and one context in it, for as long as this lives.
	//
	// Whatever JSValue was made in the context has to be freed before this goes: QuickJS
	// asserts, in a build that asserts, on a runtime torn down with something still alive,
	// which is the engine's way of saying a compile that failed leaked.
	class JsContext {
	public:
		struct Options {
			// How much C stack QuickJS may use before it throws a stack overflow instead, or
			// zero for its own default. Only a compile that runs on a thread made for it, with
			// room to spare (onLargeStack, above), can promise more than that: the limit is
			// measured from wherever the runtime was created, and on the caller's stack there
			// is no knowing how much is left.
			std::size_t maxStack = 0;
		};

		JsContext();
		explicit JsContext(const Options& options);
		~JsContext();

		JsContext(const JsContext&) = delete;
		JsContext& operator=(const JsContext&) = delete;

		// False when there is no context to use, and failure() says which part could not be
		// made. Out of memory, in practice -- but a compile says so rather than carrying on.
		explicit operator bool() const { return mContext != nullptr; }
		const char* failure() const { return mFailure; }

		JSContext* get() const { return mContext; }

	private:
		JSRuntime*  mRuntime = nullptr;
		JSContext*  mContext = nullptr;
		const char* mFailure = nullptr;
	};
}
