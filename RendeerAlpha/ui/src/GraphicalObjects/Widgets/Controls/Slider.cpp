#include <GraphicalObjects/Widgets/Controls/Slider.h>
#include "../WidgetInternal.h"
#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cmath>

namespace RDA {

	using detail::readNumber;

	void Slider::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.sliderFloat(mId.c_str(), value, minValue, maxValue, abs, variant)) {
			if (onChange) onChange(value);
		}
	}

	glm::vec2 Slider::measureContent(Gui& gui, glm::vec2 available) const {
		// Nothing to measure: it holds no text. A track about two thirds of a line reads
		// as a control rather than a hairline, and the knob overhangs it by 2px either
		// side the way Gui::sliderFloat draws it.
		return { available.x, std::round(gui.lineHeight() * 0.66f) };
	}

	bool Slider::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::Slider;
		out.name = alt;
		out.focusable = true;
		out.hasRange = true;
		out.rangeMin = minValue;
		out.rangeMax = maxValue;
		out.rangeValue = value;
		// What the arrow keys move it by, which is what a screen reader's own step should be.
		out.rangeStep = (maxValue - minValue) / 20.0;
		out.actions = kCanSetValue;
		return true;
	}
	void Slider::accessibleAction(AccessibleAction action, const std::string& text) {
		double wanted = 0.0;
		if (action != AccessibleAction::SetValue || !readNumber(text, wanted)) return;
		const float clamped = std::clamp(static_cast<float>(wanted), (std::min)(minValue, maxValue),
		                                 (std::max)(minValue, maxValue));
		if (clamped == value) return;
		value = clamped;
		if (onChange) onChange(value);
	}
}
