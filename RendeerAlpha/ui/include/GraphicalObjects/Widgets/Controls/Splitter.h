#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>

namespace RDA {

	class Splitter : public Widget {
	public:
		explicit Splitter(std::string id) : Widget(std::move(id)) {}
		float value = 120.0f;   // the size being controlled
		float minValue = 40.0f;
		float maxValue = 4000.0f;
		bool  vertical = false; // false: a horizontal bar dragged up/down
		std::function<void(float)> onChange;
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}
