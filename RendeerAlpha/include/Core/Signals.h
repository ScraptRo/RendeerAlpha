#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// State that a layout reads and an application writes.
//
// Signals live in C++, not in the scripting runtime. That is the decision the whole
// layout pipeline hangs off: because the value is here, a compiled binding can read it
// without a JavaScript engine in the process, which is what lets an App-tier build ship
// with no interpreter at all. Had they lived in JS, every application that showed a
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

		std::string_view name(uint32_t signal) const;
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
		// Records that `observer` reads `signal`. Called once, when a binding is
		// instantiated, from the dependency list its compiler worked out.
		void observe(uint32_t signal, ObserverId observer);
		// Drops every edge belonging to an observer, for when its widget goes away.
		void forget(ObserverId observer);

		// Observers whose signals changed since this was last drained, each appearing
		// once however many of its dependencies moved.
		const std::vector<ObserverId>& dirty() const { return mDirty; }
		void clearDirty();
		bool anyDirty() const { return !mDirty.empty(); }

		// Everything, for a fresh start. Does not notify: nobody is left to notify.
		void clear();

	private:
		struct Signal {
			std::string             name;
			SignalType              type = SignalType::Number;
			double                  number = 0.0;   // Number, and Bool as 0 or 1
			std::string             text;           // Text only
			std::vector<ObserverId> observers;
		};

		void markDirty(const Signal& signal);
		uint32_t declare(std::string_view name, SignalType type);

		std::vector<Signal>                          mSignals;
		std::unordered_map<std::string, uint32_t>    mByName;
		std::vector<ObserverId>                      mDirty;
		// Membership test for mDirty, so an observer that depends on three changed
		// signals is still queued once without scanning what is already there.
		std::vector<bool>                            mQueued;
	};

	// The application's signals. One table per process, the way there is one scene.
	Signals& signals();
}
