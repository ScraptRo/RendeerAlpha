#pragma once
#include <GraphicalObjects/Widgets/Containers/Container.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// One page of a Tabs. A container with a title; the title is what the tab bar says.
	class Tab : public Container {
	public:
		Tab(std::string id, std::string title)
			: Container(std::move(id)), title(std::move(title)) {}
		std::string title;
	};

	// A row of titles, and the one page whose title is selected.
	//
	// The other pages are not painted and not measured -- they are still there, holding
	// their state, because a Widget is a thing that persists. Selection is an index into
	// the Tab children, so binding it to a signal is the ordinary two-way arrangement
	// that checkbox and slider already use.
	class Tabs : public Container {
	public:
		explicit Tabs(std::string id) : Container(std::move(id)) {}
		int                      value = 0;
		float                    barHeight = 0.0f; // 0 = a line of text plus padding
		Variant                  variant = kDefaultVariant; // the tab buttons' variant
		std::function<void(int)> onChange;

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleItemAction(int item, AccessibleAction action, const std::string& value) override;
		// Only the page that is showing. A shortcut on a page nobody can see would act on
		// something the reader has no way to know is there.
		void offerShortcuts(std::vector<Widget*>& out) override;

	private:
		float barSize(Gui& gui) const;
		void  paintPage(Gui& gui, Widget& page, const Rect& box, float bar,
		                float dx, float alpha);

		// The page being left, while it is still on its way out.
		//
		// Both pages exist the whole time -- a Widget persists, which is what lets a tab
		// keep its state -- so a transition needs nothing kept alive specially. It only
		// needs the outgoing one to go on being painted for as long as it takes, and the
		// index of what that is.
		int mLeaving = -1;
		int mShown = -1;   // what was showing last frame, to notice a change
	};
}
