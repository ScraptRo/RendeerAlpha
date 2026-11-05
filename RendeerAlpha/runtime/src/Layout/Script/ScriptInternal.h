#pragma once
#include <Layout/ScriptHost.h>
#include <vendor/quickjs/quickjs.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// What the files of the script host share, and nothing outside src/Layout/Script/ may
// include. ScriptHost.h is the host's whole public face; this is its inside: the runtime
// and the records that live in it, and the few helpers more than one file reaches for.
// Each helper is declared here and defined once, in the file named beside it.
//
// The files, by concern:
//   ScriptRuntime.cpp   the runtime and its context, the deadline, exceptions as text
//   ScriptValues.cpp    values crossing between JavaScript and the engine
//   ScriptFrame.cpp     what a running function read, and whether it may write
//   ScriptNatives.cpp   the object of natives a module is handed
//   ScriptTimers.cpp    setTimeout and setInterval, and the queue they feed
//   ScriptModules.cpp   a blueprint's functions, loaded; calling one
//   ScriptState.cpp     state.ts's computed values and actions
//
// Before the split these were one anonymous namespace in one file. They are in a named
// namespace now, so that Entry, Call or Runtime can never meet another file's of the same
// name and be merged by the linker.
namespace RDA::Layout {
	namespace ScriptInternal {
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

		inline const ModuleState& stateOf(const ScriptModule& module) {
			return *static_cast<const ModuleState*>(module.state());
		}

		// ---- ScriptRuntime.cpp ------------------------------------------------------------

		Runtime& runtime();
		// Brings the runtime and its context up the first time; true once they are.
		bool start(std::string& error);
		// The exception pending on the context, as text, its lines mapped back through the
		// module's line map.
		std::string describeException(JSContext* ctx, const ScriptModule* module);

		// ---- ScriptValues.cpp -------------------------------------------------------------

		std::string toText(JSContext* ctx, JSValueConst value);
		JSValue toJS(JSContext* ctx, const Value& value);
		Value fromJS(JSContext* ctx, JSValueConst value);
		std::string jsonOf(JSContext* ctx, JSValueConst value);
		std::vector<JSValue> elementsOf(JSContext* ctx, JSValueConst list);

		// ---- ScriptFrame.cpp --------------------------------------------------------------

		void recordSignal(uint32_t id);
		void recordTable(uint32_t id);
		bool readOnly();

		// ---- ScriptNatives.cpp ------------------------------------------------------------

		// Builds Runtime::natives, the object a module is handed as `__rda`.
		void installNatives(Runtime& r);

		// ---- ScriptTimers.cpp -------------------------------------------------------------

		// Natives as well, but they are the timer queue's, so they are defined with it and
		// installed by installNatives.
		JSValue nTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv);
		JSValue nClearTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv);
		// Every timer a module started, gone.
		void cancelTimers(uint32_t owner);
		// The timers that are due go off. Called by ScriptHost::tick.
		void fireTimers();
	}
}
