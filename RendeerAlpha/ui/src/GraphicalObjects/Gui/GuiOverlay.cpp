#include <GraphicalObjects/Gui.h>

// Gui, overlays and anchors: painting over the rest of the tree, and where a widget was
// drawn.
namespace RDA {

	void Gui::drawAbove(Widget* widget, glm::vec2 origin) {
		if (widget) mOverlays.emplace_back(widget, origin);
	}

	void Gui::wantAnchor(const std::string& path) {
		if (!path.empty()) mAnchorWanted.insert(path);
	}

	bool Gui::anchorRect(const std::string& path, Rect& out) const {
		const auto at = mAnchorRect.find(path);
		if (at == mAnchorRect.end()) return false;
		out = at->second;
		return true;
	}
}
