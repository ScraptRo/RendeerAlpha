#pragma once
#include <cstdint>
#include <vector>
#include <GraphicalObjects/Gui/Interaction.h>
#include <GraphicalObjects/GuiTypes.h>

namespace RDA {

	// What a window with no frame of its own needs from the interface: the regions it is dragged
	// by, a double click on one, and where the layout drew its maximise button.
	//
	// The GUI only records that these are happening. Moving a window is the platform's business
	// and belongs to whatever owns the GLFW handle, which reads them once the walk is over.
	//
	// Owned by Gui. Public so that Gui.h can hold one by value.
	class WindowChrome {
	public:
		// A region the window can be dragged by, as an ordinary immediate-mode widget with id
		// `wid`: a button drawn inside the bar still wins the press, because it runs after this
		// and hot is last-writer-wins. Returns whether it is held.
		bool grip(uint32_t wid, const Rect& rect, const GuiInput& input, Interaction& interaction);

		bool grabbed() const { return mWindowGrabbed; }
		bool toggleAsked() const { return mWindowToggle; }
		void noteMaximizeButton(const Rect& rect) { mMaximizeButton = rect; }
		const Rect& maximizeButton() const { return mMaximizeButton; }
		bool captionUnderPointer() const { return mCaptionHot; }

		// A frame that walks the tree: what the last walk declared is gone until it is declared again.
		void beginWalk();
		// After the walk: hot names whatever is on top under the pointer, so the pointer is over a
		// bare stretch of caption only if it is one of the grips.
		void endWalk(uint32_t hot);

	private:
		bool     mWindowGrabbed = false;  // a dragWindow region is being held
		bool     mWindowToggle = false;   // ...and was double-clicked this frame
		Rect     mMaximizeButton{};       // re-declared by every walk that draws it
		std::vector<uint32_t> mGripIds;   // the dragWindow regions this walk declared
		bool     mCaptionHot = false;     // the pointer ended the last walk on one of them
	};
}
