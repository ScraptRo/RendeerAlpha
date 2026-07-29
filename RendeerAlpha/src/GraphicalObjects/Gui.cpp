#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace RDA {

	// A clip large enough to mean "unclipped"; the backend clamps it to the target.
	static constexpr glm::vec4 kFullClip{ 0.0f, 0.0f, 1.0e6f, 1.0e6f };

	void Gui::begin(const GuiInput& input) {
		mInput = input;
		// Everything handed out last frame is free again. Two stores, no deallocation.
		mFrameArena.reset();
		mDraw.clear();
		mClipStack.clear();
		mScopeStack.clear();
		mCurrentClip = kFullClip;
		mCurrentTexture = nullptr;
		mViewportRect = Rect{}; // a Viewport widget re-reports it during the walk below
		mCmdStart = 0;

		// Hot is recomputed from scratch each frame; active persists (a press-drag keeps
		// the same widget active until release).
		mHot = 0;
		mFocusClaimed = false;

		// Walk the retained tree first: it forms the base layer, and any immediate calls
		// the app makes in onUpdate then draw on top of it and win input ties.
		mRetainedRoot.paint(*this, glm::vec2(0.0f));

		// Dockable containers sit above the static tree.
		mDockSpace.update(*this, mInput.viewport);
	}

	void Gui::end() {
		flushCmd();
		// A release anywhere ends the interaction if no widget consumed it.
		if (mInput.released) mActive = 0;
		// A press that landed on no text field drops keyboard focus.
		if (mInput.pressed && !mFocusClaimed) mFocused = 0;

		// Fingerprint the frame's geometry. FNV-1a over the raw vertex/index bytes plus
		// the per-command clip and texture: everything that decides what ends up on
		// screen. Hashing is a linear pass over memory that was just written (so it is
		// cache-warm) and buys skipping the upload — and, in an on-demand loop, the
		// whole frame — whenever the UI is visually idle.
		uint64_t hash = 1469598103934665603ull; // FNV-1a offset basis
		auto feed = [&hash](const void* data, size_t bytes) {
			// FNV-1a folded 8 bytes at a time: the draw list is tens of kilobytes and
			// this runs every frame, so the byte-at-a-time form was the single most
			// expensive thing an idle frame did.
			const uint8_t* p = static_cast<const uint8_t*>(data);
			size_t i = 0;
			for (; i + sizeof(uint64_t) <= bytes; i += sizeof(uint64_t)) {
				uint64_t word;
				std::memcpy(&word, p + i, sizeof(word));
				hash = (hash ^ word) * 1099511628211ull;
			}
			for (; i < bytes; ++i) {
				hash = (hash ^ p[i]) * 1099511628211ull;
			}
		};
		feed(mDraw.vertices.data(), mDraw.vertices.size() * sizeof(GuiVertex));
		feed(mDraw.indices.data(), mDraw.indices.size() * sizeof(uint16_t));
		for (const GuiDrawCmd& cmd : mDraw.commands) {
			feed(&cmd.clip, sizeof(cmd.clip));
			feed(&cmd.indexOffset, sizeof(cmd.indexOffset));
			feed(&cmd.indexCount, sizeof(cmd.indexCount));
			const Texture* texture = cmd.texture;
			feed(&texture, sizeof(texture));
		}

		mDrawChanged = (hash != mDrawVersion);
		mDrawVersion = hash;
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

	// ---- public low-level drawing -------------------------------------------------
	void Gui::drawRect(const Rect& rect, uint32_t color) {
		addRect(rect, color);
	}
	void Gui::drawText(const char* text, glm::vec2 topLeft, uint32_t color) {
		if (mFont && text) addText(topLeft.x, topLeft.y + mFont->ascent(), text, color);
	}
	void Gui::image(const Rect& r, const Texture* texture) {
		if (!texture) return;
		setTexture(texture);
		addQuad(r.x, r.y, r.x + r.w, r.y + r.h, 0.0f, 0.0f, 1.0f, 1.0f, rgba(255, 255, 255));
		setTexture(nullptr); // the image is its own command; go back to the atlas
	}
	void Gui::pushClipRect(const Rect& rect) {
		pushClip(glm::vec4(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h));
	}
	void Gui::popClipRect() {
		popClip();
	}
	Rect Gui::currentClipRect() const {
		return { mCurrentClip.x, mCurrentClip.y, mCurrentClip.z - mCurrentClip.x, mCurrentClip.w - mCurrentClip.y };
	}
	float Gui::measureText(const char* text) const {
		return (mFont && text) ? mFont->textWidth(text) : 0.0f;
	}
	float Gui::lineHeight() const {
		return mFont ? mFont->lineAdvance() : 16.0f;
	}
	void Gui::pushId(const char* id) {
		mScopeStack.push_back(scopedId(id));
	}
	void Gui::popId() {
		if (!mScopeStack.empty()) mScopeStack.pop_back();
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
		setTexture(nullptr); // solid quads sample the atlas' white texel
		glm::vec2 w = mFont ? mFont->whiteUV() : glm::vec2(0.0f);
		addQuad(r.x, r.y, r.x + r.w, r.y + r.h, w.x, w.y, w.x, w.y, color);
	}

	void Gui::addRectRounded(const Rect& r, uint32_t color, float radius) {
		float rad = (std::min)(radius, (std::min)(r.w, r.h) * 0.5f);
		if (rad <= 0.5f || r.w <= 0.0f || r.h <= 0.0f) { addRect(r, color); return; }

		setTexture(nullptr);
		const glm::vec2 uv = mFont ? mFont->whiteUV() : glm::vec2(0.0f);
		// Outline the rounded box, then fan it from the center. Solid colour and a single
		// UV, so the fan needs no per-vertex work beyond its position.
		constexpr int kCornerSegments = 4;
		constexpr int kOutlinePoints = 4 * (kCornerSegments + 1);
		const glm::vec2 corners[4] = {
			{ r.x + r.w - rad, r.y + r.h - rad }, // bottom-right, sweeping 0 -> 90 degrees
			{ r.x + rad,       r.y + r.h - rad }, // bottom-left
			{ r.x + rad,       r.y + rad       }, // top-left
			{ r.x + r.w - rad, r.y + rad       }, // top-right
		};

		// The unit-circle offsets are the same for every rounded rect ever drawn, so they
		// are computed once instead of calling sin/cos per corner per frame.
		static const auto kUnit = [] {
			std::array<glm::vec2, kOutlinePoints> table{};
			for (int c = 0; c < 4; ++c) {
				for (int s = 0; s <= kCornerSegments; ++s) {
					float angle = (3.14159265f * 0.5f) * (static_cast<float>(c) +
					               static_cast<float>(s) / static_cast<float>(kCornerSegments));
					table[c * (kCornerSegments + 1) + s] = { std::cos(angle), std::sin(angle) };
				}
			}
			return table;
		}();

		// Stack-only: no allocation per rounded rect.
		glm::vec2 outline[kOutlinePoints];
		for (int c = 0; c < 4; ++c) {
			for (int s = 0; s <= kCornerSegments; ++s) {
				const int i = c * (kCornerSegments + 1) + s;
				outline[i] = { corners[c].x + kUnit[i].x * rad, corners[c].y + kUnit[i].y * rad };
			}
		}

		const uint16_t center = static_cast<uint16_t>(mDraw.vertices.size());
		mDraw.vertices.push_back({ { r.x + r.w * 0.5f, r.y + r.h * 0.5f }, uv, color });
		for (const glm::vec2& p : outline) {
			mDraw.vertices.push_back({ p, uv, color });
		}
		constexpr int count = kOutlinePoints;
		for (int i = 0; i < count; ++i) {
			mDraw.indices.push_back(center);
			mDraw.indices.push_back(static_cast<uint16_t>(center + 1 + i));
			mDraw.indices.push_back(static_cast<uint16_t>(center + 1 + (i + 1) % count));
		}
	}

	void Gui::addFrame(const Rect& r, uint32_t fill, uint32_t border, float borderWidth, float radius) {
		if (borderWidth <= 0.0f) { addRectRounded(r, fill, radius); return; }
		const float bw = (std::min)(borderWidth, (std::min)(r.w, r.h) * 0.5f);
		addRectRounded(r, border, radius);
		addRectRounded({ r.x + bw, r.y + bw, r.w - 2.0f * bw, r.h - 2.0f * bw },
		               fill, (std::max)(0.0f, radius - bw));
	}

	void Gui::addText(float penX, float baselineY, const char* text, uint32_t color) {
		if (!mFont || !text) return;
		addTextRange(penX, baselineY, text, static_cast<int>(std::strlen(text)), color);
	}

	float Gui::addTextRange(float penX, float baselineY, const char* text, int count, uint32_t color) {
		if (!mFont || !text) return penX;
		setTexture(nullptr);
		for (int i = 0; i < count; ++i) {
			GlyphQuad q;
			if (mFont->quadFor(text[i], penX, baselineY, q)) { // advances penX
				addQuad(q.x0, q.y0, q.x1, q.y1, q.u0, q.v0, q.u1, q.v1, color);
			}
		}
		return penX;
	}

	// ---- clipping / draw commands -------------------------------------------------
	void Gui::flushCmd() {
		uint32_t count = static_cast<uint32_t>(mDraw.indices.size());
		if (count > mCmdStart) {
			mDraw.commands.push_back({ mCurrentClip, mCmdStart, count - mCmdStart, mCurrentTexture });
			mCmdStart = count;
		}
	}

	void Gui::setTexture(const Texture* texture) {
		if (texture != mCurrentTexture) {
			flushCmd();
			mCurrentTexture = texture;
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
	void Gui::beginPanel(const char* id, const Rect& rect, Variant variant) {
		const PanelStyle& s = mTheme.panel(variant);
		mScopeStack.push_back(scopedId(id));
		pushClip(glm::vec4(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h));
		addFrame(rect, s.body, s.border, s.borderWidth, s.radius);          // body
		if (s.accentHeight > 0.0f) {
			addRect({ rect.x, rect.y, rect.w, s.accentHeight }, s.accent);  // accent strip
		}
	}

	void Gui::endPanel() {
		popClip();
		mScopeStack.pop_back();
	}

	void Gui::label(const char* text, glm::vec2 pos, uint32_t color) {
		if (!mFont) return;
		addText(pos.x, pos.y + mFont->ascent(), text, color);
	}

	bool Gui::button(const char* id, const char* text, const Rect& rect, Variant variant) {
		const ButtonStyle& s = mTheme.button(variant);
		uint32_t wid = scopedId(id);
		bool inside = rect.contains(mInput.pointer);
		if (inside) mHot = wid;
		if (mHot == wid && mInput.pressed) mActive = wid;

		bool clicked = false;
		if (mActive == wid && mInput.released) {
			if (mHot == wid) clicked = true;
			mActive = 0;
		}

		uint32_t color = s.normal;
		if (mActive == wid)      color = s.pressed; // pressed
		else if (mHot == wid)    color = s.hovered; // hovered
		addFrame(rect, color, s.border, s.borderWidth, s.radius);

		if (mFont) {
			float tw = mFont->textWidth(text);
			float tx = rect.x + (rect.w - tw) * 0.5f;
			float baseline = rect.y + rect.h * 0.5f + mFont->ascent() * 0.35f;
			addText(tx, baseline, text, s.text);
		}
		return clicked;
	}
}
