#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// State that a layout reads and an application writes.
//
// Signals live in C++, not in the scripting runtime. That is the decision the whole
// layout pipeline hangs off: because the value is here, a compiled binding can read it
// without a JavaScript engine in the process, which is what lets a shipped application
// carry no interpreter at all. Had they lived in JS, every application that showed a
// changing number would have had to carry one.
//
// Nothing here walks a tree or diffs anything. A write marks the observers of that one
// signal dirty and stops; whoever is responsible for observers drains the dirty list and
// re-evaluates exactly those. The cost of a change is proportional to what actually
// depends on it, not to the size of the interface.
//
// Types are deliberately few. A layout property is a number, a flag or some text; the
// moment a signal needs to be a struct, the thing that wants it is application state
// rather than something an interface binds to directly.
namespace RDA {

	enum class SignalType : uint8_t {
		Number,
		Bool,
		Text,
	};

	inline constexpr uint32_t kNoSignal = 0xFFFFFFFFu;

	// An identifier for something that depends on signals. The layout runtime uses one
	// per compiled binding; the table itself does not care what they mean, which keeps
	// it usable by anything that wants change notification.
	using ObserverId = uint32_t;
	inline constexpr ObserverId kNoObserver = 0xFFFFFFFFu;

	class Signals {
	public:
		// Creates the signal, or returns the existing one. Redeclaring with a different
		// type is refused rather than silently reinterpreted — a name meaning two things
		// is a bug that would otherwise surface as nonsense much later.
		uint32_t define(std::string_view name, double value);
		uint32_t define(std::string_view name, bool value);
		uint32_t define(std::string_view name, std::string_view value);

		uint32_t find(std::string_view name) const;
		size_t   count() const { return mSignals.size(); }

		SignalType       type(uint32_t signal) const;
		bool             valid(uint32_t signal) const { return signal < mSignals.size(); }

		double           number(uint32_t signal) const;
		bool             boolean(uint32_t signal) const;
		std::string_view text(uint32_t signal) const;

		// Writing marks observers dirty, but only when the value actually changed.
		// Assigning the same value again is not a change, and treating it as one is how
		// an interface ends up rebuilding itself every frame for no reason.
		void set(uint32_t signal, double value);
		void set(uint32_t signal, bool value);
		void set(uint32_t signal, std::string_view value);

		// ---- dependency graph ----
		// A new observer id. One space for every kind of observer -- a binding, a list's
		// rows, a computed signal -- handed out here because this is what indexes by them:
		// dense, in order, never reused. Not reset by clear(), since whoever holds an id
		// may outlive the signals it observed.
		ObserverId newObserver() { return mNextObserver++; }

		// Records that `observer` reads `signal`. Called once, when a binding is
		// instantiated, from the dependency list its compiler worked out.
		void observe(uint32_t signal, ObserverId observer);
		// Removes one edge. For a binding whose dependencies are recorded as it runs -- a
		// function that reads `a` on one branch and `b` on the other -- rather than worked
		// out once when it was compiled.
		void unobserve(uint32_t signal, ObserverId observer);
		// Drops every edge belonging to an observer, for when its widget goes away.
		void forget(ObserverId observer);

		// Observers whose signals changed since this was last drained, each appearing
		// once however many of its dependencies moved.
		const std::vector<ObserverId>& dirty() const { return mDirty; }
		void clearDirty();
		bool anyDirty() const { return !mDirty.empty(); }

		// Everything, for a fresh start. Does not notify: nobody is left to notify.
		void clear();

		// ---- computed signals ----
		// A signal state.ts computes. Nothing writes it but the engine, and the places a
		// write can come from -- a handler, run-time code, the C ABI -- ask this first and
		// refuse. set() itself does not, because the engine's own write goes through it.
		void setComputed(uint32_t signal, bool computed);
		bool computed(uint32_t signal) const {
			return valid(signal) && mSignals[signal].computed;
		}

		// An observer that runs inside the write rather than once a frame: a computed
		// signal, which has to be current the moment what it read changes, so a backend
		// that writes `price` and reads `total` on the next line reads the new total.
		//
		// Run after the write has finished marking, never in the middle of it, and only at
		// the outermost write: a recompute that writes its own signal queues whoever reads
		// that, and the same loop runs them, until nothing more moves. One that would go on
		// for ever -- two computed values that keep changing each other -- is stopped.
		void setEager(ObserverId observer, bool eager);
		void setEagerHandler(std::function<void(ObserverId)> handler) { mEagerHandler = std::move(handler); }

	private:
		struct Signal {
			std::string             name;
			SignalType              type = SignalType::Number;
			double                  number = 0.0;   // Number, and Bool as 0 or 1
			std::string             text;           // Text only
			std::vector<ObserverId> observers;
			bool                    computed = false;
		};

		void markDirty(const Signal& signal);
		void runEager();
		uint32_t declare(std::string_view name, SignalType type);

		std::vector<Signal>                          mSignals;
		std::unordered_map<std::string, uint32_t>    mByName;
		std::vector<ObserverId>                      mDirty;
		// Membership test for mDirty, so an observer that depends on three changed
		// signals is still queued once without scanning what is already there.
		std::vector<bool>                            mQueued;
		// Eager observers: which they are, those waiting to run, and whether the loop that
		// runs them is already going further up the stack.
		ObserverId                                   mNextObserver = 0;
		std::vector<bool>                            mEager;
		std::vector<ObserverId>                      mEagerQueue;
		bool                                         mRunningEager = false;
		std::function<void(ObserverId)>              mEagerHandler;
	};

	// The application's signals. One table per process, the way there is one scene.
	Signals& signals();
}
