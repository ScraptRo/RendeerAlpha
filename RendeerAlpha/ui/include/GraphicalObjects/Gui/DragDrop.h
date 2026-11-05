#pragma once
#include <string>
#include <utility>
#include <vector>
#include <GraphicalObjects/Gui/Interaction.h>
#include <GraphicalObjects/GuiTheme.h>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/Widget.h>

namespace RDA {

	class Canvas;

	// Dragging and dropping inside the interface, and files dropped on it from outside.
	//
	// A widget with something to carry is noted as the walk goes (the deepest under the pointer
	// wins), and every widget that would take it offers itself while something is carried. After
	// the walk, update() decides whether a press has become a drag, which target is under the
	// pointer, and whether the button came up. What travels is copied out of the widget when it
	// is picked up, so the widget may scroll away or be destroyed and the drop still has what it
	// was given.
	//
	// Owned by Gui. Public so that Gui.h can hold one by value.
	class DragDrop {
	public:
		// A widget with something to carry was pressed.
		void notePickUp(Widget* widget, const Rect& where);
		// A widget that would take what is carried. `clip` is the clip in force where it is drawn.
		void offerTarget(Widget* widget, const Rect& where, const GuiInput& input, const Rect& clip);
		// A widget asking for files dropped from outside, under the drop.
		void noteFileTarget(Widget* widget) { mFileTarget = widget; }

		bool dragging() const { return mDrag.active; }
		const std::string& kind() const { return mDrag.kind; }
		// Pressed and not yet moved far enough, or travelling: either is input the retained cache
		// cannot see.
		bool busy() const { return mDrag.active || mDrag.pending; }

		// Every frame: what was noted last frame is forgotten. Also when widgets died between the
		// walk and the end of the frame, and nothing noted may be followed.
		void resetFrame();
		// Files from outside, to the deepest element under them that asked. Returns whether any
		// handler was called.
		bool deliverFiles(const GuiInput& input);
		// The drag, after the walk: picks up, travels, lands or is cancelled, and draws the figure.
		// Returns whether the frame should be walked again.
		bool update(const GuiInput& input, Interaction& interaction, Canvas& canvas, const Theme& theme);

	private:
		struct DragState {
			bool        pending = false;   // pressed, not yet moved far enough
			bool        active = false;    // travelling
			std::string payload;
			std::string kind;
			std::string label;
			glm::vec2   size{ 0.0f };
			glm::vec2   grab{ 0.0f };      // where in it the pointer took hold
			glm::vec2   from{ 0.0f };      // where the press was
		};
		DragState mDrag;
		Widget*   mPickUp = nullptr;       // pressed this frame and has something to carry
		Rect      mPickUpRect{};
		// Every target drawn this frame during a drag, and where. The one under the
		// pointer is worked out after the walk from the last that contains it.
		std::vector<std::pair<Widget*, Rect>> mDropTargets;
		Widget*   mFileTarget = nullptr;   // under a drop of files this frame

		void finishDrag(bool dropped, bool& awake);
		void drawFigure(const Rect& target, bool haveTarget, const GuiInput& input,
		                Canvas& canvas, const Theme& theme);
	};
}
