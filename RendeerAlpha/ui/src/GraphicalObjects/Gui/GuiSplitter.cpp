#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cstdint>

// Gui::splitter.
namespace RDA {

	// ---- splitter -------------------------------------------------------------------
	bool Gui::splitter(const char* id, float& size, float minSize, float maxSize,
	                   const Rect& rect, bool vertical) {
		uint32_t wid = scopedId(id);
		const bool inside = rect.contains(mInput.pointer);
		if (inside) mInteraction.hot = wid;
		if (mInteraction.hot == wid && mInput.lPressed) {
			mInteraction.active = wid;
			// The grab offset is stored as the size the pane had when the drag began,
			// biased by the pointer, so the bar does not jump to the cursor on grab.
			mTextStates[wid].scrollGrab = (vertical ? mInput.pointer.x : mInput.pointer.y) - size;
		}

		bool changed = false;
		if (mInteraction.active == wid) {
			mFocus.claimed = true;
			const float pointer = vertical ? mInput.pointer.x : mInput.pointer.y;
			float next = std::clamp(pointer - mTextStates[wid].scrollGrab, minSize, maxSize);
			if (next != size) { size = next; changed = true; }
			if (mInput.lReleased) mInteraction.active = 0;
		}

		const bool active = (mInteraction.active == wid) || (mInteraction.hot == wid);
		mCanvas.addRect(rect, active ? rgba(90, 110, 150) : rgba(52, 58, 70));
		// A short grip in the middle, so the bar reads as draggable rather than as a rule.
		const float gripLength = (vertical ? rect.h : rect.w) * 0.25f;
		if (vertical) {
			mCanvas.addRect({ rect.x + rect.w * 0.5f - 1.0f, rect.y + (rect.h - gripLength) * 0.5f,
			          2.0f, gripLength }, rgba(150, 165, 190));
		} else {
			mCanvas.addRect({ rect.x + (rect.w - gripLength) * 0.5f, rect.y + rect.h * 0.5f - 1.0f,
			          gripLength, 2.0f }, rgba(150, 165, 190));
		}
		return changed;
	}
}
