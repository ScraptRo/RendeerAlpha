#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// A clipped window onto content taller than itself.
	//
	// Children are stacked in a column at the height each asks for, shifted up by
	// `offset`, and clipped to the view. A draggable bar appears on the right whenever
	// there is more content than fits.
	//
	// Children are measured against the *visible* height, not the full content height,
	// so a child that sizes itself relative to what is available ("no more than a third
	// of the panel") means the window rather than the scrolling extent. The bar's width
	// is reserved whether or not it is showing: letting it appear and disappear would
	// change the width available to the content, which can change the content's height,
	// which can decide the bar again — a layout that never settles.
	//
	// Anything scrolled entirely out of view is skipped rather than painted, so a long
	// notebook costs roughly what is on screen instead of what exists.
	class Scroll : public Widget {
	public:
		explicit Scroll(std::string id) : Widget(std::move(id)) {}

		float offset = 0.0f;     // how much content is hidden above the top edge
		float offsetX = 0.0f;    // and past the left one, when it scrolls sideways
		// Which way it may scroll. Down by default: a column taller than its view is the
		// ordinary case, and one wider than its view is usually a layout mistake rather
		// than something to let the reader drag around.
		bool  vertical = true;
		bool  horizontal = false;
		float spacing = 0.0f;    // between stacked children
		float padding = 0.0f;
		float barWidth = 10.0f;
		float wheelStep = 52.0f; // pixels per wheel notch
		// Where children sit across the column, the same question a Stack answers.
		// Stretch -- every child as wide as the view -- is what a scrolling page almost
		// always wants; the other three are what makes a narrow column of centred cards
		// possible. Only horizontal: this always stacks downward, so there is no second
		// axis to have an opinion about.
		Alignment hAlign;
		// Styling for the bar comes from a text field variant, which is where the scroll
		// colours already live, so a scroll view matches the fields it usually contains.
		Variant variant = kDefaultVariant;

		// Valid after the first paint.
		float contentHeight() const { return mContentHeight; }
		float viewHeight() const { return mViewHeight; }
		float maxOffset() const { return mContentHeight > mViewHeight ? mContentHeight - mViewHeight : 0.0f; }
		bool  atBottom() const { return offset >= maxOffset() - 0.5f; }
		void  scrollToBottom() { offset = maxOffset(); }
		// Brings a descendant's arranged rect into view. Call after a paint, since it
		// works from the geometry that paint computed.
		void  scrollToShow(const Rect& target);

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		float mContentHeight = 0.0f;
		float mViewHeight = 0.0f;
		float mViewTop = 0.0f;   // where the content origin sat last frame, unscrolled
		bool  mDraggingThumb = false;
		bool  mDraggingThumbX = false;
		float mThumbGrabX = 0.0f;
		float mContentWidth = 0.0f;
		float mThumbGrab = 0.0f;
	};
}
