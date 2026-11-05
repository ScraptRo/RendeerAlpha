#include "ScriptInternal.h"
#include <Layout/Blueprint.h>
#include <Core/Commands.h>
#include <Core/Signals.h>
#include <Core/Tables.h>
#include <Logger/Logger.h>

#include <algorithm>
#include <utility>

namespace RDA::Layout {
	using namespace ScriptInternal;

	// ---- state.ts's code --------------------------------------------------------------
	//
	// One per process, like the signals it computes. A computed signal is an eager
	// observer: Signals runs it inside the write that changed what it read, so it is
	// current before the write returns. An action is a command's answer, given to the
	// registry; who calls the command never knows it is JavaScript. A watch is a handler
	// whose event is a signal changing, run from tick() once the writes have finished --
	// see docs/architecture/watch-and-on-open.md.

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

		// Asked once a turn through the signal's version, rather than observing it: an
		// observer runs inside the write or is drained by the bindings, and this has to run
		// after the one and before the other.
		struct Watch {
			std::string name;
			uint32_t    signal = kNoSignal;
			JSValue     fn = JS_UNDEFINED;
			uint64_t    seen = 0;   // the version last looked at
			Value       last;       // what it held then: `was`, the next time it moves
			std::string lastError;
		};

		struct StateCode {
			std::shared_ptr<ScriptModule> module;
			std::vector<Computed>         computed;
			std::vector<Watch>            watches;
			std::string                   lastRunaway;
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

		Value readSignal(uint32_t signal) {
			switch (signals().type(signal)) {
			case SignalType::Bool: return Value::fromBool(signals().boolean(signal));
			case SignalType::Text: return Value::fromText(std::string(signals().text(signal)));
			case SignalType::Number: break;
			}
			return Value::fromNumber(signals().number(signal));
		}

		// A signal keeps its type, so the two are always the same kind.
		bool sameValue(const Value& a, const Value& b) {
			return a.kind == Value::Kind::Text ? a.text == b.text : a.number == b.number;
		}

		void settle(Watch& watch) {
			watch.seen = signals().version(watch.signal);
			watch.last = readSignal(watch.signal);
		}

		// Whether it ran.
		bool runWatch(size_t index) {
			Runtime& r = runtime();
			StateCode& s = stateCode();
			Watch& watch = s.watches[index];
			const uint64_t version = signals().version(watch.signal);
			if (version == watch.seen) return false;
			watch.seen = version;
			// A value that went somewhere and came back between two looks did not change.
			const Value now = readSignal(watch.signal);
			if (sameValue(now, watch.last)) return false;
			const Value was = std::exchange(watch.last, now);

			JSContext* ctx = r.ctx;
			JSValue fn = JS_DupValue(ctx, watch.fn);
			std::string error;
			{
				// What a handler may do: a value changing is something that happened.
				const ScriptHost::Frame frame(ScriptHost::Mode::Handler);
				Call guard(r);
				JSValue args[2] = { toJS(ctx, now), toJS(ctx, was) };
				JSValue out = JS_Call(ctx, fn, JS_UNDEFINED, 2, args);
				if (JS_IsException(out)) error = describeException(ctx, s.module.get());
				JS_FreeValue(ctx, out);
				for (JSValue& one : args) JS_FreeValue(ctx, one);
			}
			JS_FreeValue(ctx, fn);

			// Looked up again: a command it called may have reloaded the state code.
			if (index >= s.watches.size()) return true;
			Watch& after = s.watches[index];
			// Said once per message, as a computed value's error is.
			if (!error.empty() && error != after.lastError) {
				RDA_LOG_WARNING("state: " << after.name << "'s watch - " << error);
			}
			after.lastError = std::move(error);
			return true;
		}

		// Repeated while anything moved, because a watch may write what another watches, or
		// what it watches itself: a clamp is that, and settles on its second call.
		void runWatches() {
			Runtime& r = runtime();
			StateCode& s = stateCode();
			if (!r.ctx || s.watches.empty()) return;

			constexpr int kMostPasses = 64;
			std::string moved;
			for (int pass = 0; pass < kMostPasses; ++pass) {
				moved.clear();
				for (size_t i = 0; i < s.watches.size(); ++i) {
					if (!runWatch(i) || i >= s.watches.size()) continue;
					if (!moved.empty()) moved += ", ";
					moved += "state." + s.watches[i].name;
				}
				if (moved.empty()) return;
			}

			if (moved != s.lastRunaway) {
				s.lastRunaway = moved;
				RDA_LOG_ERROR("state: watches keep changing what they watch and were stopped after "
				              << kMostPasses << " passes: " << moved);
			}
			// Taken as settled. Left as they are, the next turn would only start this again,
			// and a value that climbs for ever is what a watch is here to prevent.
			for (Watch& one : s.watches) settle(one);
		}

		// A table has no observers, so a computed signal that read one is asked here.
		void followTables() {
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

		JSValue watches = JS_GetPropertyStr(ctx, exports, "w");
		for (const std::string& name : keysOf(ctx, watches)) {
			const uint32_t signal = signals().find(name);
			if (signal == kNoSignal) {
				RDA_LOG_WARNING("state: state." << name << " is watched by state.ts, and the "
				                "application never defined it -- regenerate its state module");
				continue;
			}
			Watch one;
			one.name = name;
			one.signal = signal;
			one.fn = JS_GetPropertyStr(ctx, watches, name.c_str());
			s.watches.push_back(std::move(one));
		}
		JS_FreeValue(ctx, watches);

		signals().setEagerHandler(&onEager);
		s.seenTables = tables().revision();
		// Worked out now, in the order they were declared. One that reads another not yet
		// worked out reads its starting value, and is run again when that one is written.
		for (size_t i = 0; i < s.computed.size(); ++i) recompute(i);
		// After those: a starting value is not a change, and that includes a computed one.
		for (Watch& one : s.watches) settle(one);
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
		if (r.ctx) for (Watch& one : s.watches) JS_FreeValue(r.ctx, one.fn);
		s.watches.clear();
		s.lastRunaway.clear();
		for (const uint32_t command : s.actions) commands().setAction(command, nullptr);
		s.actions.clear();
		if (r.ctx) JS_FreeValue(r.ctx, r.actions);
		r.actions = JS_UNDEFINED;
		s.module.reset();
	}

	bool ScriptHost::stateLoaded() const { return static_cast<bool>(stateCode().module); }

	void ScriptHost::tick() {
		fireTimers();
		followTables();
		// Last, so a watch hears what those two wrote. The bindings run after this, so what
		// a watch writes reaches the screen in this same frame.
		runWatches();
	}
}
