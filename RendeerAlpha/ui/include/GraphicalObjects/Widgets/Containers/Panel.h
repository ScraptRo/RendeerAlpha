#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A framed panel with a background; clips and positions its children.
	class Panel : public Widget {
	public:
		explicit Panel(std::string id) : Widget(std::move(id)) {}
		Variant variant = kDefaultVariant; // theme variant (see Gui::theme())
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}
