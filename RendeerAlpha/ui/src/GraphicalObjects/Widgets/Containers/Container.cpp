#include <GraphicalObjects/Widgets/Containers/Container.h>
#include <GraphicalObjects/Gui.h>

namespace RDA {

	// ---- concrete widgets: each is one immediate-mode call ------------------------
	void Container::paint(Gui& gui, glm::vec2 origin) {
		paintChildren(gui, glm::vec2(origin.x + rect.x, origin.y + rect.y));
	}
}
