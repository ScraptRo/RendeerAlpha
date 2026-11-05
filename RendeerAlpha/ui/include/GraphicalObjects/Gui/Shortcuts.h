#pragma once
#include <vector>
#include <GraphicalObjects/Gui/Focus.h>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/Widget.h>

namespace RDA {

	// The keyboard shortcuts of a frame: widgets offer theirs while the tree is walked, and the
	// chords of the frame are matched against them once the walk is over -- for the reason a
	// context menu is called after it: a handler may change the tree, and the walk is iterating it.
	//
	// The last offer of a chord wins, which is the widget drawn last -- an open menu over the
	// interface behind it. Whatever has the keyboard hears its own keys first, which is why
	// dispatch is handed the Focus.
	//
	// Owned by Gui. Public so that Gui.h can hold one by value.
	class Shortcuts {
	public:
		// A widget whose shortcut could be pressed right now. A no-op on a frame with no chord.
		void offer(Widget* widget, const GuiInput& input);
		// The shortcuts of a subtree that is not being painted.
		void offerIn(Widget& subtree, const GuiInput& input);
		// Matches this frame's chords. Returns whether any widget was pressed, which is not input
		// the retained cache can see.
		bool dispatch(const GuiInput& input, const Focus& focus);
		void clear() { mOffered.clear(); }
		bool empty() const { return mOffered.empty(); }

	private:
		// Shortcuts that could be pressed this frame, in the order they were offered. Only
		// filled on a frame with a chord in it.
		std::vector<Widget*> mOffered;
		// Chords already reported as belonging to two live widgets, so the log says it
		// once rather than on every press.
		std::vector<Chord> mClashes;
	};
}
