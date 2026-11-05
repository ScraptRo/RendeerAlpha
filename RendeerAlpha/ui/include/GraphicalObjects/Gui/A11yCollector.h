#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <GraphicalObjects/Accessible.h>
#include <GraphicalObjects/Gui/Interaction.h>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/Widget.h>

namespace RDA {

	// What a screen reader is told: widgets describe themselves as the walk reaches them, and what
	// they said is turned into a tree at the end of the frame and handed to the platform's bridge.
	//
	// It collects and builds; it does not act. What a screen reader asked for -- press this, focus
	// that, select there -- is queued here by the bridge and handed back, widget by widget, to Gui,
	// which is the one that can move the focus and edit a field's state.
	//
	// Owned by Gui. Public so that Gui.h can hold one by value.
	class A11yCollector {
	public:
		// The bridge a platform layer listens on; null in a GUI that no window owns.
		void setBridge(AccessibleBridge* bridge) { mBridge = bridge; }
		// Whether this frame's walk is describing anything.
		bool thisFrame() const { return mThisFrame; }
		// The tree built by the last walk, or null when nothing has asked.
		std::shared_ptr<const AccessibleTree> tree() const { return mTree; }
		// A reader has just started listening and has no tree yet, or asked for something to be
		// done: either is a reason to walk.
		bool wantsWalk() const { return mBridge && mBridge->wanted() && (!mBuilt || !mRequests.empty()); }

		// ---- the frame --------------------------------------------------------------------------
		// Every frame: what a screen reader asked for since last frame is taken, before the cache
		// decides, because an action waiting is a reason to walk.
		void beginFrame();
		// A frame that walks: describing, when anybody is listening.
		void beginWalk();
		// Widgets died between the walk and the end of the frame: nothing noted may be followed.
		void forget();
		// Builds the tree from what the walk found and drops requests nothing answered -- a widget
		// that was not drawn: acting on one later would be acting on something nobody saw.
		void endFrame(uint32_t focusedId, float unitScale);

		// ---- what the walk finds ---------------------------------------------------------------------
		// A widget, as it is placed. Returns whether it became a node, with its id.
		bool noteWidget(Widget* widget, const Rect& where, const Interaction& ids, uint32_t& id);
		// An item a widget describes on its own behalf -- a tab title, a list or tree row.
		bool noteItem(Widget* owner, uint32_t id, int item, Widget* row, const Rect& where,
		              const AccessibleInfo& info, const std::string& automationId, bool focused);
		// A text field's caret, selection and lines, added to the node it was described as.
		void noteText(uint32_t id, AccessibleText text);
		// What was asked of the node with this id, taken out of the queue in the order asked.
		std::vector<AccessibleRequest> takeRequestsFor(uint32_t id);

	private:
		// The tree as the walk finds it, before parents are resolved.
		struct Record {
			Widget*        widget = nullptr;   // what it describes, when it is a widget
			Widget*        owner = nullptr;    // for an item: who described it
			Widget*        row = nullptr;      // for an item: the widget its contents are
			int            item = -1;
			uint32_t       id = 0;
			Rect           where{};
			AccessibleInfo info;
			AccessibleText text;
			std::string    automationId;
			bool           focused = false;   // an item standing in for its owner's focus
		};
		AccessibleBridge* mBridge = nullptr;
		bool mThisFrame = false;   // describing during this frame's walk
		bool mBuilt = false;       // a tree has been built since somebody asked
		std::vector<Record> mRecords;
		std::unordered_set<const Widget*> mSeen;   // recorded this frame
		std::vector<AccessibleRequest> mRequests;
		std::shared_ptr<const AccessibleTree> mTree;
		uint64_t mVersion = 0;

		void build(uint32_t focusedId, float unitScale);
		static std::string spokenText(const Widget& widget);
	};
}
