#include <Scripting/Script.h>
#include <Logger/Logger.h>
#include <vendor/quickjs/quickjs.h>

// The embedded JavaScript runtime. This file is the only place that sees quickjs.h, so
// the rest of the engine never depends on the scripting implementation.
namespace RDA {

	namespace {
		// Turns a JSValue into text for display. Used for both results and errors, so a
		// value that cannot be stringified still produces something printable.
		std::string toString(JSContext* ctx, JSValueConst value) {
			const char* text = JS_ToCString(ctx, value);
			if (!text) return "<unprintable>";
			std::string result(text);
			JS_FreeCString(ctx, text);
			return result;
		}

		// The pending exception, as "message" plus its stack when there is one.
		std::string takeException(JSContext* ctx) {
			JSValue exception = JS_GetException(ctx);
			std::string text = toString(ctx, exception);

			// quickjs-ng dropped the context parameter this takes in Bellard's QuickJS.
			if (JS_IsError(exception)) {
				JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
				if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) {
					text += "\n" + toString(ctx, stack);
				}
				JS_FreeValue(ctx, stack);
			}
			JS_FreeValue(ctx, exception);
			return text;
		}

		// console.log(...): joins its arguments with spaces and hands them to the host.
		JSValue consoleLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			auto* engine = static_cast<ScriptEngine*>(JS_GetContextOpaque(ctx));
			std::string line;
			for (int i = 0; i < argc; ++i) {
				if (i) line += ' ';
				line += toString(ctx, argv[i]);
			}
			if (engine) engine->print(line);
			return JS_UNDEFINED;
		}
	}

	ScriptEngine::~ScriptEngine() {
		shutdown();
	}

	bool ScriptEngine::init() {
		if (mContext) return true;

		mRuntime = JS_NewRuntime();
		if (!mRuntime) {
			RDA_LOG_ERROR("Failed to create the JavaScript runtime");
			return false;
		}
		mContext = JS_NewContext(mRuntime);
		if (!mContext) {
			RDA_LOG_ERROR("Failed to create the JavaScript context");
			JS_FreeRuntime(mRuntime);
			mRuntime = nullptr;
			return false;
		}

		// So a C callback can find its way back to this object.
		JS_SetContextOpaque(mContext, this);

		// The only global the engine installs so far. Everything else a script can reach
		// will be added deliberately, one binding at a time.
		JSValue global = JS_GetGlobalObject(mContext);
		JSValue console = JS_NewObject(mContext);
		JS_SetPropertyStr(mContext, console, "log",
			JS_NewCFunction(mContext, consoleLog, "log", 1));
		JS_SetPropertyStr(mContext, global, "console", console);
		JS_FreeValue(mContext, global);

		RDA_LOG_SUCCES("JavaScript runtime initialized (QuickJS " << version() << ")");
		return true;
	}

	void ScriptEngine::shutdown() {
		if (mContext) {
			JS_FreeContext(mContext);
			mContext = nullptr;
		}
		if (mRuntime) {
			JS_FreeRuntime(mRuntime);
			mRuntime = nullptr;
		}
	}

	ScriptResult ScriptEngine::eval(const std::string& code, const std::string& name) {
		ScriptResult result;
		if (!mContext) {
			result.error = "the script engine is not initialized";
			return result;
		}

		JSValue value = JS_Eval(mContext, code.c_str(), code.size(), name.c_str(),
			JS_EVAL_TYPE_GLOBAL);

		if (JS_IsException(value)) {
			result.error = takeException(mContext);
			// Whatever it managed to ask for before it failed is thrown away. A script
			// that dies halfway used to leave half a scene behind; now it changes nothing,
			// which makes a failed run something you can simply fix and repeat.
			if (mCommit) mCommit(false);
		} else {
			result.ok = true;
			// Undefined is the usual completion value of a statement; reporting it as
			// empty keeps a console from printing "undefined" after every assignment.
			if (!JS_IsUndefined(value)) result.value = toString(mContext, value);
			// The script finished, so everything it asked for happens now, in order, and
			// as one step. This is the moment the queue exists to define.
			if (mCommit) result.commandsApplied = mCommit(true);
		}
		JS_FreeValue(mContext, value);
		return result;
	}

	const char* ScriptEngine::version() {
		return JS_GetVersion();
	}
}
