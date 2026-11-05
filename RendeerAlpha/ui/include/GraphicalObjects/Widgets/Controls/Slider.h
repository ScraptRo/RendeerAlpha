#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A draggable float in [minValue, maxValue]. onChange fires while dragging.
	class Slider : public Widget {
	public:
		explicit Slider(std::string id) : Widget(std::move(id)) {}
		float                      value = 0.0f;
		float                      minValue = 0.0f;
		float                      maxValue = 1.0f;
		Variant                    variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void(float)> onChange;
		// A slider has no content to measure, so "content" means a track of ordinary
		// thickness rather than nothing at all.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;
	};
}
