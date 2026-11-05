#pragma once
#include <GraphicalObjects/Widgets/Containers/Container.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A panel that draws over everything, beside the widget it belongs to, and closes when
	// the pointer goes somewhere else.
	//
	// Every dropdown, menu, tooltip, autocomplete list and dialog wants the same three
	// things -- draw above, take the input first, close when it is not wanted -- and
	// without this every application rebuilds them out of a <container>, a transparent
	// full-area <button> and arithmetic against the window's width. That pattern works
	// and is slightly wrong in a different way each time.
	//
	// It is a Container, so what is inside it is an ordinary layout: a <stack> of buttons
	// is a menu, a <list> is a picker, a <label> is a tooltip. This widget is only the
	// surface, where it goes, and when it goes away.
	class Popup : public Container {
	public:
		// Which edge of the anchor to sit against. `Over` covers it, for a picker that
		// replaces the control rather than hanging off it.
		enum class Placement : uint8_t { Below, Above, Right, Left, Over };

		explicit Popup(std::string id) : Container(std::move(id)) {}

		// Never, whether it is open or not. Open, it is drawn over everything from its
		// anchor; shut, it is nothing at all. Either way the room belongs to its siblings.
		bool inFlow() const override { return false; }

		// Showing or not. Bindable, and the application owns it: this never writes the
		// signal, it calls onClose and lets the layout decide -- the same shape every
		// other two-way property here has.
		bool open = false;

		// The engine has to remember where that widget was drawn to do this, which nothing
		// else needs; see Gui::wantAnchor for what it costs.
		// The full id path of the widget to hang off -- "root/toolbar/model", the path the
		// log, the probe and rda_focus all spell, not the bare id. Empty means the popup
		// is placed by its own x/y/w/h, like anything else in a container.
		//
		// "pointer" is the exception, and is what a context menu wants: the popup appears
		// where the pointer was *when it opened*, and stays there while it is open. Not
		// where the pointer is now -- a menu that followed the mouse could never be
		// reached.
		std::string anchor;
		Placement   placement = Placement::Below;
		float       gap = 4.0f;       // pixels between the anchor and this
		float       padding = 8.0f;   // inset around its children, and what it adds to its own size

		// While it is open, nothing underneath hovers or clicks: the interface behind a
		// menu is behind it. Off for a tooltip, or anything that floats without taking
		// over.
		bool blocking = true;

		// A press outside it, or Escape. The layout closes itself here -- `open` is a
		// binding, so writing false to the signal is what actually shuts it.
		std::function<void()> onClose;

		Variant variant = kDefaultVariant;  // a panel variant, for the surface

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void paintAbove(Gui& gui, glm::vec2 origin) override;

	private:
		// Where it was drawn last, so the dismissal can ask whether the press was inside
		// it -- which happens in the same call that would otherwise have to guess.
		Rect mShown{};
		// Where the pointer was on the frame this opened, for anchor="pointer", and
		// whether it was open last frame -- which is how that moment is noticed.
		glm::vec2 mOpenedAt{ 0.0f };
		bool      mWasOpen = false;
	};
}
