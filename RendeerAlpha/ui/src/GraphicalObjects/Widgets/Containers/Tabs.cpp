#include <GraphicalObjects/Widgets/Containers/Tabs.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cmath>

namespace RDA {

	// ---- Tabs ----------------------------------------------------------------------
	float Tabs::barSize(Gui& gui) const {
		return barHeight > 0.0f ? barHeight : std::round(gui.lineHeight() + 10.0f);
	}

	glm::vec2 Tabs::measureContent(Gui& gui, glm::vec2 available) const {
		const float bar = barSize(gui);
		// As tall as the bar plus the page showing, because the pages that are not showing
		// are not there as far as layout is concerned.
		glm::vec2 page{ 0.0f, 0.0f };
		int index = 0;
		for (const auto& child : children()) {
			if (!dynamic_cast<Tab*>(child.get())) continue;
			if (index++ == value) {
				page = child->measureContent(gui, { available.x, available.y - bar });
				break;
			}
		}
		return { (std::max)(available.x, page.x), bar + page.y };
	}

	void Tabs::offerShortcuts(std::vector<Widget*>& out) {
		if (shortcut.valid()) out.push_back(this);
		// Counted the way paint counts them: by Tab, so anything else among the children
		// does not shift which page `value` means.
		int index = 0;
		for (const auto& child : children()) {
			Tab* tab = dynamic_cast<Tab*>(child.get());
			if (!tab) continue;
			if (index++ == value) {
				if (tab->visible) tab->offerShortcuts(out);
				return;
			}
		}
	}

	void Tabs::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		const float bar = barSize(gui);

		// The titles, for a screen reader, before the page -- which is the order they are
		// read in, even though the page is drawn first. Each is known by the same id its
		// button is, so the keyboard's focus and the screen reader's are the same thing.
		if (gui.accessibilityWanted()) {
			float x = box.x;
			int at = 0;
			gui.pushId(mId.c_str());
			for (const auto& child : children()) {
				const Tab* tab = dynamic_cast<const Tab*>(child.get());
				if (!tab) continue;
				const float w = gui.measureText(tab->title.c_str()) + 22.0f;
				AccessibleInfo item;
				item.role = AccessibleRole::TabItem;
				item.name = tab->title;
				item.focusable = true;
				item.selected = (at == value) ? 1 : 0;
				item.actions = kCanSelect | kCanInvoke;
				gui.noteAccessibleItem(this, gui.widgetId(tab->id().c_str()), at, nullptr,
				                       { x, box.y, w, bar }, item, tab->id());
				x += w + 2.0f;
				++at;
			}
			gui.popId();
		}

		// ---- the page ----------------------------------------------------------------
		//
		// Drawn *before* the bar, which is the other way round from how this used to be.
		//
		// A bar belongs on top of the pages it switches between -- and once a title can
		// be drawn by a mesh, it has to be: a tab that reaches a pixel or two past its
		// own rect to meet the page below it is a tab whose overhang the page would
		// otherwise paint straight over. Input is unaffected either way, because the two
		// rectangles do not overlap; only the drawing does.
		//
		// The cost is that a click lands after the pages have been painted, so the page
		// it chose begins arriving on the next frame rather than this one. One frame, of
		// a transition that is measured in tens.
		const float seconds = motionSeconds();
		const uint32_t key = gui.motionKey(mId.c_str(), kMotionOpen);

		// A page changed. The one being left goes on being painted until the transition is
		// over, and keeping it costs nothing because it was never destroyed: a Tab is a
		// Widget and a Widget persists, which is the same property that lets a tab keep
		// what was typed into it.
		if (mShown != value) {
			if (seconds > 0.0f && mShown >= 0) {
				mLeaving = mShown;
				gui.motion().reset(key, 0.0f); // the transition begins at its beginning
			}
			mShown = value;
		}

		// 0 the moment the page changed, 1 once the new one has fully arrived.
		float progress = 1.0f;
		if (seconds > 0.0f && mLeaving >= 0) {
			progress = gui.motion().value(key, 1.0f, seconds, Easing::InOut);
			if (progress >= 0.999f) mLeaving = -1;
		}

		// Which way they slide: going forward through the bar moves left, going back moves
		// right, so the movement agrees with where the pages sit in the row of titles.
		const float direction = (mLeaving > value) ? -1.0f : 1.0f;
		const float travel = box.w * 0.06f; // a hint of movement, not a journey

		int index = 0;
		for (const auto& child : children()) {
			Tab* tab = dynamic_cast<Tab*>(child.get());
			if (!tab) continue;
			const int at = index++;
			if (at == value) {
				paintPage(gui, *tab, box, bar, direction * travel * (1.0f - progress), progress);
			} else if (at == mLeaving) {
				paintPage(gui, *tab, box, bar, -direction * travel * progress, 1.0f - progress);
			}
		}

		// ---- the bar -------------------------------------------------------------------
		//
		// Each title is a button, so hover, press and motion come from the theme like
		// everything else. Every one of them wears this widget's `variant`, and the one
		// that is showing is `selected` -- which is what tells a colour apart (it draws
		// in that variant's pressed colour) and what puts a mesh-drawn title on the top
		// rung of its ladder of states.
		//
		// It used to be the other way about: the showing title wore `variant` and the
		// rest wore the default. That made a bar with no variant at all show nothing
		// about which page was open, and it made a mesh impossible, because the titles
		// have to be one shape to differ by one state.
		float x = box.x;
		index = 0;
		gui.pushId(mId.c_str());
		for (const auto& child : children()) {
			Tab* tab = dynamic_cast<Tab*>(child.get());
			if (!tab) continue;
			const float w = gui.measureText(tab->title.c_str()) + 22.0f;
			if (gui.button(tab->id().c_str(), tab->title.c_str(), { x, box.y, w, bar },
			               variant, TextAlign::Center, 0.0f, index == value)
			    && index != value) {
				value = index;
				if (onChange) onChange(index);
			}
			x += w + 2.0f;
			++index;
		}
		gui.popId();
	}

	// One page of a Tabs, offset sideways and faded by however far through it is.
	//
	// Clipped to the body so a page on its way in or out draws neither over the row of
	// titles nor past the edge of what the tabs were given. The rect stays relative to the
	// box and the origin stays the box, exactly as it is without a transition -- the slide
	// is the only thing added.
	void Tabs::paintPage(Gui& gui, Widget& page, const Rect& box, float bar,
	                     float dx, float alpha) {
		if (alpha <= 0.004f) return;
		gui.pushClipRect({ box.x, box.y + bar, box.w, box.h - bar });
		gui.pushOpacity(alpha);
		page.rect = { dx, bar, box.w, box.h - bar };
		page.paint(gui, glm::vec2(box.x, box.y));
		gui.popOpacity();
		gui.popClipRect();
	}

	bool Tabs::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::TabList;
		out.name = alt;
		return true;
	}
	void Tabs::accessibleItemAction(int item, AccessibleAction action, const std::string& text) {
		(void)text;
		if (action != AccessibleAction::Select && action != AccessibleAction::Invoke) return;
		if (item == value) return;
		value = item;
		if (onChange) onChange(item);
	}
}
