#pragma once
#include <GraphicalObjects/Widget.h>
#include <vendor/RDA_Library/frame_arena.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace RDA {
	class Gui;
	class DockContainer;

	// Populates a freshly spawned container with its widgets. Runs once per instance.
	using ContainerBuilder = std::function<void(DockContainer&)>;

	// Where a container sits. Floating = a free window at floatingRect. The edge sides
	// claim a strip across that edge; Center fills whatever is left (e.g. behind it the
	// 3D scene shows through, or a Viewport widget lives there). Containers sharing a
	// side are shown as tabs in that slot.
	enum class DockSide { Floating = 0, Left, Right, Top, Bottom, Center, Count };

	// A movable, dockable container — defined in code, holding child widgets like any
	// container. The user drags its title/tab to move it and drops it onto a target;
	// the widgets inside never move on their own. This is the unit of docking.
	class DockContainer : public Widget {
	public:
		DockContainer(std::string id, std::string title)
			: Widget(std::move(id)), title(std::move(title)) {}

		std::string title;
		DockSide    dock = DockSide::Floating;
		Rect        floatingRect{ 40.0f, 40.0f, 240.0f, 220.0f };
		float       dockSize = 240.0f; // seeds a slot's width/height the first time it docks there
		bool        closable = false;  // show a close (x) button; set for spawned instances
		std::string type;              // the container type it was spawned from ("" if added directly)

		Rect computedRect{}; // on-screen region (incl. tab/title bar), filled each frame

		void paint(Gui&, glm::vec2) override {} // driven by the DockSpace, not the retained walk
		void drawContents(Gui& gui, const Rect& body) { paintChildren(gui, glm::vec2(body.x, body.y)); }
	};

	// Owns a set of DockContainers and, each frame, lays them out over the window with
	// tabbed edge/center slots, handles tab switching, tab/title dragging, docking via a
	// 5-way drop overlay, and edge resizing.
	class DockSpace {
	public:
		DockContainer* add(const char* id, const char* title);
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

		void update(Gui& gui, glm::vec2 viewport);

		// Save/restore where containers are docked. Containers are matched by id, so call
		// loadLayout() after building the tree (the containers must already exist). A
		// missing/unknown id in the data is ignored; a container not in the data keeps its
		// code-defined placement. Persist across runs with the file helpers.
		std::string saveLayout() const;
		void        loadLayout(const std::string& data);
		bool        saveLayoutToFile(const std::string& path) const;
		bool        loadLayoutFromFile(const std::string& path);

		// The Center slot's body region (where a docked Viewport / the scene shows),
		// valid after update(). If nothing is docked Center it is the whole leftover area.
		Rect centerArea() const { return mCenterBody; }

	private:
		static constexpr int kSides = static_cast<int>(DockSide::Count);
		void computeLayout(RDL::frame_arena& arena, glm::vec2 viewport);
		DockSide dropTargetAt(glm::vec2 pointer, glm::vec2 viewport, Rect* previewOut) const;
		DockContainer* spawnWithId(const char* type, const char* id);
		void applyPendingRemovals();

		struct ContainerType { std::string title; ContainerBuilder builder; };
		std::unordered_map<std::string, ContainerType> mTypes;
		uint64_t mSpawnCounter = 0;
		DockContainer* mPendingRemove = nullptr;

		std::vector<std::unique_ptr<DockContainer>> mContainers;

		// Per-frame layout, indexed by DockSide. The tab lists point into the Gui's frame
		// arena and are only valid for the frame that built them.
		DockContainer** mTabs[6]{};
		int             mTabCount[6]{};
		Rect mSlotRect[6]{};
		float mSlotSize[6]{}; // Left/Right width, Top/Bottom height; 0 = seed from container
		DockContainer* mActive[6]{}; // active tab per slot
		Rect mCenterBody{};

		DockContainer* mDragging = nullptr;   // a floating container being moved
		DockContainer* mResizingFloat = nullptr; // a floating container being resized
		DockSide       mResizeSide = DockSide::Floating; // != Floating while resizing that edge
		DockContainer* mPressTab = nullptr;   // a tab pressed, maybe about to detach
		glm::vec2      mPressPos{ 0.0f };
		glm::vec2      mDragOffset{ 0.0f };
	};
}
