#pragma once
#include <cstdint>
#include <Core/Signals.h>
#include <Layout/Expression.h>
#include <Layout/WidgetRegistry.h>
#include <string>
#include <vector>

namespace RDA {
	class Widget;
	class ListView;
}

// Live bindings: the half of the loop that puts a changed signal on screen.
//
// A value binding is a compiled program, the signals it reads, and the widget property
// it drives. It registers as an observer of each of those signals, so writing one marks
// exactly the bindings that depend on it and nothing else. Once a frame the engine
// drains that dirty list, evaluates those programs, and writes the results.
//
// Nothing walks the widget tree, and nothing is compared against a previous state. The
// cost of a change is the number of bindings that actually read what changed.
//
// Event handlers are not here. A handler is a program too, but it runs when a widget
// calls it rather than when a signal moves, so it lives in the widget's own callback
// where its lifetime is already handled.
namespace RDA::Layout {

	class BindingRuntime {
	public:
		// Takes ownership of the program. `signalIds` is the program's signal list already
		// resolved against the table, so evaluating never looks a name up. `setter` writes
		// the result into `target`; `property` is only its name, for what is reported.
		ObserverId add(Program program, std::vector<uint32_t> signalIds,
		               Widget* target, std::string property, PropSetter setter);

		// An observer that drives no property: when any of these signals moves, the list
		// is told its rows are stale and re-binds them itself.
		//
		// This exists because a row binding cannot be an ordinary one. An ordinary
		// binding knows the single widget it writes; a row binding is one program over a
		// pool of rows, and a signal write has no way to say which row it meant. So the
		// signal graph drives the list, and the list drives its rows.
		ObserverId addRowRefresh(std::vector<uint32_t> signalIds, ListView* list);

		// Stops a binding. Called when the widgets it writes to are going away — an
		// evaluation afterwards would be writing through a dangling pointer.
		void remove(ObserverId observer);

		// One more signal an observer depends on. For a list whose rows run JavaScript,
		// whose reads are known only as they happen.
		void watch(ObserverId observer, uint32_t signal);


		// Evaluates every binding, whether or not anything changed. Used once when a
		// layout is instantiated, so a property starts at the value its expression says
		// rather than at whatever the widget was constructed with.
		size_t applyAll();
		// One binding, now: for a binding made after the layout loaded -- a list row built
		// because the view grew -- which has no reason to wait for its signal to move.
		bool applyNow(ObserverId observer);

		// Evaluates the bindings whose signals changed, and clears the dirty list.
		// Returns how many were applied, which is zero on almost every frame.
		size_t applyDirty();

	private:
		struct Binding {
			Program               program;
			std::vector<uint32_t> signalIds;
			Widget*               target = nullptr;
			std::string           property;
			PropSetter            setter = nullptr;
			// Set instead of target/property: this one marks a list stale rather than
			// evaluating anything.
			ListView*             refresh = nullptr;
			bool                  live = false;
			// A program that leaves the VM. What it reads is recorded each time it runs
			// and the observed set follows it: `dynamic` is what it read last time beyond
			// `signalIds`, and `tableReads` the tables, with the version each had then.
			bool                  script = false;
			std::vector<uint32_t> dynamic;
			std::vector<std::pair<uint32_t, uint64_t>> tableReads;
			// The last failure said, so a function that throws is reported when what it
			// says changes rather than every time it runs.
			std::string           lastError;
		};

		void rewatch(ObserverId id, Binding& binding, const std::vector<uint32_t>& read,
		             const std::vector<std::pair<uint32_t, uint64_t>>& tablesRead);

		bool applyOne(Binding& binding);
		// Places a binding at a new observer id, and returns the id.
		ObserverId place(Binding binding);

		// Indexed by observer id, so the dirty list from Signals is a direct lookup.
		// Removed bindings leave a hole rather than shifting, because an id that moved
		// would point at the wrong binding from inside the signal graph. Ids come from
		// Signals, which other observers share, so there are holes for theirs too.
		std::vector<Binding> mBindings;
		size_t               mLive = 0;
		// Bindings that read a table while running, checked when any table moves.
		std::vector<ObserverId> mTableWatchers;
		uint64_t                mSeenTables = 0;
	};

	BindingRuntime& bindings();
}
