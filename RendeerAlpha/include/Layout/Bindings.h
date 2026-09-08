#pragma once
#include <cstdint>
#include <Core/Signals.h>
#include <Layout/Expression.h>
#include <string>
#include <vector>

namespace RDA {
	class Widget;
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
		// resolved against the table, so evaluating never looks a name up.
		ObserverId add(Program program, std::vector<uint32_t> signalIds,
		               Widget* target, std::string property);

		// Stops a binding. Called when the widgets it writes to are going away — an
		// evaluation afterwards would be writing through a dangling pointer.
		void remove(ObserverId observer);

		// Evaluates every binding, whether or not anything changed. Used once when a
		// layout is instantiated, so a property starts at the value its expression says
		// rather than at whatever the widget was constructed with.
		size_t applyAll();

		// Evaluates the bindings whose signals changed, and clears the dirty list.
		// Returns how many were applied, which is zero on almost every frame.
		size_t applyDirty();

	private:
		struct Binding {
			Program               program;
			std::vector<uint32_t> signalIds;
			Widget*               target = nullptr;
			std::string           property;
			bool                  live = false;
		};

		bool applyOne(Binding& binding);

		// Indexed by observer id, so the dirty list from Signals is a direct lookup.
		// Removed bindings leave a hole rather than shifting, because an id that moved
		// would point at the wrong binding from inside the signal graph.
		std::vector<Binding> mBindings;
		size_t               mLive = 0;
	};

	BindingRuntime& bindings();

	// Writes a computed value into a widget property. Knows the same property names the
	// loader does, because it is the same set: a property that can be given a constant
	// can be given an expression.
	//
	// Returns false for a property that cannot be driven, so the caller can say which one
	// rather than failing silently.
	bool applyBoundValue(RDA::Widget& widget, const std::string& property, const Value& value);
}
