#pragma once
#include <cstdint>
#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/DockTree.h>
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

	// A movable, dockable container — defined in code, holding child widgets like any
	// container. The user drags its title/tab to move it and drops it onto a target;
	// the widgets inside never move on their own. This is the unit of docking.
	class DockContainer : public Widget {
	public:
		DockContainer(std::string id, std::string title)
			: Widget(std::move(id)), title(std::move(title)) {}

		std::string title;
		// Which dock style draws this panel's tab and title bar. The space's own chrome
		// -- splitters, drop guidance -- uses the default, since it belongs to no panel.
		Variant     variant = kDefaultVariant;
		DockSide    dock = DockSide::Floating; // initial placement only; see above
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

		void paint(Gui&, glm::vec2) override {} // driven by the DockSpace, not the retained walk
		void drawContents(Gui& gui, const Rect& body) { paintChildren(gui, glm::vec2(body.x, body.y)); }
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
		bool hasPendingWork() const { return !mPendingSpawns.empty() || mPendingRemove != nullptr; }

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
		// to the end of the frame, so it is safe to call from within a callback.
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
		void applyPendingRemovals();
		// Puts containers that are not in the tree yet where their `dock` hint asks.
		void seedTree(glm::vec2 viewport);
		void drawPane(Gui& gui, DockNode* leaf, bool& busy);
		DockContainer* containerFor(const std::string& id) const;
		DockNode* largestPane() const;
		// A drop within a band of the window border docks across that whole edge, rather
		// than into whichever pane happens to be there.
		DockDrop windowEdgeAt(glm::vec2 pointer, const Rect& area, Rect* previewOut) const;
		// Bumps the revision and drops any node pointer that a collapse could dangle.
		void structuralChange();

		struct ContainerType { std::string title; ContainerBuilder builder; };
		std::unordered_map<std::string, ContainerType> mTypes;
		uint64_t mSpawnCounter = 0;
		DockContainer* mPendingRemove = nullptr;
		// Spawns requested from inside the walk, created at the top of the next update.
		std::vector<std::string> mPendingSpawns;
		uint64_t mRevision = 0;

		std::vector<std::unique_ptr<DockContainer>> mContainers;

		DockLayout mTree;
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

		DockContainer* mDragging = nullptr;      // a floating container being moved
		DockContainer* mResizingFloat = nullptr; // a floating container being resized
		DockNode*      mResizeSplit = nullptr;   // the splitter being dragged, if any
		DockContainer* mPressTab = nullptr;      // a tab pressed, maybe about to detach
		glm::vec2      mPressPos{ 0.0f };
		glm::vec2      mDragOffset{ 0.0f };
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
