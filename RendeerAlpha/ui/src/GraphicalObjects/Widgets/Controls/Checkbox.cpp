#include <GraphicalObjects/Widgets/Controls/Checkbox.h>
#include "../WidgetInternal.h"
#include <GraphicalObjects/Gui.h>

namespace RDA {

	using detail::either;

	void Checkbox::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.checkbox(mId.c_str(), label.c_str(), value, abs, variant)) {
			if (onChange) onChange(value);
		}
	}

	// ---- measuring text widgets ------------------------------------------------------
	//
	// Without these, SizeSpec::Mode::Content falls back to Widget::measureContent, which
	// reports `rect` — zero for anything a layout container is positioning. Stacked in a
	// column that made every label zero pixels tall and drew them on top of each other,
	// so a layout had to name a pixel height for every piece of text in it.
	//
	// The numbers below mirror what the immediate widgets actually draw, so a measured
	// widget and a hand-sized one land in the same place.

	namespace {
		// kButtonPadX / kButtonPadY are in GuiTypes.h, because Gui::button needs the same
		// numbers to inset aligned text by.
		//
		// Gui::checkbox draws a square box of rect.h and starts the label 8px after it.
		constexpr float kCheckboxGap = 8.0f;
	}

	glm::vec2 Checkbox::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		const TextStyle& font = gui.theme().checkbox(variant).font;
		const float line = gui.lineHeight(font);
		// The box is square and as tall as the row, so the row's height is the box.
		return { line + kCheckboxGap + gui.measureText(label.c_str(), font), line };
	}

	bool Checkbox::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::CheckBox;
		out.name = either(alt, label);
		out.focusable = true;
		out.toggled = value ? 1 : 0;
		out.actions = kCanToggle | kCanInvoke;
		return true;
	}
	void Checkbox::accessibleAction(AccessibleAction action, const std::string& value) {
		(void)value;
		if (action == AccessibleAction::Toggle || action == AccessibleAction::Invoke) pressShortcut();
	}
}
