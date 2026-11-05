#include "JsContext.h"
#include "SourceMap.h"
#include <vendor/quickjs/quickjs.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace RDA::Layout {

	bool onLargeStack(const std::function<void()>& work) {
#if defined(_WIN32)
		struct Box { const std::function<void()>* work; };
		Box box{ &work };
		HANDLE thread = CreateThread(nullptr, kCompileStack,
			[](LPVOID raw) -> DWORD { (*static_cast<Box*>(raw)->work)(); return 0; },
			&box, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
		if (!thread) return false;
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
		return true;
#else
		pthread_attr_t attr;
		if (pthread_attr_init(&attr) != 0) return false;
		pthread_attr_setstacksize(&attr, kCompileStack);
		pthread_t thread;
		const bool started = pthread_create(&thread, &attr,
			[](void* raw) -> void* { (*static_cast<const std::function<void()>*>(raw))(); return nullptr; },
			const_cast<std::function<void()>*>(&work)) == 0;
		pthread_attr_destroy(&attr);
		if (!started) return false;
		pthread_join(thread, nullptr);
		return true;
#endif
	}

	JsContext::JsContext() : JsContext(Options()) {}

	JsContext::JsContext(const Options& options) {
		mRuntime = JS_NewRuntime();
		if (!mRuntime) { mFailure = "cannot create a JS runtime"; return; }
		if (options.maxStack != 0) JS_SetMaxStackSize(mRuntime, options.maxStack);
		mContext = JS_NewContext(mRuntime);
		if (!mContext) {
			JS_FreeRuntime(mRuntime);
			mRuntime = nullptr;
			mFailure = "cannot create a JS context";
		}
	}

	JsContext::~JsContext() {
		if (mContext) JS_FreeContext(mContext);
		if (mRuntime) JS_FreeRuntime(mRuntime);
	}

	std::string thrownText(JSContext* ctx) {
		return thrownText(ctx, ThrownFormat());
	}

	std::string thrownText(JSContext* ctx, const SourceMap& origins) {
		ThrownFormat format;
		format.origins = &origins;
		return thrownText(ctx, format);
	}

	std::string thrownText(JSContext* ctx, const ThrownFormat& format) {
		JSValue error = JS_GetException(ctx);
		const char* message = JS_ToCString(ctx, error);
		std::string text = message ? message : format.unprintable;
		if (message) JS_FreeCString(ctx, message);
		if (format.dropErrorPrefix && text.rfind("Error: ", 0) == 0) text.erase(0, 7);

		if (format.stack) {
			JSValue stack = JS_IsObject(error) ? JS_GetPropertyStr(ctx, error, "stack") : JS_UNDEFINED;
			if (JS_IsString(stack)) {
				const char* trace = JS_ToCString(ctx, stack);
				std::string lines = trace ? trace : "";
				if (trace) JS_FreeCString(ctx, trace);
				while (!lines.empty() && (lines.back() == '\n' || lines.back() == ' ')) lines.pop_back();
				if (!lines.empty()) text += "\n" + lines;
			}
			JS_FreeValue(ctx, stack);
		}
		JS_FreeValue(ctx, error);
		return format.origins ? format.origins->rewrite(text) : text;
	}

	bool evalScript(JSContext* ctx, const char* source, std::size_t size, const char* name,
	                std::string& error, const ThrownFormat& format) {
		JSValue result = JS_Eval(ctx, source, size, name, JS_EVAL_TYPE_GLOBAL);
		const bool ok = !JS_IsException(result);
		if (!ok) error = thrownText(ctx, format);
		JS_FreeValue(ctx, result);
		return ok;
	}
}
