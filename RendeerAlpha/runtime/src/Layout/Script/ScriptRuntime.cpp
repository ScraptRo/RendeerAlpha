#include "ScriptInternal.h"

#include <cstdlib>

namespace RDA::Layout {
	using namespace ScriptInternal;

	namespace {
		// ---- the deadline --------------------------------------------------------------

		int interruptHandler(JSRuntime*, void*) {
			Runtime& r = runtime();
			if (r.depth > 0 && Clock::now() > r.deadline) {
				r.interrupted = true;
				return 1;
			}
			return 0;
		}

		// "rda:main:12:5" -> "main.tsx:30", through the module's line map.
		std::string mapLines(const std::string& text, const std::vector<std::string>& lineMap) {
			std::string out;
			size_t at = 0;
			for (;;) {
				const size_t found = text.find("rda:", at);
				if (found == std::string::npos) { out += text.substr(at); break; }
				out += text.substr(at, found - at);
				size_t cursor = found + 4;
				while (cursor < text.size() && text[cursor] != ':' && text[cursor] != ')' &&
				       text[cursor] != '\n') ++cursor;
				size_t digits = cursor + 1;
				size_t line = 0;
				bool any = false;
				while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
					line = line * 10 + static_cast<size_t>(text[digits] - '0');
					++digits;
					any = true;
				}
				if (cursor >= text.size() || text[cursor] != ':' || !any) {
					out += text.substr(found, cursor - found);
					at = cursor;
					continue;
				}
				// The column is of the generated module, and means nothing in the source.
				size_t end = digits;
				if (end < text.size() && text[end] == ':') {
					++end;
					while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
				}
				const std::string& mapped = (line >= 1 && line <= lineMap.size()) ? lineMap[line - 1]
				                                                                   : std::string();
				out += mapped.empty() ? std::string("the engine's own code") : mapped;
				at = end;
			}
			return out;
		}

		// The two hooks the VM calls.
		bool hookCall(uint32_t id, const Value* args, size_t argc, Value& out, std::string& error) {
			return scriptHost().call(id, args, argc, out, error);
		}
		bool hookRun(uint32_t id, const RowRef* row, const Value* event, Value& out, std::string& error) {
			return scriptHost().run(id, row, event, out, error);
		}
		const ScriptHooks kHooks{ &hookCall, &hookRun };
	}

	namespace ScriptInternal {
		Runtime& runtime() {
			static Runtime* one = new Runtime;
			return *one;
		}

		std::string describeException(JSContext* ctx, const ScriptModule* module) {
			Runtime& r = runtime();
			JSValue error = JS_GetException(ctx);
			std::string message;
			if (r.interrupted) {
				message = "took longer than " + std::to_string(static_cast<int>(r.budgetMs)) +
				          " ms and was stopped -- a loop that does not end, or work that belongs "
				          "in the backend";
			} else {
				message = toText(ctx, error);
			}
			if (JS_IsError(error)) {
				JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
				if (JS_IsString(stack)) {
					std::string trace = toText(ctx, stack);
					while (!trace.empty() && (trace.back() == '\n' || trace.back() == ' ')) trace.pop_back();
					if (!trace.empty()) message += "\n" + trace;
				}
				JS_FreeValue(ctx, stack);
			}
			JS_FreeValue(ctx, error);
			static const std::vector<std::string> none;
			return mapLines(message, module ? module->lineMap() : none);
		}

		bool start(std::string& error) {
			Runtime& r = runtime();
			if (r.ctx) return true;

			r.rt = JS_NewRuntime();
			if (!r.rt) { error = "cannot create the JavaScript runtime"; return false; }
			// Generous for an interface, and a ceiling rather than a reservation: a function
			// that allocates without end is stopped by this rather than by the machine.
			JS_SetMemoryLimit(r.rt, size_t{ 256 } * 1024 * 1024);
			JS_SetMaxStackSize(r.rt, size_t{ 512 } * 1024);
			JS_SetInterruptHandler(r.rt, &interruptHandler, nullptr);

			// The language and nothing else: no eval, so nothing can be compiled from text
			// at run time, and no file, process or network, so nothing reaches past the
			// interface. What the module needs of the engine it is handed as `__rda`.
			r.ctx = JS_NewContextRaw(r.rt);
			if (!r.ctx) { JS_FreeRuntime(r.rt); r.rt = nullptr; error = "cannot create a JavaScript context"; return false; }
			JS_AddIntrinsicBaseObjects(r.ctx);
			JS_AddIntrinsicDate(r.ctx);
			JS_AddIntrinsicRegExpCompiler(r.ctx);
			JS_AddIntrinsicRegExp(r.ctx);
			JS_AddIntrinsicJSON(r.ctx);
			JS_AddIntrinsicProxy(r.ctx);
			JS_AddIntrinsicMapSet(r.ctx);
			JS_AddIntrinsicTypedArrays(r.ctx);
			JS_AddIntrinsicPromise(r.ctx);
			JS_AddIntrinsicBigInt(r.ctx);
			JS_AddIntrinsicWeakRef(r.ctx);

			installNatives(r);

			if (const char* asked = std::getenv("RDA_SCRIPT_BUDGET_MS")) {
				const double ms = std::atof(asked);
				if (ms > 0.0) r.budgetMs = ms;
			}
			setScriptHooks(&kHooks);
			return true;
		}
	}

	// ---- ScriptHost ------------------------------------------------------------------------

	ScriptHost& scriptHost() {
		static ScriptHost* host = new ScriptHost;
		return *host;
	}

	bool ScriptHost::running() const { return runtime().ctx != nullptr; }
	double ScriptHost::budgetMs() const { return runtime().budgetMs; }
}
