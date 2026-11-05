#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>

namespace RDA {

	// Lays its children out in a line, top to bottom or left to right.
	//
	// Children sized Fixed or Content take what they ask for; the space left over is
	// split between the Fill ones by weight. On the cross axis everything stretches to
	// the container's width (or height), which is what a stacked panel almost always
	// wants.
	//
	// This is what makes "insert a cell between two others" a one-line operation: nothing
	// carries a hand-computed position, so inserting shifts the rest automatically.
	class Stack : public Widget {
	public:
		explicit Stack(std::string id) : Widget(std::move(id)) {}
		bool  vertical = true;
		float spacing = 4.0f;
		float padding = 0.0f;

		// Absolute, not relative to `vertical`. A stack whose direction is a binding --
		// `arrange={() => state.rda.width < 700 ? "vertical" : "horizontal"}` -- flips
		// axes while it runs, and a cross-axis alignment would silently come to mean the
		// other one at that moment. "Centred horizontally" cannot.
		//
		// Whichever of the two lies across the stacking axis decides how wide or tall a
		// child that named no size across it becomes; the other moves the children along
		// the axis as a group, and Stretch there is read as Start because stretching
		// along the axis is what a child's own `fill` is for.
		Alignment  hAlign;
		Alignment  vAlign;
		Distribute spread = Distribute::None;

		// Across the stacking axis and along it, whichever way round this one runs.
		Alignment crossAlign() const { return crossAlignOf(vertical, hAlign, vAlign); }
		Alignment mainAlign()  const { return mainAlignOf(vertical, hAlign, vAlign); }

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}
