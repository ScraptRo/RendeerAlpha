#include <Layout/ScriptHost.h>
#include <Layout/Blueprint.h>
#include <Core/Commands.h>
#include <Core/Signals.h>
#include <Core/Tables.h>
#include <Logger/Logger.h>
#include <vendor/quickjs/quickjs.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <unordered_set>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;

		// One callable thing: a function a module exports, or one of its bindings' factories.
		struct Entry {
			JSValue       fn = JS_UNDEFINED;
			ScriptModule* module = nullptr;
			bool          live = false;
		};

		// What a loaded module keeps in JavaScript.
		struct ModuleState {
			JSValue exports = JS_UNDEFINED;
			JSValue row = JS_UNDEFINED;   // __rdaRow(tableId, index): the `item` of a row template
		};

		// The runtime, and everything that has to outlive a call. Process lifetime and never
		// destroyed, for the reason bindings() is not: a global LayoutHost releases its
		// modules from its destructor, after a static here would already be gone.
		struct Runtime {
			JSRuntime*         rt = nullptr;
			JSContext*         ctx = nullptr;
			JSValue            natives = JS_UNDEFINED;
			std::vector<Entry> entries;
			int                depth = 0;          // calls in progress, for the deadline
			Clock::time_point  deadline{};
			bool               interrupted = false;
			double             budgetMs = 50.0;
			// The loaded state code's actions, by name: run-time code that calls one calls
			// it here, directly, so its arguments and its result stay JavaScript values.
			JSValue            actions = JS_UNDEFINED;
			// Every module loaded, by id, for what outlives a call: a timer's errors are told
			// through its module's line map, and its module's going is what cancels it.
			std::unordered_map<uint32_t, ScriptModule*> modules;
			uint32_t           nextModule = 0;
			// setTimeout and setInterval: what to call, when, and whose it is.
			struct Timer {
				uint32_t                  id = 0;
				uint32_t                  owner = 0;
				JSValue                   fn = JS_UNDEFINED;
				JSValue                   args = JS_UNDEFINED;
				Clock::time_point         due{};
				Clock::duration           period{};
				bool                      repeat = false;
			};
			std::vector<Timer> timers;
			uint32_t           nextTimer = 0;
		};

		// A ceiling, not a budget: an application with ten thousand timers waiting has one in
		// a loop that is never cleared, and saying so beats running out of memory slowly.
		constexpr size_t kMostTimers = 10000;

		Runtime& runtime() {
			static Runtime* one = new Runtime;
			return *one;
		}

		std::string toText(JSContext* ctx, JSValueConst value) {
			const char* raw = JS_ToCString(ctx, value);
			std::string out = raw ? raw : "";
			if (raw) JS_FreeCString(ctx, raw);
			return out;
		}

		JSValue toJS(JSContext* ctx, const Value& value) {
			switch (value.kind) {
			case Value::Kind::Number: return JS_NewFloat64(ctx, value.number);
			case Value::Kind::Bool:   return JS_NewBool(ctx, value.number != 0.0);
			case Value::Kind::Text:   return JS_NewStringLen(ctx, value.text.data(), value.text.size());
			}
			return JS_UNDEFINED;
		}

		// What a function returned, as one of the three things a property can be given.
		// Nothing is "", so a label shows nothing and a flag reads false.
		Value fromJS(JSContext* ctx, JSValueConst value) {
			if (JS_IsBool(value))   return Value::fromBool(JS_ToBool(ctx, value) == 1);
			if (JS_IsNumber(value)) { double n = 0.0; JS_ToFloat64(ctx, &n, value); return Value::fromNumber(n); }
			if (JS_IsNull(value) || JS_IsUndefined(value)) return Value::fromText(std::string());
			return Value::fromText(toText(ctx, value));
		}

		void recordSignal(uint32_t id) {
			if (ScriptHost::Frame* frame = scriptHost().frame()) frame->noteSignal(id);
		}

		void recordTable(uint32_t id) {
			if (ScriptHost::Frame* frame = scriptHost().frame()) frame->noteTable(id, tables().at(id).version());
		}

		bool readOnly() {
			ScriptHost::Frame* frame = scriptHost().frame();
			return frame && frame->mode() == ScriptHost::Mode::Value;
		}

		// A value as JSON, the way an action's result and a command's arguments cross the
		// boundary. Nothing JSON can say -- undefined, a function, a promise -- is null.
		std::string jsonOf(JSContext* ctx, JSValueConst value) {
			if (JS_IsUndefined(value) || JS_IsFunction(ctx, value) || JS_IsPromise(value) ||
			    JS_IsSymbol(value)) return "null";
			JSValue text = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
			if (JS_IsException(text)) {
				JS_FreeValue(ctx, JS_GetException(ctx));   // a cycle, a BigInt: said as null
				return "null";
			}
			std::string out = JS_IsString(text) ? toText(ctx, text) : std::string("null");
			JS_FreeValue(ctx, text);
			return out;
		}

		// The elements of a JavaScript array, owned by the caller.
		std::vector<JSValue> elementsOf(JSContext* ctx, JSValueConst list) {
			std::vector<JSValue> out;
			if (!JS_IsArray(list)) return out;
			int64_t length = 0;
			if (JS_GetLength(ctx, list, &length) < 0) { JS_FreeValue(ctx, JS_GetException(ctx)); return out; }
			for (int64_t i = 0; i < length && i < 255; ++i)
				out.push_back(JS_GetPropertyUint32(ctx, list, static_cast<uint32_t>(i)));
			return out;
		}

		// The engine's values a layout may also write; the rest of state.rda is a report.
		// The same list ExpressionParser keeps, for the same reason.
		bool engineWritable(std::string_view name) {
			return name == "rda.maximized" || name == "rda.minimized" ||
			       name == "rda.fullscreen" || name == "rda.open";
		}

		// ---- the natives: the one object a module is handed --------------------------

		JSValue nGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			const uint32_t id = signals().find(name);
			if (id == kNoSignal) {
				return JS_ThrowReferenceError(ctx, "state.%s is not declared -- nothing in state.ts, "
				                              "nor a signal() in the layout, names it", name.c_str());
			}
			recordSignal(id);
			switch (signals().type(id)) {
			case SignalType::Number: return JS_NewFloat64(ctx, signals().number(id));
			case SignalType::Bool:   return JS_NewBool(ctx, signals().boolean(id));
			case SignalType::Text: {
				const std::string_view text = signals().text(id);
				return JS_NewStringLen(ctx, text.data(), text.size());
			}
			}
			return JS_UNDEFINED;
		}

		JSValue nHas(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			return JS_NewBool(ctx, signals().find(name) != kNoSignal);
		}

		JSValue nSet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			if (readOnly()) {
				return JS_ThrowTypeError(ctx, "a value binding cannot write state.%s -- it runs whenever "
				                         "what it reads changes. Write it from an event handler", name.c_str());
			}
			if (name.rfind("rda.", 0) == 0 && !engineWritable(name)) {
				return JS_ThrowTypeError(ctx, "state.%s is the engine's, and read-only", name.c_str());
			}
			const uint32_t id = signals().find(name);
			if (id == kNoSignal) {
				return JS_ThrowReferenceError(ctx, "state.%s is not declared, so there is nothing to write",
				                              name.c_str());
			}
			if (signals().computed(id)) {
				return JS_ThrowTypeError(ctx, "state.%s is computed by state.ts, and nothing else writes it",
				                         name.c_str());
			}
			JSValueConst value = argc > 1 ? argv[1] : JS_UNDEFINED;
			// Through the signal's own type, as StoreSignal does: `state.count = "3"` is the
			// number three, not a signal that has stopped being a number.
			switch (signals().type(id)) {
			case SignalType::Number: {
				double n = 0.0;
				if (JS_ToFloat64(ctx, &n, value) < 0) return JS_EXCEPTION;
				signals().set(id, n);
				break;
			}
			case SignalType::Bool: signals().set(id, JS_ToBool(ctx, value) == 1); break;
			case SignalType::Text: signals().set(id, std::string_view(toText(ctx, value))); break;
			}
			return JS_UNDEFINED;
		}

		JSValue nCommand(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			if (readOnly()) {
				return JS_ThrowTypeError(ctx, "a value binding cannot call commands.%s -- it runs whenever "
				                         "what it reads changes. Call it from an event handler", name.c_str());
			}
			const uint32_t id = commands().find(name);
			if (id == kNoCommand) {
				return JS_ThrowReferenceError(ctx, "commands.%s is not a command the application declared",
				                              name.c_str());
			}
			// The arguments, as an array. A module compiled before commands took any passes
			// none, which is the same as an empty list.
			JSValueConst list = argc > 1 ? argv[1] : JS_UNDEFINED;
			Runtime& r = runtime();
			// An action: called directly, in this frame, so what it is given and what it
			// returns stay JavaScript values and a throw is the caller's to see.
			if (commands().hasAction(id) && JS_IsObject(r.actions)) {
				JSValue fn = JS_GetPropertyStr(ctx, r.actions, name.c_str());
				if (JS_IsFunction(ctx, fn)) {
					std::vector<JSValue> args = elementsOf(ctx, list);
					JSValue out = JS_Call(ctx, fn, JS_UNDEFINED, static_cast<int>(args.size()), args.data());
					for (JSValue& one : args) JS_FreeValue(ctx, one);
					JS_FreeValue(ctx, fn);
					if (JS_IsException(out)) return out;
					// A backend that binds an action watches it: told afterwards, with the
					// same arguments.
					if (commands().hasWork(id)) {
						commands().runWork(id, CommandArgs(JS_IsArray(list) ? jsonOf(ctx, list) : std::string("[]")));
					}
					return out;
				}
				JS_FreeValue(ctx, fn);
			}
			const CommandArgs args(JS_IsArray(list) ? jsonOf(ctx, list) : std::string("[]"));
			if (!commands().invoke(id, args)) {
				return JS_ThrowReferenceError(ctx, "nothing answers commands.%s -- no handler is bound "
				                              "to it, and state.ts does not answer it", name.c_str());
			}
			return JS_UNDEFINED;
		}

		// setTimeout and setInterval: (owner, fn, ms, repeat, args). The owner is the module
		// whose prelude asked, so the timer goes when that module does.
		JSValue nTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			if (readOnly()) {
				return JS_ThrowTypeError(ctx, "a value binding cannot start a timer -- it runs whenever what "
				                         "it reads changes. Start it from an event handler or an action");
			}
			if (argc < 2 || !JS_IsFunction(ctx, argv[1])) {
				return JS_ThrowTypeError(ctx, "a timer needs a function to call -- code is never compiled "
				                         "from text while the application runs");
			}
			Runtime& r = runtime();
			if (r.timers.size() >= kMostTimers) {
				return JS_ThrowRangeError(ctx, "%d timers are waiting at once -- one is started over and "
				                          "over and never cleared", static_cast<int>(kMostTimers));
			}
			int32_t owner = 0;
			if (argc > 0) JS_ToInt32(ctx, &owner, argv[0]);
			double ms = 0.0;
			if (argc > 2) JS_ToFloat64(ctx, &ms, argv[2]);
			if (!(ms >= 0.0)) ms = 0.0;                    // and NaN
			if (ms > 2147483647.0) ms = 2147483647.0;      // what a browser allows, about 24 days
			const bool repeat = argc > 3 && JS_ToBool(ctx, argv[3]) == 1;

			Runtime::Timer timer;
			timer.id = ++r.nextTimer;
			timer.owner = static_cast<uint32_t>(owner);
			timer.fn = JS_DupValue(ctx, argv[1]);
			timer.args = argc > 4 && JS_IsArray(argv[4]) ? JS_DupValue(ctx, argv[4]) : JS_NewArray(ctx);
			const auto wait = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms));
			timer.due = Clock::now() + wait;
			// An interval faster than a millisecond is one that runs every frame; asking the
			// loop to spin for it would cost a core and show nothing more.
			timer.period = repeat ? (std::max)(wait, std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(1)))
			                      : Clock::duration{};
			timer.repeat = repeat;
			r.timers.push_back(timer);
			return JS_NewInt64(ctx, timer.id);
		}

		void freeTimer(JSContext* ctx, Runtime::Timer& timer) {
			JS_FreeValue(ctx, timer.fn);
			JS_FreeValue(ctx, timer.args);
			timer.fn = JS_UNDEFINED;
			timer.args = JS_UNDEFINED;
		}

		JSValue nClearTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			int64_t id = 0;
			if (argc > 0) JS_ToInt64(ctx, &id, argv[0]);
			Runtime& r = runtime();
			for (auto it = r.timers.begin(); it != r.timers.end(); ++it) {
				if (it->id != static_cast<uint32_t>(id)) continue;
				freeTimer(ctx, *it);
				r.timers.erase(it);
				break;
			}
			return JS_UNDEFINED;
		}

		JSValue nTable(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			const std::string name = argc > 0 ? toText(ctx, argv[0]) : std::string();
			const uint32_t id = tables().find(name);
			return JS_NewInt32(ctx, id == kNoTable ? -1 : static_cast<int32_t>(id));
		}

		bool tableArg(JSContext* ctx, JSValueConst value, uint32_t& id) {
			int32_t raw = -1;
			if (JS_ToInt32(ctx, &raw, value) < 0 || raw < 0 || !tables().valid(static_cast<uint32_t>(raw)))
				return false;
			id = static_cast<uint32_t>(raw);
			return true;
		}

		JSValue nRows(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			uint32_t id = 0;
			if (argc < 1 || !tableArg(ctx, argv[0], id)) return JS_NewInt32(ctx, 0);
			recordTable(id);
			return JS_NewFloat64(ctx, static_cast<double>(tables().at(id).rows()));
		}

		JSValue nCell(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			uint32_t id = 0;
			if (argc < 3 || !tableArg(ctx, argv[0], id)) return JS_UNDEFINED;
			recordTable(id);
			const Table& table = tables().at(id);
			double rowNumber = -1.0;
			JS_ToFloat64(ctx, &rowNumber, argv[1]);
			if (rowNumber < 0.0 || rowNumber >= static_cast<double>(table.rows())) return JS_UNDEFINED;
			const size_t row = static_cast<size_t>(rowNumber);
			const uint32_t column = table.column(toText(ctx, argv[2]));
			if (column == kNoColumn) return JS_UNDEFINED;
			switch (table.columnType(column)) {
			case ColumnType::Number: return JS_NewFloat64(ctx, table.number(row, column));
			case ColumnType::Bool:   return JS_NewBool(ctx, table.boolean(row, column));
			case ColumnType::Text: {
				const std::string_view text = table.text(row, column);
				return JS_NewStringLen(ctx, text.data(), text.size());
			}
			}
			return JS_UNDEFINED;
		}

		JSValue nColumn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			uint32_t id = 0;
			if (argc < 2 || !tableArg(ctx, argv[0], id)) return JS_NewBool(ctx, false);
			return JS_NewBool(ctx, tables().at(id).column(toText(ctx, argv[1])) != kNoColumn);
		}

		JSValue nLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
			int32_t level = 0;
			if (argc > 0) JS_ToInt32(ctx, &level, argv[0]);
			const std::string line = argc > 1 ? toText(ctx, argv[1]) : std::string();
			// console.log is information, which the console shows only when asked to; a
			// warning or an error is shown whatever the level is.
			if (level >= 2)      RDA_LOG_ERROR("script: " << line);
			else if (level == 1) RDA_LOG_WARNING("script: " << line);
			else                 RDA_LOG_INFO("script: " << line);
			return JS_UNDEFINED;
		}

		// ---- the deadline --------------------------------------------------------------

		int interruptHandler(JSRuntime*, void*) {
			Runtime& r = runtime();
			if (r.depth > 0 && Clock::now() > r.deadline) {
				r.interrupted = true;
				return 1;
			}
			return 0;
		}

		struct Call {
			Runtime& r;
			explicit Call(Runtime& runtime) : r(runtime) {
				if (r.depth++ == 0) {
					r.interrupted = false;
					// Measured from here: the loop thread's stack is wherever this call
					// happens to be made from, and QuickJS checks its own depth against that.
					JS_UpdateStackTop(r.rt);
					r.deadline = Clock::now() +
					             std::chrono::microseconds(static_cast<int64_t>(r.budgetMs * 1000.0));
				}
			}
			~Call() {
				if (--r.depth == 0) {
					// Promises a call left behind settle now, so an async function finishes
					// inside the call that started it rather than at some later frame.
					JSContext* pending = nullptr;
					while (JS_ExecutePendingJob(r.rt, &pending) > 0) {}
				}
			}
		};

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

		const ModuleState& stateOf(const ScriptModule& module) {
			return *static_cast<const ModuleState*>(module.state());
		}

		// The two hooks the VM calls.
		bool hookCall(uint32_t id, const Value* args, size_t argc, Value& out, std::string& error) {
			return scriptHost().call(id, args, argc, out, error);
		}
		bool hookRun(uint32_t id, const RowRef* row, const Value* event, Value& out, std::string& error) {
			return scriptHost().run(id, row, event, out, error);
		}
		const ScriptHooks kHooks{ &hookCall, &hookRun };

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

			r.natives = JS_NewObject(r.ctx);
			const struct { const char* name; JSCFunction* fn; int length; } natives[] = {
				{ "get", &nGet, 1 }, { "has", &nHas, 1 }, { "set", &nSet, 2 },
				{ "command", &nCommand, 2 }, { "table", &nTable, 1 }, { "rows", &nRows, 1 },
				{ "cell", &nCell, 3 }, { "column", &nColumn, 2 }, { "log", &nLog, 2 },
				{ "timer", &nTimer, 5 }, { "clearTimer", &nClearTimer, 1 },
			};
			for (const auto& one : natives) {
				JS_SetPropertyStr(r.ctx, r.natives, one.name, JS_NewCFunction(r.ctx, one.fn, one.name, one.length));
			}

			if (const char* asked = std::getenv("RDA_SCRIPT_BUDGET_MS")) {
				const double ms = std::atof(asked);
				if (ms > 0.0) r.budgetMs = ms;
			}
			setScriptHooks(&kHooks);
			return true;
		}

		uint32_t addEntry(JSValue fn, ScriptModule* module) {
			Runtime& r = runtime();
			r.entries.push_back(Entry{ fn, module, true });
			return static_cast<uint32_t>(r.entries.size() - 1);
		}
	}

	// ---- ScriptModule --------------------------------------------------------------------

	ScriptModule::~ScriptModule() {
		Runtime& r = runtime();
		if (!r.ctx) return;
		r.modules.erase(mId);
		// Its timers go with it: a reloaded layout must not keep running the old one's.
		for (auto it = r.timers.begin(); it != r.timers.end();) {
			if (it->owner == mId) { freeTimer(r.ctx, *it); it = r.timers.erase(it); }
			else ++it;
		}
		auto release = [&](uint32_t id) {
			if (id >= r.entries.size() || !r.entries[id].live) return;
			JS_FreeValue(r.ctx, r.entries[id].fn);
			r.entries[id] = Entry{};
		};
		for (const auto& [name, id] : mFunctions) release(id);
		for (const uint32_t id : mBindings) release(id);
		if (mState) {
			ModuleState* state = static_cast<ModuleState*>(mState);
			JS_FreeValue(r.ctx, state->row);
			JS_FreeValue(r.ctx, state->exports);
			delete state;
		}
	}

	uint32_t ScriptModule::function(std::string_view name) const {
		const auto found = mFunctions.find(std::string(name));
		return found == mFunctions.end() ? kNoScript : found->second;
	}

	uint32_t ScriptModule::binding(uint32_t index) const {
		return index < mBindings.size() ? mBindings[index] : kNoScript;
	}

	// ---- ScriptHost ------------------------------------------------------------------------

	ScriptHost& scriptHost() {
		static ScriptHost* host = new ScriptHost;
		return *host;
	}

	ScriptHost::Frame::Frame(Mode mode) : mMode(mode) {
		ScriptHost& host = scriptHost();
		mOuter = host.mFrame;
		host.mFrame = this;
	}

	ScriptHost::Frame::~Frame() {
		scriptHost().mFrame = mOuter;
	}

	bool ScriptHost::running() const { return runtime().ctx != nullptr; }
	double ScriptHost::budgetMs() const { return runtime().budgetMs; }

	std::shared_ptr<ScriptModule> ScriptHost::load(const Blueprint& blueprint, const std::string& name,
	                                               std::string& error) {
		if (!blueprint.hasScript()) return nullptr;
		return loadModule(blueprint.script(), name, "rebuild the layout", error);
	}

	std::shared_ptr<ScriptModule> loadModule(const BlueprintScript& script, const std::string& name,
	                                         const char* remedy, std::string& error) {
		// Bytecode is QuickJS's own format, and changes between its releases. Refused rather
		// than read: what a mismatch does is undefined, and the fix is one rebuild.
		if (script.engine != JS_GetVersion()) {
			error = "its functions were compiled by QuickJS " + script.engine + " and this engine runs " +
			        JS_GetVersion() + " -- " + remedy;
			return nullptr;
		}
		if (!start(error)) return nullptr;
		Runtime& r = runtime();
		JSContext* ctx = r.ctx;

		std::shared_ptr<ScriptModule> module(new ScriptModule);
		module->mName = name;
		module->mId = ++r.nextModule;
		r.modules[module->mId] = module.get();
		{
			size_t from = 0;
			const std::string& map = script.lineMap;
			while (from <= map.size()) {
				const size_t end = map.find('\n', from);
				module->mLineMap.push_back(map.substr(from, end == std::string::npos ? std::string::npos : end - from));
				if (end == std::string::npos) break;
				from = end + 1;
			}
		}

		JSValue compiled = JS_ReadObject(ctx, script.code.data(), script.code.size(), JS_READ_OBJ_BYTECODE);
		if (JS_IsException(compiled)) {
			error = "its functions could not be read: " + describeException(ctx, module.get());
			return nullptr;
		}
		Call guard(r);
		JSValue wrapper = JS_EvalFunction(ctx, compiled);   // takes `compiled`
		if (JS_IsException(wrapper) || !JS_IsFunction(ctx, wrapper)) {
			error = "its functions could not be loaded: " + describeException(ctx, module.get());
			JS_FreeValue(ctx, wrapper);
			return nullptr;
		}
		// The natives, and which module this is -- what its timers are filed under.
		JSValue handed[2] = { r.natives, JS_NewInt64(ctx, module->mId) };
		JSValue exports = JS_Call(ctx, wrapper, JS_UNDEFINED, 2, handed);
		JS_FreeValue(ctx, wrapper);
		if (JS_IsException(exports)) {
			error = "its functions could not be started: " + describeException(ctx, module.get());
			return nullptr;
		}

		ModuleState* state = new ModuleState;
		state->exports = exports;
		state->row = JS_GetPropertyStr(ctx, exports, "row");
		module->mState = state;

		JSValue functions = JS_GetPropertyStr(ctx, exports, "f");
		JSPropertyEnum* names = nullptr;
		uint32_t count = 0;
		if (JS_IsObject(functions) &&
		    JS_GetOwnPropertyNames(ctx, &names, &count, functions, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
			for (uint32_t i = 0; i < count; ++i) {
				const char* key = JS_AtomToCString(ctx, names[i].atom);
				JSValue fn = JS_GetProperty(ctx, functions, names[i].atom);
				if (key && JS_IsFunction(ctx, fn)) module->mFunctions.emplace(key, addEntry(fn, module.get()));
				else JS_FreeValue(ctx, fn);
				if (key) JS_FreeCString(ctx, key);
			}
			JS_FreePropertyEnum(ctx, names, count);
		}
		JS_FreeValue(ctx, functions);

		JSValue bindings = JS_GetPropertyStr(ctx, exports, "b");
		uint32_t length = 0;
		JSValue lengthValue = JS_GetPropertyStr(ctx, bindings, "length");
		JS_ToUint32(ctx, &length, lengthValue);
		JS_FreeValue(ctx, lengthValue);
		for (uint32_t i = 0; i < length; ++i) {
			module->mBindings.push_back(addEntry(JS_GetPropertyUint32(ctx, bindings, i), module.get()));
		}
		JS_FreeValue(ctx, bindings);
		return module;
	}

	bool ScriptHost::call(uint32_t id, const Value* args, size_t argc, Value& out, std::string& error) {
		Runtime& r = runtime();
		if (!r.ctx || id >= r.entries.size() || !r.entries[id].live) {
			error = "the layout this function belonged to has been unloaded";
			return false;
		}
		JSContext* ctx = r.ctx;
		const Entry entry = r.entries[id];
		std::vector<JSValue> values;
		values.reserve(argc);
		for (size_t i = 0; i < argc; ++i) values.push_back(toJS(ctx, args[i]));

		Call guard(r);
		JSValue result = JS_Call(ctx, entry.fn, JS_UNDEFINED, static_cast<int>(argc), values.data());
		for (JSValue& one : values) JS_FreeValue(ctx, one);
		if (JS_IsException(result)) {
			error = describeException(ctx, entry.module);
			return false;
		}
		out = fromJS(ctx, result);
		JS_FreeValue(ctx, result);
		return true;
	}

	bool ScriptHost::run(uint32_t id, const RowRef* row, const Value* event, Value& out, std::string& error) {
		Runtime& r = runtime();
		if (!r.ctx || id >= r.entries.size() || !r.entries[id].live) {
			error = "the layout this binding belonged to has been unloaded";
			return false;
		}
		JSContext* ctx = r.ctx;
		const Entry entry = r.entries[id];

		Call guard(r);
		// The row, as the template's parameter: a view onto one row of the table, read
		// column by column as the binding asks.
		JSValue item = JS_UNDEFINED;
		if (row && row->table) {
			for (uint32_t t = 0; t < tables().count(); ++t) {
				if (&tables().at(t) != row->table) continue;
				JSValue args[2] = { JS_NewInt32(ctx, static_cast<int32_t>(t)),
				                    JS_NewFloat64(ctx, static_cast<double>(row->index)) };
				item = JS_Call(ctx, stateOf(*entry.module).row, JS_UNDEFINED, 2, args);
				break;
			}
			if (JS_IsException(item)) {
				error = describeException(ctx, entry.module);
				return false;
			}
		}
		JSValue binding = JS_Call(ctx, entry.fn, JS_UNDEFINED, 1, &item);
		JS_FreeValue(ctx, item);
		if (JS_IsException(binding)) {
			error = describeException(ctx, entry.module);
			return false;
		}
		JSValue passed = event ? toJS(ctx, *event) : JS_UNDEFINED;
		JSValue result = JS_Call(ctx, binding, JS_UNDEFINED, event ? 1 : 0, &passed);
		JS_FreeValue(ctx, passed);
		JS_FreeValue(ctx, binding);
		if (JS_IsException(result)) {
			error = describeException(ctx, entry.module);
			return false;
		}
		out = fromJS(ctx, result);
		JS_FreeValue(ctx, result);
		return true;
	}

	// ---- state.ts's code --------------------------------------------------------------
	//
	// One per process, like the signals it computes. A computed signal is an eager
	// observer: Signals runs it inside the write that changed what it read, so it is
	// current before the write returns. An action is a command's answer, given to the
	// registry; who calls the command never knows it is JavaScript.

	namespace {
		struct Computed {
			std::string           name;
			uint32_t              signal = kNoSignal;
			ObserverId            observer = kNoObserver;
			JSValue               fn = JS_UNDEFINED;
			std::vector<uint32_t> reads;    // what it read last time, and so observes
			std::vector<std::pair<uint32_t, uint64_t>> tables;
			std::string           lastError;
		};

		struct StateCode {
			std::shared_ptr<ScriptModule> module;
			std::vector<Computed>         computed;
			std::vector<uint32_t>         actions;   // commands given an answer
			std::unordered_map<ObserverId, size_t> byObserver;
			uint64_t                      seenTables = 0;
		};

		StateCode& stateCode() {
			static StateCode* one = new StateCode;
			return *one;
		}

		void writeSignal(uint32_t signal, const Value& value) {
			switch (signals().type(signal)) {
			case SignalType::Number: signals().set(signal, value.asNumber()); break;
			case SignalType::Bool:   signals().set(signal, value.truthy()); break;
			case SignalType::Text:   signals().set(signal, std::string_view(value.asText())); break;
			}
		}

		void recompute(size_t index) {
			Runtime& r = runtime();
			StateCode& s = stateCode();
			if (!r.ctx || index >= s.computed.size()) return;
			JSContext* ctx = r.ctx;

			std::vector<uint32_t> read;
			std::vector<std::pair<uint32_t, uint64_t>> tablesRead;
			bool ok = false;
			Value value;
			std::string error;
			{
				// A computed value reads: it may not write state or call a command, for the
				// reason a value binding may not -- it runs whenever what it read moves.
				const ScriptHost::Frame frame(ScriptHost::Mode::Value);
				Call guard(r);
				JSValue result = JS_Call(ctx, s.computed[index].fn, JS_UNDEFINED, 0, nullptr);
				if (JS_IsException(result)) {
					error = describeException(ctx, s.module.get());
				} else {
					value = fromJS(ctx, result);
					ok = true;
				}
				JS_FreeValue(ctx, result);
				read = frame.signals();
				tablesRead = frame.tables();
			}

			Computed& c = s.computed[index];
			// What it reads now is what it follows. Not itself: a value made from its own
			// last value would recompute for ever, and is a mistake to say rather than run.
			const auto self = std::find(read.begin(), read.end(), c.signal);
			if (self != read.end()) {
				read.erase(self);
				if (ok) {
					ok = false;
					error = "it reads state." + c.name + " -- itself -- and a value made from its own "
					        "last value never settles";
				}
			}
			for (const uint32_t old : c.reads) {
				if (std::find(read.begin(), read.end(), old) == read.end()) signals().unobserve(old, c.observer);
			}
			for (const uint32_t now : read) signals().observe(now, c.observer);
			c.reads = std::move(read);
			c.tables = std::move(tablesRead);

			if (!ok) {
				// Kept at its last value, and said once per message: the next thing it reads
				// may well be one it can handle.
				if (error != c.lastError) {
					c.lastError = error;
					RDA_LOG_WARNING("state: " << c.name << " - " << error);
				}
				return;
			}
			c.lastError.clear();
			// Written last, and through set(), which may run other computed values that read
			// this one -- so nothing of `c` is touched after it.
			writeSignal(c.signal, value);
		}

		void onEager(ObserverId observer) {
			const StateCode& s = stateCode();
			const auto found = s.byObserver.find(observer);
			if (found != s.byObserver.end()) recompute(found->second);
		}

		// The answer an action gives a command: arguments in as JSON, result out as JSON.
		bool runAction(const std::string& name, const CommandArgs& args, std::string& result,
		               std::string& error) {
			Runtime& r = runtime();
			StateCode& s = stateCode();
			result = "null";
			if (!r.ctx || !JS_IsObject(r.actions)) {
				error = "state.ts's code is not loaded";
				return false;
			}
			JSContext* ctx = r.ctx;
			const ScriptHost::Frame frame(ScriptHost::Mode::Handler);
			Call guard(r);
			JSValue fn = JS_GetPropertyStr(ctx, r.actions, name.c_str());
			const std::string& json = args.json();
			JSValue list = JS_ParseJSON(ctx, json.c_str(), json.size(), "<arguments>");
			if (JS_IsException(list)) {
				error = "its arguments are not a JSON array: " + describeException(ctx, nullptr);
				JS_FreeValue(ctx, fn);
				return false;
			}
			std::vector<JSValue> values = elementsOf(ctx, list);
			JS_FreeValue(ctx, list);
			JSValue out = JS_Call(ctx, fn, JS_UNDEFINED, static_cast<int>(values.size()), values.data());
			for (JSValue& one : values) JS_FreeValue(ctx, one);
			JS_FreeValue(ctx, fn);
			if (JS_IsException(out)) {
				error = describeException(ctx, s.module.get());
				RDA_LOG_WARNING("state: commands." << name << " - " << error);
				return false;
			}
			result = jsonOf(ctx, out);
			JS_FreeValue(ctx, out);
			return true;
		}

		// The names of an object's own properties, in the order they were written.
		std::vector<std::string> keysOf(JSContext* ctx, JSValueConst object) {
			std::vector<std::string> out;
			JSPropertyEnum* names = nullptr;
			uint32_t count = 0;
			if (!JS_IsObject(object) ||
			    JS_GetOwnPropertyNames(ctx, &names, &count, object, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0)
				return out;
			for (uint32_t i = 0; i < count; ++i) {
				const char* key = JS_AtomToCString(ctx, names[i].atom);
				if (key) { out.emplace_back(key); JS_FreeCString(ctx, key); }
			}
			JS_FreePropertyEnum(ctx, names, count);
			return out;
		}
	}

	bool ScriptHost::loadState(const uint8_t* bytes, size_t size, std::string& error) {
		BlueprintScript script;
		if (!readScriptSection(bytes, size, script, error)) {
			error = "state.ts's code: " + error;
			return false;
		}
		return loadState(script, error);
	}

	bool ScriptHost::loadState(const BlueprintScript& script, std::string& error) {
		// The new code first, and only then the old one away: a reload that does not load
		// leaves what was running running.
		std::shared_ptr<ScriptModule> module =
			loadModule(script, "state", "regenerate the state module (rda build)", error);
		if (!module) {
			if (error.empty()) error = "state.ts's code could not be loaded";
			return false;
		}
		unloadState();

		Runtime& r = runtime();
		JSContext* ctx = r.ctx;
		StateCode& s = stateCode();
		s.module = module;
		const JSValue exports = stateOf(*module).exports;

		JSValue computed = JS_GetPropertyStr(ctx, exports, "c");
		for (const std::string& name : keysOf(ctx, computed)) {
			const uint32_t signal = signals().find(name);
			if (signal == kNoSignal) {
				// The declaration grew since the backend was generated: it has no signal to
				// put the value in. Said rather than half done.
				RDA_LOG_WARNING("state: state." << name << " is computed by state.ts, and the "
				                "application never defined it -- regenerate its state module");
				continue;
			}
			Computed one;
			one.name = name;
			one.signal = signal;
			one.fn = JS_GetPropertyStr(ctx, computed, name.c_str());
			one.observer = signals().newObserver();
			signals().setComputed(signal, true);
			signals().setEager(one.observer, true);
			s.byObserver.emplace(one.observer, s.computed.size());
			s.computed.push_back(std::move(one));
		}
		JS_FreeValue(ctx, computed);

		JSValue actions = JS_GetPropertyStr(ctx, exports, "a");
		for (const std::string& name : keysOf(ctx, actions)) {
			// An action needs nothing from a backend, so one the backend never declared is
			// declared here rather than refused.
			const uint32_t command = commands().define(name);
			commands().setAction(command, [name](const CommandArgs& args, std::string& result,
			                                     std::string& why) {
				return runAction(name, args, result, why);
			});
			s.actions.push_back(command);
		}
		r.actions = actions;   // owned from here; freed by unloadState

		signals().setEagerHandler(&onEager);
		s.seenTables = tables().revision();
		// Worked out now, in the order they were declared. One that reads another not yet
		// worked out reads its starting value, and is run again when that one is written.
		for (size_t i = 0; i < s.computed.size(); ++i) recompute(i);
		return true;
	}

	void ScriptHost::unloadState() {
		Runtime& r = runtime();
		StateCode& s = stateCode();
		for (Computed& one : s.computed) {
			signals().setComputed(one.signal, false);
			signals().forget(one.observer);   // its edges, and its place in the queue
			if (r.ctx) JS_FreeValue(r.ctx, one.fn);
		}
		s.computed.clear();
		s.byObserver.clear();
		for (const uint32_t command : s.actions) commands().setAction(command, nullptr);
		s.actions.clear();
		if (r.ctx) JS_FreeValue(r.ctx, r.actions);
		r.actions = JS_UNDEFINED;
		s.module.reset();
	}

	bool ScriptHost::stateLoaded() const { return static_cast<bool>(stateCode().module); }

	namespace {
		// The timers that are due, in the order they fell due. Each goes off once a tick,
		// however late the tick is; one started while these run waits for the next.
		void fireTimers() {
			Runtime& r = runtime();
			if (!r.ctx || r.timers.empty()) return;
			JSContext* ctx = r.ctx;
			const Clock::time_point now = Clock::now();
			std::vector<std::pair<Clock::time_point, uint32_t>> due;
			for (const Runtime::Timer& timer : r.timers) {
				if (timer.due <= now) due.emplace_back(timer.due, timer.id);
			}
			if (due.empty()) return;
			std::sort(due.begin(), due.end());

			for (const auto& one : due) {
				// Looked up again each time: the one before may have cleared it, or started
				// others, which moves the list under any reference held across the call.
				auto found = std::find_if(r.timers.begin(), r.timers.end(),
				                          [&](const Runtime::Timer& t) { return t.id == one.second; });
				if (found == r.timers.end()) continue;
				JSValue fn = JS_DupValue(ctx, found->fn);
				JSValue args = JS_DupValue(ctx, found->args);
				const uint32_t owner = found->owner;
				if (found->repeat) {
					// From when it was due rather than from now, so an interval keeps its beat;
					// but never into the past, so a loop that slept does not fire it in a burst.
					found->due += found->period;
					if (found->due <= now) found->due = now + found->period;
				} else {
					freeTimer(ctx, *found);
					r.timers.erase(found);
				}

				const auto module = r.modules.find(owner);
				const ScriptModule* source = module == r.modules.end() ? nullptr : module->second;
				{
					// What a handler may do: a timer is something that happened.
					const ScriptHost::Frame frame(ScriptHost::Mode::Handler);
					Call guard(r);
					std::vector<JSValue> values = elementsOf(ctx, args);
					JSValue out = JS_Call(ctx, fn, JS_UNDEFINED, static_cast<int>(values.size()), values.data());
					for (JSValue& value : values) JS_FreeValue(ctx, value);
					if (JS_IsException(out)) RDA_LOG_WARNING("timer: " << describeException(ctx, source));
					JS_FreeValue(ctx, out);
				}
				JS_FreeValue(ctx, fn);
				JS_FreeValue(ctx, args);
			}
		}

	}

	void ScriptHost::tick() {
		fireTimers();
		StateCode& s = stateCode();
		if (s.computed.empty()) return;
		// One sum for every table, so a frame where none moved costs one comparison.
		const uint64_t revision = tables().revision();
		if (revision == s.seenTables) return;
		s.seenTables = revision;
		for (size_t i = 0; i < s.computed.size(); ++i) {
			for (const auto& [table, version] : s.computed[i].tables) {
				if (!tables().valid(table) || tables().at(table).version() != version) {
					recompute(i);
					break;
				}
			}
		}
	}

	double ScriptHost::secondsToNextTimer() const {
		const Runtime& r = runtime();
		if (r.timers.empty()) return -1.0;
		Clock::time_point next = r.timers.front().due;
		for (const Runtime::Timer& timer : r.timers) next = (std::min)(next, timer.due);
		const double seconds = std::chrono::duration<double>(next - Clock::now()).count();
		return seconds > 0.0 ? seconds : 0.0;
	}

	size_t ScriptHost::timerCount() const { return runtime().timers.size(); }
}
