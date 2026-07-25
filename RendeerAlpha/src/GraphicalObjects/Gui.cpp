#include <GraphicalObjects/Gui.h>

namespace RDA {

	// A clip large enough to mean "unclipped"; the backend clamps it to the target.
	static constexpr glm::vec4 kFullClip{ 0.0f, 0.0f, 1.0e6f, 1.0e6f };

	void Gui::begin(const GuiInput& input) {
		mInput = input;
		mDraw.clear();
		mClipStack.clear();
		mScopeStack.clear();
		mCurrentClip = kFullClip;
		mCmdStart = 0;

		// Hot is recomputed from scratch each frame; active persists (a press-drag keeps
		// the same widget active until release).
		mHot = 0;
	}

	void Gui::end() {
		flushCmd();
		// A release anywhere ends the interaction if no widget consumed it.
		if (mInput.released) mActive = 0;
	}

	// ---- ids ----------------------------------------------------------------------
	uint32_t Gui::hashId(const char* str) const {
		uint32_t hash = 2166136261u; // FNV-1a
		for (const char* c = str; *c; ++c) {
			hash ^= static_cast<uint8_t>(*c);
			hash *= 16777619u;
		}
		return hash;
	}

	uint32_t Gui::scopedId(const char* id) const {
		uint32_t scope = mScopeStack.empty() ? 0u : mScopeStack.back();
		return hashId(id) ^ (scope * 0x9e3779b9u);
	}

	// ---- geometry -----------------------------------------------------------------
	void Gui::addQuad(float x0, float y0, float x1, float y1,
	                  float u0, float v0, float u1, float v1, uint32_t color) {
		uint16_t base = static_cast<uint16_t>(mDraw.vertices.size());
		mDraw.vertices.push_back({ { x0, y0 }, { u0, v0 }, color });
		mDraw.vertices.push_back({ { x1, y0 }, { u1, v0 }, color });
		mDraw.vertices.push_back({ { x1, y1 }, { u1, v1 }, color });
		mDraw.vertices.push_back({ { x0, y1 }, { u0, v1 }, color });
		mDraw.indices.push_back(base);
		mDraw.indices.push_back(base + 1);
		mDraw.indices.push_back(base + 2);
		mDraw.indices.push_back(base);
		mDraw.indices.push_back(base + 2);
		mDraw.indices.push_back(base + 3);
	}

	void Gui::addRect(const Rect& r, uint32_t color) {
		glm::vec2 w = mFont ? mFont->whiteUV() : glm::vec2(0.0f);
		addQuad(r.x, r.y, r.x + r.w, r.y + r.h, w.x, w.y, w.x, w.y, color);
	}

	void Gui::addText(float penX, float baselineY, const char* text, uint32_t color) {
		if (!mFont) return;
		for (const char* c = text; *c; ++c) {
			GlyphQuad q;
			if (mFont->quadFor(*c, penX, baselineY, q)) {
				addQuad(q.x0, q.y0, q.x1, q.y1, q.u0, q.v0, q.u1, q.v1, color);
			}
		}
	}

	// ---- clipping / draw commands -------------------------------------------------
	void Gui::flushCmd() {
		uint32_t count = static_cast<uint32_t>(mDraw.indices.size());
		if (count > mCmdStart) {
			mDraw.commands.push_back({ mCurrentClip, mCmdStart, count - mCmdStart });
			mCmdStart = count;
		}
	}

	void Gui::pushClip(const glm::vec4& clip) {
		flushCmd();
		mClipStack.push_back(mCurrentClip);
		// Intersect with the parent clip so nested panels stay inside their parents.
		mCurrentClip = glm::vec4(
			(std::max)(mCurrentClip.x, clip.x),
			(std::max)(mCurrentClip.y, clip.y),
			(std::min)(mCurrentClip.z, clip.z),
			(std::min)(mCurrentClip.w, clip.w));
	}

	void Gui::popClip() {
		flushCmd();
		mCurrentClip = mClipStack.back();
		mClipStack.pop_back();
	}

	// ---- widgets ------------------------------------------------------------------
	void Gui::beginPanel(const char* id, const Rect& rect) {
		mScopeStack.push_back(scopedId(id));
		pushClip(glm::vec4(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h));
		addRect(rect, rgba(28, 30, 36, 235));                          // body
		addRect({ rect.x, rect.y, rect.w, 3.0f }, rgba(74, 106, 208)); // accent strip
	}

	void Gui::endPanel() {
		popClip();
		mScopeStack.pop_back();
	}

	void Gui::label(const char* text, glm::vec2 pos, uint32_t color) {
		if (!mFont) return;
		addText(pos.x, pos.y + mFont->ascent(), text, color);
	}

	bool Gui::button(const char* id, const char* text, const Rect& rect) {
		uint32_t wid = scopedId(id);
		bool inside = rect.contains(mInput.pointer);
		if (inside) mHot = wid;
		if (mHot == wid && mInput.pressed) mActive = wid;

		bool clicked = false;
		if (mActive == wid && mInput.released) {
			if (mHot == wid) clicked = true;
			mActive = 0;
		}

		uint32_t color = rgba(58, 62, 72);
		if (mActive == wid)      color = rgba(42, 106, 208); // pressed
		else if (mHot == wid)    color = rgba(80, 86, 100);  // hovered
		addRect(rect, color);

		if (mFont) {
			float tw = mFont->textWidth(text);
			float tx = rect.x + (rect.w - tw) * 0.5f;
			float baseline = rect.y + rect.h * 0.5f + mFont->ascent() * 0.35f;
			addText(tx, baseline, text, rgba(235, 236, 240));
		}
		return clicked;
	}
}
