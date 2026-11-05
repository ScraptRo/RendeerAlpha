#include <GraphicalObjects/Docking.h>
#include <GraphicalObjects/Gui.h>

// The three widgets of the dock system: the header a panel can wear (DockHeader, which is
// all inline and has nothing to define here), the panel itself, and the host that puts a
// dock space in a layout. What they do between them -- arranging, dragging, drawing the
// chrome -- is DockSpace's, in the files beside this one.
namespace RDA {

	DockHeader* DockContainer::header() const {
		for (const std::unique_ptr<Widget>& child : children()) {
			if (auto* found = dynamic_cast<DockHeader*>(child.get())) return found;
		}
		return nullptr;
	}

	float DockContainer::headerHeight(float fallback) const {
		const DockHeader* bar = header();
		return bar ? bar->bandHeight(fallback) : fallback;
	}

	bool DockContainer::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::Pane;
		out.name = alt.empty() ? title : alt;
		return true;
	}

	void DockContainer::drawContents(Gui& gui, const Rect& body) {
		// A panel is a place a reader can go, named by its title, and what is in it hangs
		// off it. Recorded here because the dock space, not a parent, is what draws it.
		if (gui.accessibilityWanted()) gui.noteAccessible(this, body);
		// Indexed and re-read for the same reason paintChildren is: a callback in here
		// may add or remove a sibling while this is walking them.
		for (size_t i = 0; i < children().size(); ++i) {
			Widget* child = children()[i].get();
			if (!child || !child->visible) continue;
			if (dynamic_cast<DockHeader*>(child)) continue;   // chrome, drawn in the bar
			child->paint(gui, glm::vec2(body.x, body.y));
		}
	}

	// ---- the dock space as a widget -----------------------------------------------

	DockHost::~DockHost() {
		// On the way out, including the teardown a hot reload does -- which is why an
		// arrangement survives editing the layout that declared it.
		if (!persist.empty()) mSpace.saveLayoutToFile(persist);
	}

	Widget* DockHost::addChild(std::unique_ptr<Widget> child) {
		if (dynamic_cast<DockContainer*>(child.get())) {
			// Ownership moves to the dock space, which is what positions it. The pointer
			// handed back is still a Widget*, so whatever built the tree goes on adding
			// this panel's contents to it without knowing any of that happened.
			return mSpace.adopt(std::unique_ptr<DockContainer>(
				static_cast<DockContainer*>(child.release())));
		}
		return Widget::addChild(std::move(child));
	}

	void DockHost::paint(Gui& gui, glm::vec2 origin) {
		const Rect area = placement(gui, origin);

		// A backdrop, if anything that is not a panel was put in here.
		paintChildren(gui, glm::vec2(area.x, area.y));

		if (!mOpened) {
			mOpened = true;
			mSpace.placeFloatingWithin(area);
			// After the panels exist, because a saved arrangement is matched to them by
			// id and one that arrives first has nothing to match against.
			if (!persist.empty()) mSpace.loadLayoutFromFile(persist);
		}

		// Clipped to itself. A panel dragged past the edge stops at it, which is what
		// makes this a dock area rather than a second window manager.
		gui.pushClipRect(area);
		mSpace.update(gui, area);
		gui.popClipRect();
	}
}
