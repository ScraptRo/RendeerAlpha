#include <GraphicalObjects/Widgets/Containers/Panel.h>
#include <GraphicalObjects/Gui.h>

namespace RDA {

	void Panel::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		gui.beginPanel(mId.c_str(), abs, variant);
		// Children are positioned relative to the panel's top-left.
		paintChildren(gui, glm::vec2(abs.x, abs.y));
		gui.endPanel();
	}
}
