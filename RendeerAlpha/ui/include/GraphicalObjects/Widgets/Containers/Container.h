#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>

namespace RDA {

	// An invisible grouping node: paints its children, draws nothing itself. The Gui's
	// retained root is one of these.
	class Container : public Widget {
	public:
		explicit Container(std::string id) : Widget(std::move(id)) {}
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}
