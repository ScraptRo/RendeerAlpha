#include <Layout/Bindings.h>
#include <GraphicalObjects/Widget.h>
#include <Layout/ListView.h>
#include <Layout/ScriptHost.h>
#include <Core/Tables.h>
#include <Logger/Logger.h>

#include <algorithm>
#include <cmath>

namespace RDA::Layout {

	// Never destroyed, on purpose.
	//
	// An application holds its LayoutHost or Router as a global -- the documented shape,
	// and the natural one -- and a global is destroyed after main returns, in the same
	// static-destruction pass as this runtime. The order between the two is defined, and
	// wrong: a function-local static is destroyed in reverse order of construction, and
	// this one is first constructed inside onStart, so it dies *before* the application's
	// global. That global's destructor then releases its bindings into a vector that has
	// already been freed. MSVC's freed vector happens to read as empty; glibc's does not,
	// and the first Linux run of a real application segfaulted after its last frame with
	// a log that looked perfectly clean.
	//
	// One object of process lifetime, leaked at process exit, costs nothing -- and it
	// gives every path that reaches this table from a destructor the one property such a
	// table must have. signals(), commands() and tables() do the same, for the same
	// reason: release() reaches the first through this one.
	BindingRuntime& bindings() {
		static BindingRuntime* runtime = new BindingRuntime;
		return *runtime;
	}

	ObserverId BindingRuntime::add(Program program, std::vector<uint32_t> signalIds,
	                               Widget* target, std::string property, PropSetter setter) {
		Binding binding;
		binding.program   = std::move(program);
		binding.signalIds = std::move(signalIds);
		binding.target    = target;
		binding.property  = std::move(property);
		binding.setter    = setter;
		binding.live      = true;
		binding.script    = binding.program.usesScript();
		return place(std::move(binding));
	}

	ObserverId BindingRuntime::place(Binding binding) {
		// Ids are handed out densely and never reused, so Signals can index its own
		// bookkeeping by them without a map -- and it is Signals that hands them out, since
		// a computed signal observes too.
		const ObserverId id = signals().newObserver();
		if (id >= mBindings.size()) mBindings.resize(static_cast<size_t>(id) + 1);
		mBindings[id] = std::move(binding);
		++mLive;

		for (const uint32_t signal : mBindings[id].signalIds) {
			signals().observe(signal, id);
		}
		return id;
	}

	ObserverId BindingRuntime::addRowRefresh(std::vector<uint32_t> signalIds, ListView* list) {
		Binding binding;
		binding.signalIds = std::move(signalIds);
		binding.refresh   = list;
		binding.live      = true;
		return place(std::move(binding));
	}

	void BindingRuntime::watch(ObserverId observer, uint32_t signal) {
		if (observer >= mBindings.size() || !mBindings[observer].live) return;
		signals().observe(signal, observer);
	}

	void BindingRuntime::rewatch(ObserverId id, Binding& binding, const std::vector<uint32_t>& read,
	                             const std::vector<std::pair<uint32_t, uint64_t>>& tablesRead) {
		auto isStatic = [&](uint32_t s) {
			return std::find(binding.signalIds.begin(), binding.signalIds.end(), s) != binding.signalIds.end();
		};
		// Dropped: what it read last time and not this time -- a branch not taken now.
		for (const uint32_t old : binding.dynamic) {
			if (std::find(read.begin(), read.end(), old) == read.end() && !isStatic(old))
				signals().unobserve(old, id);
		}
		std::vector<uint32_t> now;
		for (const uint32_t s : read) {
			if (isStatic(s)) continue;
			now.push_back(s);
			signals().observe(s, id);   // a second edge to the same observer is not added
		}
		binding.dynamic = std::move(now);
		binding.tableReads = tablesRead;
		if (!binding.tableReads.empty() &&
		    std::find(mTableWatchers.begin(), mTableWatchers.end(), id) == mTableWatchers.end())
			mTableWatchers.push_back(id);
	}

	void BindingRuntime::remove(ObserverId observer) {
		if (observer >= mBindings.size() || !mBindings[observer].live) return;
		mTableWatchers.erase(std::remove(mTableWatchers.begin(), mTableWatchers.end(), observer),
		                     mTableWatchers.end());
		signals().forget(observer);
		mBindings[observer] = Binding{}; // frees the program; leaves the slot as a hole
		--mLive;
	}

	bool BindingRuntime::applyOne(Binding& binding) {
		if (!binding.live) return false;
		// The refresh kind evaluates nothing: the list re-binds its own rows, which is
		// where the row is known.
		if (binding.refresh) { binding.refresh->invalidateRows(); return true; }
		// Nothing is bound without a setter -- the loader says so when the layout loads --
		// so a value that arrives always has somewhere to go.
		if (!binding.target || !binding.setter) return false;

		if (binding.script) {
			// Watched while it runs: what the function reads is what this binding depends
			// on now, which may not be what it depended on last time.
			const ObserverId id = static_cast<ObserverId>(&binding - mBindings.data());
			const ScriptHost::Frame frame(ScriptHost::Mode::Value);
			const EvalResult result = evaluate(binding.program, signals(), binding.signalIds);
			rewatch(id, binding, frame.signals(), frame.tables());
			if (!result.ok) {
				// A function that threw is kept, with the last value it produced still on
				// screen: the next thing it reads may well be one it can handle.
				if (result.scriptError) {
					if (result.error != binding.lastError) {
						binding.lastError = result.error;
						RDA_LOG_WARNING("binding: " << binding.property << " on '"
						                << binding.target->id() << "' - " << result.error);
					}
					return false;
				}
				RDA_LOG_WARNING("binding: " << binding.property << " - " << result.error);
				binding.live = false;
				return false;
			}
			binding.lastError.clear();
			binding.setter(*binding.target, result.value);
			return true;
		}

		const EvalResult result = evaluate(binding.program, signals(), binding.signalIds);
		if (!result.ok) {
			RDA_LOG_WARNING("binding: " << binding.property << " - " << result.error);
			// Stopped rather than retried every frame: the program is wrong, and it will
			// still be wrong next frame.
			binding.live = false;
			return false;
		}
		binding.setter(*binding.target, result.value);
		return true;
	}

	bool BindingRuntime::applyNow(ObserverId observer) {
		if (observer >= mBindings.size()) return false;
		return applyOne(mBindings[observer]);
	}

	size_t BindingRuntime::applyAll() {
		size_t applied = 0;
		for (Binding& binding : mBindings) {
			if (applyOne(binding)) ++applied;
		}
		return applied;
	}

	size_t BindingRuntime::applyDirty() {
		// Bindings whose functions read a table: a table has no observers, so whether one
		// moved is asked here -- once, of the sum of every table's version, on a frame
		// where nothing changed, and binding by binding only when something did.
		std::vector<ObserverId> fromTables;
		if (!mTableWatchers.empty()) {
			const uint64_t revision = tables().revision();
			if (revision != mSeenTables) {
				mSeenTables = revision;
				for (const ObserverId id : mTableWatchers) {
					if (id >= mBindings.size() || !mBindings[id].live) continue;
					for (const auto& [table, version] : mBindings[id].tableReads) {
						if (!tables().valid(table) || tables().at(table).version() != version) {
							fromTables.push_back(id);
							break;
						}
					}
				}
			}
		}

		const std::vector<ObserverId>& dirty = signals().dirty();
		if (dirty.empty() && fromTables.empty()) return 0;

		size_t applied = 0;
		// Copied because applying can write a signal — a handler's effect landing on a
		// bound property — which would otherwise grow the list being walked.
		std::vector<ObserverId> pending = dirty;
		signals().clearDirty();
		for (const ObserverId id : fromTables) {
			if (std::find(pending.begin(), pending.end(), id) == pending.end()) pending.push_back(id);
		}

		for (const ObserverId observer : pending) {
			if (observer >= mBindings.size()) continue;
			if (applyOne(mBindings[observer])) ++applied;
		}
		return applied;
	}
}
