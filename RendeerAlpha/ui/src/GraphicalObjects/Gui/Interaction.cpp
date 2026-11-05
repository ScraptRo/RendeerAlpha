#include <GraphicalObjects/Gui/Interaction.h>
#include <cstdint>
#include <utility>

// Interaction: ids, hot and active, and the input a subtree is shown instead of the real one.
namespace RDA {

	// ---- ids ----------------------------------------------------------------------
	uint32_t Interaction::hashId(const char* str) const {
		uint32_t hash = 2166136261u; // FNV-1a
		for (const char* c = str; *c; ++c) {
			hash ^= static_cast<uint8_t>(*c);
			hash *= 16777619u;
		}
		return hash;
	}

	uint32_t Interaction::scopedId(const char* id) const {
		uint32_t scope = mScopeStack.empty() ? 0u : mScopeStack.back();
		return hashId(id) ^ (scope * 0x9e3779b9u);
	}

	void Interaction::pushId(const char* id) {
		mScopeStack.push_back(scopedId(id));
	}
	void Interaction::popId() {
		if (!mScopeStack.empty()) mScopeStack.pop_back();
	}

	void Interaction::pushInert(GuiInput& input) {
		mInputStack.push_back(input);
		++mInertDepth;
		GuiInput quiet;
		// What a frame still needs to know while nothing is being done to it: how big the
		// window is, and how long the frame was. A pointer nowhere near anything is what
		// makes every hit test fail, and everything else stays at its empty default.
		quiet.viewport = input.viewport;
		quiet.dt = input.dt;
		quiet.pointer = glm::vec2(-1.0e6f);
		input = quiet;
	}

	void Interaction::pushPointerAway(GuiInput& input) {
		mInputStack.push_back(input);
		input.pointer = glm::vec2(-1.0e6f);
		input.lDown = input.lPressed = input.lReleased = false;
		input.rDown = input.rPressed = input.rReleased = false;
		input.mDown = input.mPressed = input.mReleased = false;
		input.scroll = 0.0f;
		input.droppedFiles.clear();
	}

	void Interaction::popInert(GuiInput& input) {
		if (mInputStack.empty()) return;
		input = std::move(mInputStack.back());
		mInputStack.pop_back();
		if (mInertDepth > 0) --mInertDepth;
	}

	void Interaction::popPointerAway(GuiInput& input) {
		if (mInputStack.empty()) return;
		input = std::move(mInputStack.back());
		mInputStack.pop_back();
	}

	Interaction::PointerArea Interaction::pointerArea(const char* id, const Rect& rect,
	                                                  const GuiInput& input) {
		const uint32_t wid = scopedId(id);
		if (rect.contains(input.pointer)) this->hot = wid;
		const bool hot = (this->hot == wid);
		const bool anyPressed = input.lPressed || input.rPressed || input.mPressed;
		if (hot && anyPressed) this->active = wid;

		const bool held = (this->active == wid);
		const bool anyDown = input.lDown || input.rDown || input.mDown;
		if (held && !anyDown) this->active = 0;
		return { hot, held };
	}

	// ---- the frame ----------------------------------------------------------------
	void Interaction::beginFrame(const GuiInput& input) {
		mScopeStack.clear();
		if (input.lPressed) clickPress = 0;
	}

	void Interaction::beginWalk(GuiInput& input) {
		// An overlay asked for the pointer last frame. The walk below runs without one --
		// nothing underneath hovers, highlights, takes focus or fires -- and the real
		// input is put back before the overlay pass, which is where the popup and its
		// dismissal read it.
		//
		// The claim is re-made every frame the overlay is open, so nothing has to
		// remember to release it.
		mPointerClaimed = mPointerClaimedNext;
		mPointerClaimedNext = false;
		mRealInput = input;
		if (mPointerClaimed) {
			// Far enough outside that no rect contains it, rather than a flag every
			// widget would have to remember to ask about.
			input.pointer = glm::vec2(-1.0e6f, -1.0e6f);
			input.lDown = input.lPressed = input.lReleased = false;
			input.scroll = 0.0f;
			// And the keys, as far as shortcuts go: the interface behind a menu is behind
			// it for the keyboard too, so Delete in a dialog does not also delete the row
			// the dialog is about. The overlay pass gets them back with the pointer.
			input.chords.clear();
		}
		mInputStack.clear();
		mInertDepth = 0;
		// Hot is recomputed from scratch each frame; active persists (a press-drag keeps
		// the same widget active until release).
		hot = 0;
	}

	void Interaction::restoreInput(GuiInput& input) {
		input = mRealInput;
	}
}
