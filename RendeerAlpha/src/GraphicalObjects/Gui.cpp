#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace RDA {

	// A clip large enough to mean "unclipped"; the backend clamps it to the target.
	static constexpr glm::vec4 kFullClip{ 0.0f, 0.0f, 1.0e6f, 1.0e6f };

	bool Gui::canReuseRetained() const {
		if (!mCacheValid || mLayoutDirty) return false;

		// Any input at all could move a hover highlight, a caret or a dock, so the tree
		// has to be walked to find out.
		if (mInput.pointer != mLastPointer) return false;
		if (mInput.down || mInput.pressed || mInput.released) return false;
		if (mInput.scroll != 0.0f) return false;
		if (!mInput.typed.empty() || !mInput.editKeys.empty()) return false;
		if (mInput.copy || mInput.cut || mInput.paste || mInput.selectAll || mInput.submit) return false;

		// A focused field blinks its caret, which is geometry changing on a timer.
		if (mFocused != 0) return false;

		// The layout is measured against the target, and a Viewport widget's quad samples
		// whatever texture it was given — either changing invalidates the geometry.
		if (mInput.viewport != mLastViewport) return false;
		if (mSceneTexture != mLastSceneTexture) return false;

		// Docks added or closed outside the walk, or queued from inside it and still
		// waiting to be applied.
		if (mDockSpace.revision() != mLastDockRevision) return false;
		if (mDockSpace.hasPendingWork()) return false;

		return true;
	}

	void Gui::begin(const GuiInput& input) {
		mInput = input;
		// Everything handed out last frame is free again. Two stores, no deallocation.
		mFrameArena.reset();
		mClipStack.clear();
		mScopeStack.clear();
		// Seeded to the target rather than to "infinite": widgets read the current clip
		// as the area they are laid out inside, so the outermost one has to be a real
		// size or anything anchored at the root would stretch to the fallback's extent.
		mCurrentClip = (input.viewport.x > 0.0f && input.viewport.y > 0.0f)
			? glm::vec4(0.0f, 0.0f, input.viewport.x, input.viewport.y)
			: kFullClip;
		mCurrentTexture = nullptr;
		mFocusClaimed = false;
		mScrollConsumed = false;

		// Everything queued from last frame's callbacks lands here, before anything walks
		// the tree — the one point where restructuring is unambiguously safe. A structural
		// edit obviously invalidates the cached geometry.
		if (!mTree.empty()) mLayoutDirty = true;
		mTree.flush();

		if (canReuseRetained()) {
			// Truncate back to where the retained walk finished last frame. The geometry
			// is already in the buffers, so this costs three size assignments and no
			// copying; the application's immediate calls then append after it as usual.
			mDraw.vertices.resize(mRetainedVertices);
			mDraw.indices.resize(mRetainedIndices);
			mDraw.commands.resize(mRetainedCommands);
			mCmdStart = static_cast<uint32_t>(mRetainedIndices);
			// `mHot` and `mViewportRect` are deliberately left alone: without input they
			// still describe the situation the skipped walk would have reproduced.
			++mCacheStats.reused;
			return;
		}

		mDraw.clear();
		mViewportRect = Rect{}; // a Viewport widget re-reports it during the walk below
		mCmdStart = 0;
		// Hot is recomputed from scratch each frame; active persists (a press-drag keeps
		// the same widget active until release).
		mHot = 0;

		{
			// Guarded: any direct structural edit from inside the walk now warns instead
			// of quietly corrupting the container it happens in.
			WidgetWalkGuard guard;

			// Walk the retained tree first: it forms the base layer, and any immediate
			// calls the app makes in onUpdate then draw on top of it and win input ties.
			mRetainedRoot.paint(*this, glm::vec2(0.0f));

			// Dockable containers sit above the static tree.
			mDockSpace.update(*this, mInput.viewport);
		}

		// Close the in-progress command so the retained portion is a whole number of draw
		// commands; immediate calls then start a fresh one and the split is clean.
		flushCmd();
		mRetainedVertices = mDraw.vertices.size();
		mRetainedIndices = mDraw.indices.size();
		mRetainedCommands = mDraw.commands.size();
		mLastPointer = mInput.pointer;
		mLastViewport = mInput.viewport;
		mLastSceneTexture = mSceneTexture;
		mLastDockRevision = mDockSpace.revision();
		mLayoutDirty = false;
		mCacheValid = true;
		++mCacheStats.walked;
	}

	void debugLogDrawData(const char* label, const GuiDrawData& data) {
		RDA_LOG_INFO("draw[" << label << "] vertices=" << data.vertices.size()
			<< " indices=" << data.indices.size() << " commands=" << data.commands.size());
		for (size_t i = 0; i < data.commands.size(); ++i) {
			const GuiDrawCmd& c = data.commands[i];
			const uint64_t end = static_cast<uint64_t>(c.indexOffset) + c.indexCount;
			RDA_LOG_INFO("  cmd[" << i << "] range=" << c.indexOffset << ".." << end
				<< (end > data.indices.size() ? " PAST-END" : "")
				<< " clip=(" << c.clip.x << "," << c.clip.y << ")-(" << c.clip.z << "," << c.clip.w << ")"
				<< " texture=" << (const void*)c.texture);
		}
	}

	void debugLogCommandVertices(const char* label, const GuiDrawData& data,
	                             size_t commandIndex, size_t limit) {
		if (commandIndex >= data.commands.size()) {
			RDA_LOG_WARNING("draw[" << label << "] no command " << commandIndex);
			return;
		}
		const GuiDrawCmd& c = data.commands[commandIndex];
		RDA_LOG_INFO("draw[" << label << "] command " << commandIndex << " vertices:");
		size_t shown = 0;
		for (uint32_t i = c.indexOffset; i < c.indexOffset + c.indexCount && shown < limit; ++i, ++shown) {
			if (i >= data.indices.size()) { RDA_LOG_WARNING("  index " << i << " past end"); break; }
			const uint16_t vi = data.indices[i];
			if (vi >= data.vertices.size()) { RDA_LOG_WARNING("  vertex " << vi << " past end"); break; }
			const GuiVertex& v = data.vertices[vi];
			RDA_LOG_INFO("  i[" << i << "]=" << vi << " pos=(" << v.pos.x << "," << v.pos.y
				<< ") uv=(" << v.uv.x << "," << v.uv.y << ")");
		}
	}

	bool Gui::appendDrawData(const GuiDrawData& data) {
		if (data.vertices.empty() || data.commands.empty()) return true; // nothing to add

		// Indices are 16-bit and are rebased onto the end of this frame's vertices, so
		// the merged buffer has to stay inside what one can name. Refusing is better than
		// wrapping silently, which would draw whatever happened to be at the low indices.
		const size_t base = mDraw.vertices.size();
		if (base + data.vertices.size() > 0xFFFFu) return false;

		const uint32_t indexBase = static_cast<uint32_t>(mDraw.indices.size());
		mDraw.vertices.insert(mDraw.vertices.end(), data.vertices.begin(), data.vertices.end());
		mDraw.indices.reserve(mDraw.indices.size() + data.indices.size());
		for (uint16_t index : data.indices) {
			mDraw.indices.push_back(static_cast<uint16_t>(base + index));
		}
		for (GuiDrawCmd cmd : data.commands) {
			cmd.indexOffset += indexBase;
			mDraw.commands.push_back(cmd);
		}

		// The appended indices already belong to the commands that came with them, so the
		// in-progress command has to start *after* them. Without this, end()'s flushCmd()
		// spans from wherever it was to the new end and emits a second command covering
		// everything appended — drawing it again with this GUI's own texture, which for an
		// image batch means sampling the font atlas instead.
		mCmdStart = static_cast<uint32_t>(mDraw.indices.size());
		return true;
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
