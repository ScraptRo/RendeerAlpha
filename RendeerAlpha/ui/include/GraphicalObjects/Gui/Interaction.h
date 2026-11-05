#pragma once
#include <cstdint>
#include <vector>
#include <GraphicalObjects/GuiTypes.h>

namespace RDA {

	// Which widget the pointer is for: the ids widgets are known by, hot and active, and the
	// input a subtree is shown instead of the real one.
	//
	// Immediate-mode interaction is a handful of ids compared against each other. A widget works
	// out its id, asks whether the pointer is inside it and whether it is the one the press went
	// down on, and the answers are all in `hot` and `active`. Nothing here draws and nothing here
	// knows what a widget is; the input is handed in rather than held, because the frame's input
	// is Gui's and a dozen other things read it.
	//
	// Owned by Gui. Public so that Gui.h can hold one by value.
	class Interaction {
	public:
		// ---- ids ---------------------------------------------------------------------------
		uint32_t hashId(const char* str) const;    // FNV-1a
		// The id of a widget inside the current scope: its own hash, combined with the scope.
		uint32_t scopedId(const char* id) const;
		// Push/pop an id scope: widget ids created between them are namespaced by `id`, so two
		// instances of the same content (e.g. two spawned containers) don't share hot/active/
		// focus/edit state. Panels and dock containers do this automatically.
		void pushId(const char* id);
		void popId();
		// A scope whose id is already worked out, as a panel's is.
		void pushScope(uint32_t scope) { mScopeStack.push_back(scope); }
		void popScope() { mScopeStack.pop_back(); }

		// ---- hot and active ------------------------------------------------------------------
		// Read and written by every widget as it is drawn, which is the whole of the protocol.
		uint32_t hot = 0;     // id under the pointer this frame (last writer wins)
		uint32_t active = 0;  // id the pointer went down on (persists across frames)
		// The widget whose press is only a click waiting to happen -- a button, a checkbox.
		// A pick-up may take such a press over, because what it would have done is
		// something a drag can cancel. Anything else that took the press -- a slider, a
		// field, a splitter -- is dragging already, and keeps it.
		uint32_t clickPress = 0;

		// A surface that takes the pointer the way a button does and hands it on raw: hot
		// while the pointer is over it, held from a press of *any* button over it until
		// every button is up again. Held is what keeps a drag that wandered off the edge
		// going, and what stops whatever it wandered over from lighting up meanwhile.
		// On the frame the last button comes up it still answers held, so the release
		// is seen by whoever asked.
		struct PointerArea {
			bool hot = false;
			bool held = false;
		};
		PointerArea pointerArea(const char* id, const Rect& rect, const GuiInput& input);

		// ---- what a subtree is shown ---------------------------------------------------------
		// Each push saves `input` and replaces it; the pop puts it back. Gui hands in its own.
		void pushInert(GuiInput& input);
		void popInert(GuiInput& input);
		void pushPointerAway(GuiInput& input);
		void popPointerAway(GuiInput& input);
		// Only pushInert counts: a pane whose pointer is merely elsewhere is still there.
		bool inert() const { return mInertDepth > 0; }

		// ---- an overlay that owns the pointer --------------------------------------------------
		// Claimed for the next frame, because the overlay pass runs after the walk. See Gui.h.
		void claimPointer() { mPointerClaimedNext = true; }
		bool pointerClaimed() const { return mPointerClaimed; }

		// ---- the frame -----------------------------------------------------------------------
		// Every frame, walked or not.
		void beginFrame(const GuiInput& input);
		// A frame that walks the tree: settles whether an overlay holds the pointer, and takes
		// it from `input` if so. Hot starts again from nothing.
		void beginWalk(GuiInput& input);
		// The real input back into `input`, before the overlay pass.
		void restoreInput(GuiInput& input);

	private:
		std::vector<uint32_t> mScopeStack;

		// The real input, held while a subtree is being shown something else -- nothing
		// at all, or a pointer somewhere else.
		std::vector<GuiInput> mInputStack;
		int                   mInertDepth = 0;   // how many of mInputStack are pushInert's

		// An overlay asked for the pointer last frame, and this frame's walk runs without
		// one. The real input, kept so the overlay pass can have it back.
		bool     mPointerClaimed = false;
		bool     mPointerClaimedNext = false;
		GuiInput mRealInput;
	};
}
