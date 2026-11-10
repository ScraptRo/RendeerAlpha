// What `rda prototype` adds to a layout that has an `editable` container in it.
//
// An outline around each editable container, and a panel of widgets to put in one and of the
// properties of whatever is selected inside one. Every change is written into the .tsx, and hot
// reload is what puts it on the screen, so there is one source of truth and nothing here to save:
// what the editor shows is the file, and closing the window loses nothing.
//
// The outlines are drawn in onUpdate, after the walk, so they sit on top of the interface and hear
// the real pointer, and the pointer is claimed every frame so that the interface takes no clicks.
// The panel is a window of its own, so that it can sit anywhere and cover nothing, and it is drawn
// the same way into that window's GUI. A shot has no second window to show, so there it is drawn
// over the layout, which is also what happens if the window cannot be opened.
//
// F2 takes all of it away and gives the interface its pointer back, and so does closing the panel.
#pragma once

#include "Common.h"

#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/Shortcut.h>
#include <Layout/JsxSource.h>
#include <Layout/WidgetSchema.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace RDA {
	class Gui;
	class Window;
}

namespace Cli {

	class Editor {
	public:
		Editor();

		// The layout now on screen, as its compiled file and its source.
		void show(const std::string& blueprint, const std::string& source);

		// Whether the panel gets a window of its own.
		void floatPanel(bool on) { mFloating = on; }

		// Once a frame, after the walk. Costs one stat of the blueprint when nothing in the
		// layout is editable.
		void frame(RDA::Gui& gui);

	private:
		struct Node {
			std::string id;
			std::string type;
			int         parent = -1;
			int         depth = 0;
			bool        editable = false;  // this one is
			bool        inside = false;    // this one is, or something above it is
			bool        horizontal = false; // a stack running sideways
			bool        arranges = false;   // places its children itself
			std::vector<int> children;
			RDA::Rect   rect;
			bool        placed = false;     // was drawn, so `rect` means something
		};

		// One line of the properties.
		struct Field {
			const RDA::Layout::PropDesc* prop = nullptr;
			std::string text;       // what is showing
			std::string saved;      // what the file says
			bool        computed = false;
			bool        focused = false;
		};

		using Change = std::function<void(RDA::Layout::Jsx::Document& document, int element)>;

		// Where a node is written.
		struct Located {
			std::string file;
			std::unique_ptr<RDA::Layout::Jsx::Document> document;
			int         element = -1;
		};

		void reload(RDA::Gui& gui);
		void measure(RDA::Gui& gui);
		void openPanel(RDA::Gui& gui);
		void closePanel();
		void readKeys(RDA::Gui& gui);

		// Where the pointer is over the layout, in its units. While a widget is being dragged out
		// of the panel's window that window has the mouse, so this is worked out from where the
		// two windows are. False when the system will not say, which Wayland does not.
		bool pointerOverLayout(const RDA::Gui& gui, const RDA::Gui& panel, glm::vec2& out) const;

		void select(int node);
		void readFields();
		void commit(Field& field);
		void commitAll();

		// In the layout's own file, or what it imports: a component's elements are in the file
		// the component is written in. No element in what comes back means `why` says what stopped it.
		Located locate(int node, std::string* why = nullptr) const;

		// Finds `node`, changes it and saves. False, with mStatus saying why, when it is not there
		// to change.
		bool edit(int node, const Change& change);

		int  nodeAt(glm::vec2 point) const;
		int  dropTargetAt(glm::vec2 point) const;
		// Which of `target`'s children something dropped here goes before; their number if
		// none, and it goes at the end.
		size_t dropIndex(int target, glm::vec2 point) const;
		void drop(glm::vec2 point);
		void removeSelected();

		void drawOutlines(RDA::Gui& gui, glm::vec2 pointer, bool overPanel);
		void drawCarried(RDA::Gui& gui, glm::vec2 pointer);
		void drawPanel(RDA::Gui& gui);
		float drawPalette(RDA::Gui& gui, float x, float y, float width);
		void  drawFields(RDA::Gui& gui, const RDA::Rect& area);
		void  drawField(RDA::Gui& gui, Field& field, const RDA::Rect& row);

		bool clicked(const RDA::Gui& gui, const RDA::Rect& rect) const;
		RDA::Rect panelRect(const RDA::Gui& gui) const;

		std::string mBlueprint;
		std::string mSource;
		int64_t     mStamp = 0;
		std::vector<Node> mNodes;
		bool        mAnyEditable = false;
		bool        mAnnounced = false;

		RDA::Chord  mToggle;
		RDA::Chord  mCancel;
		bool        mVisible = true;

		bool         mFloating = false;
		RDA::Window* mPanelWindow = nullptr;
		bool         mPanelIsNew = false;  // made this frame, so its GUI has not begun one

		int         mSelected = -1;
		std::string mPendingSelect;  // an id, for once the file it was just written to reloads
		std::vector<Field> mFields;
		std::string mStatus;

		// The type picked up from the panel; empty when nothing is. Held is the button still
		// being down from picking it up. Let go on the panel it stays picked up, and a click in
		// the layout says where it goes.
		std::string mCarried;
		bool        mHeld = false;
		glm::vec2   mPressAt{ 0.0f };
		glm::vec2   mPanelAt{ -1.0f };
		bool        mMovingPanel = false;
		glm::vec2   mGrab{ 0.0f };
		float       mScroll = 0.0f;
	};
}
