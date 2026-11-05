#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A labelled boolean toggle. onChange fires with the new value when toggled.
	class Checkbox : public Widget {
	public:
		Checkbox(std::string id, std::string label) : Widget(std::move(id)), label(std::move(label)) {}
		std::string               label;
		bool                      value = false;
		Variant                   variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void(bool)> onChange;
		// The same toggle a click makes.
		void pressShortcut() override {
			value = !value;
			if (onChange) onChange(value);
		}
		// The box is square and as tall as a line; the label follows it.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;
	};
}
