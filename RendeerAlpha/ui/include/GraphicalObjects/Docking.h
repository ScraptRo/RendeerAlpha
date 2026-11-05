#pragma once
#include <cstdint>
#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/DockTree.h>
#include <GraphicalObjects/TileGrid.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace RDA {
	class Gui;
	class DockContainer;

	// Populates a freshly spawned container with its widgets. Runs once per instance.
	using ContainerBuilder = std::function<void(DockContainer&)>;

	// Where a container should go *the first time it is seen*, before the user has moved
	// it or a saved layout has been restored. It is a starting hint, not a position:
	// once a container is in the tree, DockLayout owns where it sits, and panels sharing
	// a hint are seeded into one tab group. Floating means "not docked at all".
	//
	// This used to be the position itself, which is why a panel could only ever be at one
	// of five places in the whole window. See DockTree.h for what replaced it.
	enum class DockSide { Floating = 0, Left, Right, Top, Bottom, Center, Count };

	// How a dock space arranges what is in it.
	//
	// Panes is the editor: the area is cut up, every panel is somebody's half, splitters
	// between them, and no space is ever unclaimed. Tiles is the dashboard: each panel
	// has a size of its own in a column grid, there is room between them, and dragging
	// one pushes the ones it lands on rather than re-cutting anything. See TileGrid.h.
	//
	// Two arrangements rather than one that does both, because the two disagree about
	// what a panel *is*. In Panes a panel is a share of its neighbour, so there are tabs
	// and splitters and a drop has four meanings; in Tiles it is a rectangle, so there
	// are none of those and a drop has one. A single mode would have to answer both.
	enum class DockArrange : uint8_t { Panes, Tiles };

	// The chrome a <dock> wears instead of the engine's.
	//
	// A dock panel's bar is drawn in C++: a filled strip, the title as text, an "x" at
	// the far right. Every colour of that is themeable and none of its *shape* is, so a
	// panel that wanted an icon beside its name, or two buttons, or no bar at all, could
	// not have one. This is the answer: the bar is a widget tree like any other, and the
	// engine keeps only the part that cannot be handed over -- the hit test that picks
	// the panel up and carries it to another pane.
	//
	// It is drawn wherever the panel is the only one in its pane, which includes floating
	// and includes every tile. Sharing a pane with others, the tab strip is what the pane
	// wears and this is not drawn at all: a strip belongs to the pane and a header
	// belongs to a panel, and two panels in one group cannot both dress it.
	//
	// Its height is its own `height` when it names one, and the theme's `tabHeight`
	// otherwise -- so a header that says nothing is exactly as tall as the bar it
	// replaced.
	class DockHeader : public Widget {
	public:
		explicit DockHeader(std::string id) : Widget(std::move(id)) {}

		// Driven by the DockSpace, like the panel that holds it, rather than by the
		// retained walk -- it is chrome, and where the chrome goes is the pane's answer.
		void paint(Gui&, glm::vec2) override {}
		void drawHeader(Gui& gui, const Rect& band) {
			paintChildren(gui, glm::vec2(band.x, band.y));
		}

		float bandHeight(float fallback) const {
			return (height.mode == SizeSpec::Mode::Fixed && height.value > 0.0f)
				? height.value : fallback;
		}
	};

	// A movable, dockable container — defined in code, holding child widgets like any
	// container. The user drags its title/tab to move it and drops it onto a target;
	// the widgets inside never move on their own. This is the unit of docking.
	class DockContainer : public Widget {
	public:
		DockContainer(std::string id, std::string title)
			: Widget(std::move(id)), title(std::move(title)) {}
		bool describe(AccessibleInfo& out) const override;

		std::string title;
		// Which dock style draws this panel's tab and title bar. The space's own chrome
		// -- splitters, drop guidance -- uses the default, since it belongs to no panel.
		Variant     variant = kDefaultVariant;
		DockSide    dock = DockSide::Floating; // initial placement only; see above

		// Where it starts in a Tiles arrangement, in grid cells. A negative col or row
		// means "wherever it fits", which is the first hole reading left to right and
		// then down -- so a dashboard can state the sizes and let the grid do the
		// placing. Initial placement only, the same way `dock` is: once a tile is in the
		// grid, where it sits belongs to whoever dragged it.
		int tileCol = -1, tileRow = -1;
		int tileCols = 3, tileRows = 3;
		Rect        floatingRect{ 40.0f, 40.0f, 240.0f, 220.0f };
		float       dockSize = 240.0f; // how wide/tall its first pane should be
		bool        closable = false;  // show a close (x) button; set for spawned instances
		std::string type;              // the container type it was spawned from ("" if added directly)

		// Where it was last *drawn*, which is not always where it was asked to be: a
		// floating panel still arriving, or still shrinking out of the pane it was pulled
		// from, is on its way to its rect. Input is tested against this, so what you can
		// click on is what you can see.
		Rect computedRect{};

		// Never drawn yet, so the next time it is it arrives -- fading and growing into
		// place instead of simply being there. A panel dragged out of a pane is not
		// arriving; it was on the screen a frame ago, and detaching clears this.
		bool fresh = true;

		// The header this panel wears, or nullptr for the engine's own bar. The first one
		// wins: a panel with two headers has a bug in it, and picking one quietly is a
		// kinder answer than drawing both on top of each other.
		DockHeader* header() const;
		// How tall its bar is, given what the theme would have used.
		float headerHeight(float fallback) const;

		void paint(Gui&, glm::vec2) override {} // driven by the DockSpace, not the retained walk
		// Everything except the header, which is drawn in the bar rather than in the body.
		void drawContents(Gui& gui, const Rect& body);
	};

	// Owns a set of DockContainers and, each frame, lays them out over the window: a tree
	// of nested split panes (see DockTree.h) plus any floating windows. Handles tab
	// switching, tab/title dragging, docking onto a pane's edge or the window's, splitter
	// dragging, and closing.
	class DockSpace {
	public:
		// Bumped whenever the set of containers or the shape of the tree changes. The
		// GUI's retained cache watches this so a dock added, moved or closed outside the
		// paint walk still forces a rebuild.
		uint64_t revision() const { return mRevision; }
		// Work queued from inside the walk and not yet applied — the next frame has to
		// run update() rather than reuse cached geometry, or it would never happen.
		bool hasPendingWork() const { return !mPendingSpawns.empty() || !mPendingRemove.empty(); }

		// Panes or Tiles. Set before the first update, the way a container's `dock` is:
		// the two arrangements keep separate state, so changing it mid-run leaves the
		// panels where the other one had them until they are placed again.
		DockArrange arrange = DockArrange::Panes;

		// The grid, when this space arranges in Tiles. Its columns, row height and gap
		// are what a layout's `columns`, `rowHeight` and `gap` set.
		TileGrid&       grid() { return mGrid; }
		const TileGrid& grid() const { return mGrid; }

		DockContainer* add(const char* id, const char* title);
		// Takes a container built elsewhere. The layout front end constructs its own, so
		// that the properties and bindings on a <dock> node are applied by exactly the
		// same code that applies them to every other widget.
		DockContainer* adopt(std::unique_ptr<DockContainer> container);
		DockContainer* find(const char* id);

		// ---- container prototypes ----
		// Register a container type: a title + a builder that fills it with widgets.
		void define(const char* type, const char* title, ContainerBuilder builder);
		// Instantiate a defined type onto the screen (floating, cascaded). Callable any
		// number of times; each instance gets a unique id and isolated widget state.
		// Returns the new container, or nullptr if the type was never defined.
		DockContainer* spawn(const char* type);
		// Remove a container (e.g. from its close button, or programmatically). Deferred
		// to the end of the frame, so it is safe to call from within a callback. Any number
		// may be asked for in one frame, and they are applied in the order they were asked.
		void remove(DockContainer* container);
		void removeById(const char* id);

		// `area` is where the docking happens. It used to be the window's size, with the
		// origin assumed to be zero -- which is exactly why docking could only ever be
		// the whole window. Passing a rectangle is what lets a layout scope it to part
		// of one, and the full-window case is just the rectangle that covers everything.
		void update(Gui& gui, const Rect& area);

		// Moves floating containers that have not been placed yet, once. A floating rect
		// is authored relative to the dock area, so a dock space that is not the whole
		// window puts its floating panels inside itself rather than at the window's
		// top-left corner, where they would simply not be visible.
		void placeFloatingWithin(const Rect& area);

		// The pane arrangement, if the application wants to inspect or drive it directly.
		DockLayout& tree() { return mTree; }
		const DockLayout& tree() const { return mTree; }

		// Save/restore the arrangement. Containers are matched by id, so call
		// loadLayout() after building the tree (the containers must already exist);
		// spawned instances that are gone are recreated from their type. A container the
		// data does not mention keeps its code-defined placement. Reads the current
		// format and migrates the previous five-slot one.
		std::string saveLayout() const;
		void        loadLayout(const std::string& data);
		bool        saveLayoutToFile(const std::string& path) const;
		bool        loadLayoutFromFile(const std::string& path);

		// The body of the pane holding whatever was seeded Center — where a docked
		// Viewport or the 3D scene shows. Falls back to the largest pane, and to the
		// whole window when nothing is docked at all. Valid after update().
		Rect centerArea() const { return mCenterBody; }

	private:
		DockContainer* spawnWithId(const char* type, const char* id);
		// Creates whatever a callback asked to spawn during the last walk. Run at the top
		// of a frame, before anything walks the container list.
		void flushPendingSpawns();

		// ---- Tiles ----
		// The Tiles arrangement's whole frame: placing, dragging, resizing and drawing.
		// Separate from update() rather than folded into it because the two arrangements
		// share the container list and the theme and nothing else. The frame is these
		// steps, in this order.
		void updateTiles(Gui& gui, const Rect& area);
		void placeTiles();
		void advanceTileDrag(Gui& gui, const Rect& area);
		void advanceTileResize(Gui& gui, const Rect& area);
		void pressTiles(Gui& gui, const Rect& area);
		void drawTileLanding(Gui& gui, const Rect& area);
		void drawTiles(Gui& gui, const Rect& area);
		void drawTile(Gui& gui, DockContainer* container, const Rect& rect, bool active,
		              float arrive);

		// Applies what remove() queued, in order.
		void applyPendingRemovals();
		// One container taken apart: out of the tree and the grid, and out of the list.
		void removeNow(DockContainer* container);
		// Puts containers that are not in the tree yet where their `dock` hint asks.
		void seedTree(glm::vec2 viewport);
		void drawPane(Gui& gui, DockNode* leaf, bool& busy);
		// What the corner drag currently means. `into` is the pane it would join with, or
		// null for a split; `side` and `ratio` say where the split would fall.
		struct CornerIntent {
			DockNode* from = nullptr;
			DockNode* into = nullptr;    // join target, or null
			DockDrop  side = DockDrop::None;
			float     ratio = 0.5f;
			Rect      preview{};
			bool      valid = false;
		};
		CornerIntent cornerIntentAt(glm::vec2 pointer) const;

		// ---- Panes ----
		// What one frame of the Panes arrangement works out in one step and a later one
		// needs: where the drag in progress would land, whether something already has hold
		// of the pointer, and the answers that were computed once for the whole frame.
		struct PaneFrame {
			Rect area{};
			DockLayout::Showing showing;
			CornerIntent corner;                   // the corner drag, while one is being made
			// Where a carried floating panel would land.
			Rect       previewRect{};
			DockDrop   previewZone = DockDrop::None;
			DockNode*  previewPane = nullptr;
			bool       wholeEdge = false;          // across a whole side of the area
			bool       busy = false;               // something has the pointer already
			bool       underFloating = false;      // the pointer is over a floating panel
		};
		// update() is these, in order: held things advance, then presses are decided,
		// then everything is drawn from the back to the front.
		PaneFrame beginPaneFrame(const Rect& area);
		void advanceSplitterDrag(Gui& gui, PaneFrame& frame);
		void advanceFloatResize(Gui& gui);
		void advanceTabPress(Gui& gui, PaneFrame& frame);
		void advanceCornerDrag(Gui& gui, PaneFrame& frame);
		void advanceFloatDrag(Gui& gui, PaneFrame& frame);
		void pressFloating(Gui& gui, PaneFrame& frame);
		bool pointerUnderFloating(Gui& gui) const;
		void drawPanes(Gui& gui, PaneFrame& frame);
		void drawSplitters(Gui& gui, PaneFrame& frame);
		void drawFloating(Gui& gui, PaneFrame& frame);
		void drawFloatingPanel(Gui& gui, PaneFrame& frame, DockContainer* container);
		void drawDropGuidance(Gui& gui, PaneFrame& frame);
		void drawClosing(Gui& gui);
		// The grip in a pane's bottom-right corner, and the press that takes it.
		void drawPaneCorner(Gui& gui, DockNode* leaf, const Rect& slot, bool& busy);
		// A figure of the panel being carried, drawn where letting go would put it: the
		// room it would take, outlined, wearing a bar with its name on it.
		//
		// A translucent fill on its own was what this used to be, and a fill laid over a
		// pane that is itself tinted reads as the window having gone blue rather than as
		// a shape with edges. The outline is what makes it a figure and the name is what
		// says which panel the figure is -- which matters most in the one case where the
		// rectangle alone says nothing, a drop into a tab group.
		void drawDropGhost(Gui& gui, const Rect& where, const DockStyle& chrome,
		                   const char* title, bool asTab);
		// Where a tab joining this pane would be drawn: after the ones already in its
		// strip. Answered even for a pane wearing a <header> and therefore showing no
		// strip at all, because joining it is exactly what brings the strip back.
		float tabStripEnd(Gui& gui, const DockNode* leaf) const;
		DockContainer* containerFor(const std::string& id) const;
		DockNode* largestPane() const;
		// A drop within a band of the window border docks across that whole edge, rather
		// than into whichever pane happens to be there.
		DockDrop windowEdgeAt(glm::vec2 pointer, const Rect& area, Rect* previewOut) const;
		// Bumps the revision and drops any node pointer that a collapse could dangle.
		void structuralChange();

		// This space's own corner of the animation table.
		//
		// There is always more than one dock space in a running application: the Gui keeps
		// one of its own, for panels a backend adds through the C ABI, and a <dockspace>
		// in a layout brings another -- and a layout may declare two. Everything else in
		// here is keyed by a panel's id, which is unique across all of them. The drop
		// guidance is keyed by a literal, so every space asked for the same entry: one of
		// them said 1 while it was dragging and the others said 0 in the same frame, each
		// restarting the other's journey, and its clock never left zero. The guidance was
		// permanently a fade that had not started, which is why dropping a panel showed
		// nothing at all about where it would land.
		static uint32_t nextKeySalt();
		const uint32_t mKeySalt = nextKeySalt();

		struct ContainerType { std::string title; ContainerBuilder builder; };
		std::unordered_map<std::string, ContainerType> mTypes;
		uint64_t mSpawnCounter = 0;
		// Removals requested from inside the walk, applied in order at the end of update().
		// A list because more than one thing can close in a frame -- a handler closing a
		// pair, or a press on a close box and a call from the application -- and a single
		// slot kept only the last of them.
		std::vector<DockContainer*> mPendingRemove;
		// Spawns requested from inside the walk, created at the top of the next update.
		std::vector<std::string> mPendingSpawns;
		uint64_t mRevision = 0;

		std::vector<std::unique_ptr<DockContainer>> mContainers;

		DockLayout mTree;
		TileGrid   mGrid;
		// Tiles: what the hand is on, and where on it. The offset is in pixels, so a tile
		// picked up by its right-hand end stays picked up there rather than jumping its
		// top-left corner to the pointer.
		DockContainer* mTileDrag = nullptr;
		DockContainer* mTileResize = nullptr;
		glm::vec2      mTileGrab{ 0.0f };
		Rect           mTileGhost{};   // where the dragged tile is drawn, vs where it will land
		// One container id per placement hint, naming the pane that hint seeded. Held as
		// an id rather than a node pointer because splitting turns a pane into a split;
		// the id survives that, and finds the pane again through the tree.
		std::string mSeedAnchor[static_cast<int>(DockSide::Count)];
		// Containers whose `dock` hint has been acted on. Once placed — by seeding or by
		// a restored layout — a container's position belongs to the user, so a panel
		// they dragged out to float is not quietly pulled back to its hint next frame.
		std::unordered_set<std::string> mSeeded;
		Rect mCenterBody{};
		bool mFloatingPlaced = false;

		// A panel that has been closed and is still fading out.
		//
		// Closing destroys the container, so nothing of it survives to be drawn -- what is
		// kept here is what it looked like: where it was, what it was called, and which
		// variant drew it. Enough for a ghost of the chrome, which is what a panel going
		// away looks like anyway.
		//
		// It leaves the tree immediately, so the panes beside it start taking the space at
		// once and glide into it while this fades over the top. Keeping it in the tree
		// instead would mean a pane that occupies a fraction of a split, which the tree
		// has no way to express.
		struct ClosingPane {
			std::string id;      // for its place in the animation table
			std::string title;
			Variant     variant;
			Rect        rect{};
			bool        started = false;
		};
		std::vector<ClosingPane> mClosing;

		// A pane's corner is being dragged: split it, or join it with a neighbour.
		//
		// Held as a container id rather than a DockNode*, because committing either one
		// restructures the tree and every node pointer with it. The id survives that and
		// finds its pane again.
		std::string mCornerId;
		glm::vec2   mCornerStart{ 0.0f };

		DockContainer* mDragging = nullptr;      // a floating container being moved
		DockContainer* mResizingFloat = nullptr; // a floating container being resized
		DockNode*      mResizeSplit = nullptr;   // the splitter being dragged, if any
		DockContainer* mPressTab = nullptr;      // a tab pressed, maybe about to detach
		glm::vec2      mPressPos{ 0.0f };
		glm::vec2      mDragOffset{ 0.0f };
		// Where a floating panel was taken hold of, and whether it has gone anywhere since.
		// Until it has, a release is a click on its bar -- which brings it to the front and
		// must not also drop it into whatever pane happens to be underneath.
		glm::vec2      mDragFrom{ 0.0f };
		bool           mDragTravelled = false;
	};

	// A dock space as a widget: docking scoped to a rectangle instead of to the window.
	//
	// This is what makes the windowing system optional and placeable. An application that
	// wants none simply has no dock host; one that wants panels in a corner puts a host
	// in that corner and everything outside it lays out normally.
	//
	// Its DockContainer children are the panels, and they are not ordinary children: a
	// docked panel is positioned by the dock space, not by the parent's layout, so they
	// are handed over as they arrive. Anything else stays an ordinary child and paints
	// underneath, which is how a dock area gets a backdrop.
	class DockHost : public Widget {
	public:
		explicit DockHost(std::string id) : Widget(std::move(id)) {}
		~DockHost() override;

		DockSpace& space() { return mSpace; }

		// Where the arrangement is remembered between runs. Empty means it is not: the
		// panels open where the layout says every time.
		//
		// This is the split between the two things that both want to decide where a
		// panel goes. The layout states where a panel starts; once someone drags it, the
		// arrangement is theirs, and this is where it is kept. A saved file wins, and a
		// panel it does not mention keeps what the layout said -- so adding a panel to a
		// layout does not require anyone to delete their arrangement.
		std::string persist;

		Widget* addChild(std::unique_ptr<Widget> child) override;
		void    paint(Gui& gui, glm::vec2 origin) override;

	private:
		DockSpace mSpace;
		bool      mOpened = false;
	};
}
