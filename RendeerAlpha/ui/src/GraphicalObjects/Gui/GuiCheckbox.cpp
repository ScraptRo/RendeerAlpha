#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cstdint>

// Gui::checkbox.
namespace RDA {

	// ---- checkbox -----------------------------------------------------------------
	bool Gui::checkbox(const char* id, const char* label, bool& value, const Rect& rect, Variant variant) {
		const CheckboxStyle& s = mTheme.checkbox(variant);
		uint32_t wid = scopedId(id);
		bool inside = rect.contains(mInput.pointer);
		if (inside) mInteraction.hot = wid;
		if (mInteraction.hot == wid && mInput.lPressed) { mInteraction.active = wid; mInteraction.clickPress = wid; }

		bool clicked = false;
		if (mInteraction.active == wid && mInput.lReleased) {
			if (mInteraction.hot == wid) clicked = true;
			mInteraction.active = 0;
		}
		if (mInteraction.hot == wid && mInput.lPressed) setFocus(wid);
		focusable(wid, rect);
		if (focusActivated(wid)) clicked = true;
		if (clicked) value = !value;

		float box = rect.h;
		Rect boxRect{ rect.x, rect.y, box, box };
		const uint32_t boxColour = motion().colour(wid ^ kMotionFill,
			(mInteraction.hot == wid) ? s.boxHover : s.box, s.motion.seconds, s.motion.curve);
		const float radius = motion().value(wid ^ kMotionRadius, s.radius,
		                                    s.motion.seconds, s.motion.curve);
		mCanvas.addFrame(boxRect, boxColour, s.border, s.borderWidth, radius);

		// The mark grows and fades rather than appearing. Ticking a box is the smallest
		// thing an interface confirms, and it is the one most worth confirming visibly.
		const float shown = motion().value(wid ^ kMotionMark, value ? 1.0f : 0.0f,
		                                   s.motion.seconds, s.motion.curve);
		if (shown > 0.004f) {
			const float inset = box * s.checkInset;
			const float full = box - 2.0f * inset;
			const float size = full * shown;
			const float centre = (full - size) * 0.5f;
			mCanvas.addRectRounded({ rect.x + inset + centre, rect.y + inset + centre, size, size },
			               fadeTo(s.check, shown), (std::max)(0.0f, radius - inset));
		}
		if (mCanvas.font() && label && label[0]) {
			float baselineY = rect.y + rect.h * 0.5f + mCanvas.baseline(s.font) * 0.35f;
			mCanvas.addText(rect.x + box + 8.0f, baselineY, label, s.label, s.font);
		}
		return clicked;
	}
}
