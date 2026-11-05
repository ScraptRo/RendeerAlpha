#include <GraphicalObjects/Widgets/Controls/Splitter.h>
#include <GraphicalObjects/Gui.h>

namespace RDA {

	// ---- Scroll ---------------------------------------------------------------------

	void Splitter::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.splitter(mId.c_str(), value, minValue, maxValue, abs, vertical)) {
			if (onChange) onChange(value);
		}
	}
}
