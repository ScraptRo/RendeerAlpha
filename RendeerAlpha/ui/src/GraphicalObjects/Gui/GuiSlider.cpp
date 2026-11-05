#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cstdint>

// Gui::sliderFloat.
namespace RDA {

	// ---- slider -------------------------------------------------------------------
	bool Gui::sliderFloat(const char* id, float& value, float minValue, float maxValue, const Rect& rect, Variant variant) {
		const SliderStyle& s = mTheme.slider(variant);
		uint32_t wid = scopedId(id);
		bool inside = rect.contains(mInput.pointer);
		if (inside) mInteraction.hot = wid;
		if (mInteraction.hot == wid && mInput.lPressed) mInteraction.active = wid;

		bool changed = false;
		if (mInteraction.active == wid) {
			float t = (rect.w > 0.0f) ? (mInput.pointer.x - rect.x) / rect.w : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			float next = minValue + t * (maxValue - minValue);
			if (next != value) { value = next; changed = true; }
			if (mInput.lReleased) mInteraction.active = 0;
		}

		if (mInteraction.hot == wid && mInput.lPressed) setFocus(wid);
		focusable(wid, rect);
		// A twentieth of the range per press, which is a usable number of presses to
		// cross it and small enough to land on a particular value.
		if (const int step = focusStep(wid); step != 0) {
			const float span = maxValue - minValue;
			const float next = std::clamp(value + step * span * 0.05f, minValue, maxValue);
			if (next != value) { value = next; changed = true; }
		}

		float t = (maxValue > minValue) ? (value - minValue) / (maxValue - minValue) : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		const float radius = motion().value(wid ^ kMotionRadius, s.radius,
		                                    s.motion.seconds, s.motion.curve);
		mCanvas.addRectRounded(rect, s.track, radius);                                  // track
		mCanvas.addRectRounded({ rect.x, rect.y, rect.w * t, rect.h }, s.fill, radius); // fill
		float knob = s.knobWidth;
		mCanvas.addRectRounded({ rect.x + rect.w * t - knob * 0.5f, rect.y - 2.0f, knob, rect.h + 4.0f },
		               (mInteraction.active == wid) ? s.knobActive : s.knob, radius);
		return changed;
	}
}
