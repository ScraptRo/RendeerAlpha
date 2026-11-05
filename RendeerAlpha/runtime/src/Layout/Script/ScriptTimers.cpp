#include "ScriptInternal.h"
#include <Logger/Logger.h>

#include <algorithm>

namespace RDA::Layout {
	using namespace ScriptInternal;

	namespace {
		// A ceiling, not a budget: an application with ten thousand timers waiting has one in
		// a loop that is never cleared, and saying so beats running out of memory slowly.
		constexpr size_t kMostTimers = 10000;

		void freeTimer(JSContext* ctx, Runtime::Timer& timer) {
			JS_FreeValue(ctx, timer.fn);
			JS_FreeValue(ctx, timer.args);
			timer.fn = JS_UNDEFINED;
			timer.args = JS_UNDEFINED;
		}
	}

	namespace ScriptInternal {
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

		void cancelTimers(uint32_t owner) {
			Runtime& r = runtime();
			for (auto it = r.timers.begin(); it != r.timers.end();) {
				if (it->owner == owner) { freeTimer(r.ctx, *it); it = r.timers.erase(it); }
				else ++it;
			}
		}

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

	double ScriptHost::secondsToNextTimer() const {
		const Runtime& r = runtime();
		if (r.timers.empty()) return -1.0;
		Clock::time_point next = r.timers.front().due;
		for (const Runtime::Timer& timer : r.timers) next = (std::min)(next, timer.due);
		const double seconds = std::chrono::duration<double>(next - Clock::now()).count();
		return seconds > 0.0 ? seconds : 0.0;
	}

	size_t ScriptHost::timerCount() const { return runtime().timers.size(); }

	size_t ScriptHost::timerCount(const ScriptModule& module) const {
		const std::vector<Runtime::Timer>& timers = runtime().timers;
		return static_cast<size_t>(std::count_if(timers.begin(), timers.end(), [&](const Runtime::Timer& timer) {
			return timer.owner == module.id();
		}));
	}
}
