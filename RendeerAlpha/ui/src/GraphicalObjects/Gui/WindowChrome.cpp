#include <GraphicalObjects/Gui/WindowChrome.h>
#include <algorithm>

// WindowChrome: the region a window with no frame of its own is dragged by.
namespace RDA {

	bool WindowChrome::grip(uint32_t wid, const Rect& rect, const GuiInput& input, Interaction& interaction) {
		mGripIds.push_back(wid);
		if (rect.contains(input.pointer)) interaction.hot = wid;
		if (interaction.hot == wid && input.lPressed) interaction.active = wid;
		if (interaction.active == wid && input.lReleased) interaction.active = 0;
		// The second press of a double click on the bar itself -- not on a button drawn
		// over it, which runs later and takes hot from it, and so is never `wid` here.
		if (interaction.hot == wid && input.lPressed && input.clickCount == 2) mWindowToggle = true;

		// Held is enough: there is no click to report and nothing to draw. A drag that
		// began here goes on even once the pointer has left the bar, which is what makes
		// a window follow the pointer across the screen rather than stopping at its own
		// edge.
		const bool held = (interaction.active == wid);
		if (held) {
			mWindowGrabbed = true;
		}
		return held;
	}

	void WindowChrome::beginWalk() {
		mGripIds.clear();
		// The same for the maximise button: a walk that does not draw it means it is gone.
		// Not on a reused frame, which draws exactly what the last walk did.
		mMaximizeButton = Rect{};
		// The same: a grip is held for as long as it is re-declared and the pointer is
		// still down, so letting go needs no call.
		mWindowGrabbed = false;
		mWindowToggle = false;
	}

	void WindowChrome::endWalk(uint32_t hot) {
		mCaptionHot = std::find(mGripIds.begin(), mGripIds.end(), hot) != mGripIds.end();
	}
}
