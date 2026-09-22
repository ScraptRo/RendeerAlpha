#include <GraphicalObjects/Gui.h>
#include <GraphicalObjects/Images.h>
#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Viewports.h>
#include <Core/Tables.h>
#include <Core/Utf8.h>
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
		if (mInput.undo || mInput.redo) return false;

		// A focused field blinks its caret, which is geometry changing on a timer.
		if (mFocused != 0) return false;

		// And so is a colour on its way somewhere. The cache decides by looking at input,
		// and an animation is the one thing that changes without any.
		if (mMotion.moving()) return false;

		// The layout is measured against the target, and a Viewport widget's quad samples
		// whatever texture it was given — either changing invalidates the geometry.
		if (mInput.viewport != mLastViewport) return false;
		if (mSceneTexture != mLastSceneTexture) return false;
		// A backend replaced what a <viewport> is drawing. Nothing else about the frame
		// says so -- same tree, same input -- so the drawings have a revision of their own.
		if (viewports().revision() != mLastViewportRevision) return false;
		// And the theme was swapped underneath it. Everything on screen is a different
		// colour and not one of the checks above can tell.
		if (mTheme.revision() != mLastThemeRevision) return false;
		// And rows arrived, or changed, under a <list>. Same tree, same input, different
		// contents -- and until this was here, a transcript that was only being appended
		// to reached the screen when the reader happened to move the mouse.
		if (tables().revision() != mLastTableRevision) return false;
		// And the atlas learned a character. A glyph baked on demand usually changes the
		// text's width too, which would be caught by the geometry differing -- but one
		// whose advance happens to match what the replacement box had would otherwise sit
		// there as a box until something else moved.
		if (mFont && mFont->revision() != mLastFontRevision) return false;
		// And a picture a backend registered was replaced. Same tree, same input, a
		// different image -- a preview updated while nobody touched the mouse would
		// otherwise sit there showing the one before it.
		if (images().revision() != mLastImageRevision) return false;
		// And a stream got a frame. Same tree, same input, a different picture -- a feed
		// that only advanced when the pointer moved would be the same bug twice.
		if (streams().revision() != mLastStreamRevision) return false;
		// And something asked to be looked at again -- an icon part-way through its loop.
		if (mAwake) return false;

		// Docks added or closed outside the walk, or queued from inside it and still
		// waiting to be applied.
		if (mDockSpace.revision() != mLastDockRevision) return false;
		if (mDockSpace.hasPendingWork()) return false;

		return true;
	}

	void Gui::begin(const GuiInput& input) {
		mInput = input;
		// Before anything asks: a widget's paint reads where a value is *now*, so the
		// stepping has to have happened. Values nobody asked for last frame are dropped
		// here too, which is what keeps the table the size of what is on screen.
		mMotion.step(input.dt);
		// Before anything is drawn and whether or not the tree is walked: the clear happens
		// on the render path, and a value that stops being asked for stops moving.
		{
			const BackgroundStyle& ground = mTheme.background();
			mBackgroundColor = mMotion.colour(kMotionBackground, ground.color,
			                                  ground.motion.seconds, ground.motion.curve);
		}
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
		mFocusables.clear();
		// Tab moves the keyboard on. Read here, spent at the end of the frame: a text
		// field in code mode inserts spaces instead and says so by consuming it.
		mFocusMove = 0;
		mFocusMoveConsumed = false;
		for (const GuiEditKey key : mInput.editKeys) {
			if (key == GuiEditKey::Tab) mFocusMove = mInput.shift ? -1 : 1;
		}
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

		// Only now: this frame walks the tree, so this is a frame on which every value
		// still in use will be asked for. Ageing on a reused frame would forget a
		// colour that is sitting there perfectly visible.
		mMotion.forget();

		// Asked for again by whatever is still playing, every frame it still is. Cleared
		// here rather than when it settles, so nothing has to remember to stop asking.
		mAwake = false;
		// The same: a grip is held for as long as it is re-declared and the pointer is
		// still down, so letting go needs no call.
		mWindowGrabbed = false;

		mDraw.clear();
		mOverlays.clear();
		// Where things are is a fact about this frame, so it is thrown away at the top of
		// it. What is *wanted* is not, and persists.
		mAnchorRect.clear();

		// An overlay asked for the pointer last frame. The walk below runs without one --
		// nothing underneath hovers, highlights, takes focus or fires -- and the real
		// input is put back before the overlay pass, which is where the popup and its
		// dismissal read it.
		//
		// The claim is re-made every frame the overlay is open, so nothing has to
		// remember to release it.
		mPointerClaimed = mPointerClaimedNext;
		mPointerClaimedNext = false;
		mRealInput = mInput;
		if (mPointerClaimed) {
			// Far enough outside that no rect contains it, rather than a flag every
			// widget would have to remember to ask about.
			mInput.pointer = glm::vec2(-1.0e6f, -1.0e6f);
			mInput.down = mInput.pressed = mInput.released = false;
			mInput.scroll = 0.0f;
		}
		mOpacity = 1.0f;
		mOpacityStack.clear();
		mInputStack.clear();
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
			mDockSpace.update(*this, Rect{ 0.0f, 0.0f, mInput.viewport.x, mInput.viewport.y });

			// And anything that asked to draw over the lot -- an open dropdown, which has
			// to escape the panel that clips the widget it belongs to. The clip goes back
			// to the whole viewport for these, and they are painted in the order they
			// asked, so the last one to open is on top.
			//
			// Copied first: a paintAbove is allowed to ask again for next frame, and
			// growing the list being iterated would invalidate the iterator.
			const std::vector<std::pair<Widget*, glm::vec2>> above = mOverlays;
			mOverlays.clear();
			// The real pointer, for whatever is in front. Restored here rather than at the
			// end of the frame, because an overlay is exactly the thing that should have
			// it -- and restored unconditionally, since a frame where nothing claimed it
			// kept a copy of the same input anyway.
			mInput = mRealInput;
			for (const auto& one : above) {
				pushClip(glm::vec4(0.0f, 0.0f, mInput.viewport.x, mInput.viewport.y));
				one.first->paintAbove(*this, one.second);
				popClip();
			}
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
		mLastViewportRevision = viewports().revision();
		mLastThemeRevision = mTheme.revision();
		mLastTableRevision = tables().revision();
		if (mFont) mLastFontRevision = mFont->revision();
		mLastImageRevision = images().revision();
		mLastStreamRevision = streams().revision();
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

	void Gui::end() {
		// The ring first, and before the flush: it is geometry like everything else, and
		// added after the last command is closed it would never reach one. Drawn last
		// among the widgets, so it sits on top of whatever it surrounds.
		for (const Focusable& f : mFocusables) {
			if (f.id != mFocused) continue;
			const FocusStyle& ring = mTheme.focus(f.ring);
			if (ring.width <= 0.0f) break;
			const Rect r{ f.rect.x + ring.inset, f.rect.y + ring.inset,
			              f.rect.w - ring.inset * 2.0f, f.rect.h - ring.inset * 2.0f };
			// Four edges rather than addFrame: that draws the border as a full rect and
			// lays the fill on top, so a ring with nothing to fill would come out solid.
			// This has to leave the widget underneath visible -- it is the thing being
			// pointed at.
			const float t = ring.width;
			addRect({ r.x, r.y, r.w, t }, ring.color);                         // top
			addRect({ r.x, r.y + r.h - t, r.w, t }, ring.color);               // bottom
			addRect({ r.x, r.y + t, t, (std::max)(0.0f, r.h - t * 2.0f) }, ring.color);
			addRect({ r.x + r.w - t, r.y + t, t, (std::max)(0.0f, r.h - t * 2.0f) }, ring.color);
			break;
		}

		flushCmd();
		// A release anywhere ends the interaction if no widget consumed it.
		if (mInput.released) mActive = 0;
		// A press that landed on nothing focusable drops the keyboard.
		if (mInput.pressed && !mFocusClaimed) mFocused = 0;

		// And Tab, once everything has had its turn to register.
		if (mFocusMove != 0 && !mFocusMoveConsumed && !mFocusables.empty()) {
			size_t at = mFocusables.size(); // "nowhere yet"
			for (size_t i = 0; i < mFocusables.size(); ++i) {
				if (mFocusables[i].id == mFocused) { at = i; break; }
			}
			const size_t count = mFocusables.size();
			size_t next;
			if (at == count) {
				// Nothing had it: forwards starts at the first, backwards at the last.
				next = (mFocusMove > 0) ? 0 : count - 1;
			} else {
				next = (mFocusMove > 0) ? (at + 1) % count : (at + count - 1) % count;
			}
			mFocused = mFocusables[next].id;
		}

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

	void Gui::drawRectRounded(const Rect& rect, uint32_t color, float radius) {
		addRectRounded(rect, color, radius);
	}
	void Gui::drawText(const char* text, glm::vec2 topLeft, uint32_t color, TextStyle font) {
		if (mFont && text) addText(topLeft.x, topLeft.y + baseline(font), text, color, font);
	}
	void Gui::image(const Rect& r, const Texture* texture, uint32_t tint) {
		if (!texture) return;
		setTexture(texture);
		addQuad(r.x, r.y, r.x + r.w, r.y + r.h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
		setTexture(nullptr); // the image is its own command; go back to the atlas
	}
	void Gui::pushOpacity(float alpha) {
		mOpacityStack.push_back(mOpacity);
		// Multiplied, not replaced: a half-faded thing inside a half-faded thing is a
		// quarter there, which is what nesting has to mean for it to compose.
		mOpacity *= (alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha));
	}

	void Gui::popOpacity() {
		if (mOpacityStack.empty()) return;
		mOpacity = mOpacityStack.back();
		mOpacityStack.pop_back();
	}

	Gui::ScrollBarLook Gui::scrollBar(uint32_t key, bool needed, bool hot,
	                                  float thumbSpan, float trackSpan,
	                                  const TextFieldStyle& style) {
		ScrollBarLook look;
		look.presence = needed ? 1.0f : 0.0f;
		// A bar nobody needs is one whose handle fills it. Content shrinking to fit ends
		// with the handle as long as the track, which is what "all of it is showing"
		// looks like, and the bar fades from there -- rather than the handle collapsing
		// to nothing on its way out, which reads as a fault.
		look.thumb = needed ? thumbSpan : trackSpan;
		look.colour = hot ? style.scrollThumbHover : style.scrollThumb;

		const float seconds = style.motion.seconds;
		if (seconds <= 0.0f) return look; // no duration asked for, and none stored
		const Easing curve = style.motion.curve;
		look.presence = mMotion.value(key + 0u, look.presence, seconds, curve);
		look.thumb    = mMotion.value(key + 1u, look.thumb, seconds, curve);
		look.colour   = mMotion.colour(key + 2u, look.colour, seconds, curve);
		return look;
	}

	void Gui::pushInert() {
		mInputStack.push_back(mInput);
		GuiInput quiet;
		// What a frame still needs to know while nothing is being done to it: how big the
		// window is, and how long the frame was. A pointer nowhere near anything is what
		// makes every hit test fail, and everything else stays at its empty default.
		quiet.viewport = mInput.viewport;
		quiet.dt = mInput.dt;
		quiet.pointer = glm::vec2(-1.0e6f);
		mInput = quiet;
	}

	void Gui::popInert() {
		if (mInputStack.empty()) return;
		mInput = std::move(mInputStack.back());
		mInputStack.pop_back();
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
	// Bold is drawn twice, this far apart. It scales with the size so a heading is
	// emboldened as much as a caption is, proportionally, and it never changes an
	// advance -- the second pass bleeds a pixel to the right and that is all, which is
	// what keeps a bold word in the same monospaced column as a regular one.
	static float boldOffset(float pixelHeight) {
		return std::max(1.0f, std::round(pixelHeight / 16.0f));
	}

	float Gui::measureText(const char* text, TextStyle font) const {
		if (!mFont || !text) return 0.0f;
		const int index = mFont->indexForSize(font.size);
		float width = mFont->textWidth(text, index);
		// The bleed, once, so a bounding box drawn from this contains what is drawn.
		if (font.bold && width > 0.0f) width += boldOffset(mFont->pixelHeight(index));
		return width;
	}
	float Gui::lineHeight(TextStyle font) const {
		return mFont ? mFont->lineAdvance(mFont->indexForSize(font.size)) : 16.0f;
	}
	float Gui::baseline(TextStyle font) const {
		return mFont ? mFont->ascent(mFont->indexForSize(font.size)) : 12.0f;
	}

	std::vector<std::pair<size_t, size_t>> Gui::wrapText(const std::string& text, float width,
	                                                     TextStyle font) const {
		std::vector<std::pair<size_t, size_t>> lines;
		if (!mFont) { lines.emplace_back(0, text.size()); return lines; }
		const int index = mFont->indexForSize(font.size);

		// One pass, character by character, remembering the last space. A word that does
		// not fit on a line of its own is broken rather than allowed to overflow: a long
		// path or hash has no space in it, and silently running off the edge is worse
		// than an ugly break.
		//
		// The offsets returned are byte offsets, because that is what a caller slices a
		// std::string with, but the walk advances a codepoint at a time -- so a break
		// never lands inside a character.
		size_t lineStart = 0;
		size_t lastSpace = std::string::npos;
		float  penX = 0.0f;
		float  widthAtSpace = 0.0f;

		size_t i = 0;
		while (i < text.size()) {
			if (text[i] == '\n') {
				lines.emplace_back(lineStart, i - lineStart);
				lineStart = i + 1;
				lastSpace = std::string::npos;
				penX = 0.0f;
				++i;
				continue;
			}
			uint32_t cp = 0;
			const size_t step = Utf8::decode(text.data() + i, text.data() + text.size(), cp);
			const float advance = mFont->advance(cp, index);
			if (cp == ' ') { lastSpace = i; widthAtSpace = penX; }

			if (penX + advance > width && i > lineStart) {
				if (lastSpace != std::string::npos && lastSpace > lineStart) {
					lines.emplace_back(lineStart, lastSpace - lineStart);
					lineStart = lastSpace + 1;
					penX -= widthAtSpace + mFont->advance(U' ', index);
				} else {
					lines.emplace_back(lineStart, i - lineStart);
					lineStart = i;
					penX = 0.0f;
				}
				lastSpace = std::string::npos;
			}
			penX += advance;
			i += step;
		}
		lines.emplace_back(lineStart, text.size() - lineStart);
		return lines;
	}
	void Gui::drawAbove(Widget* widget, glm::vec2 origin) {
		if (widget) mOverlays.emplace_back(widget, origin);
	}

	void Gui::wantAnchor(const std::string& path) {
		if (!path.empty()) mAnchorWanted.insert(path);
	}

	bool Gui::anchorRect(const std::string& path, Rect& out) const {
		const auto at = mAnchorRect.find(path);
		if (at == mAnchorRect.end()) return false;
		out = at->second;
		return true;
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
		// The one place all GUI geometry passes through, which is why the fade lives here
		// rather than in each widget: a fill, a glyph and a picture are all a quad with a
		// colour, so scaling the alpha once covers a whole tree.
		if (mOpacity < 1.0f) color = fadeTo(color, mOpacity);
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

	void Gui::addQuadPoints(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
	                        uint32_t color) {
		if (mOpacity < 1.0f) color = fadeTo(color, mOpacity);
		const glm::vec2 uv = mFont ? mFont->whiteUV() : glm::vec2(0.0f);
		uint16_t base = static_cast<uint16_t>(mDraw.vertices.size());
		mDraw.vertices.push_back({ { p0.x, p0.y }, { uv.x, uv.y }, color });
		mDraw.vertices.push_back({ { p1.x, p1.y }, { uv.x, uv.y }, color });
		mDraw.vertices.push_back({ { p2.x, p2.y }, { uv.x, uv.y }, color });
		mDraw.vertices.push_back({ { p3.x, p3.y }, { uv.x, uv.y }, color });
		mDraw.indices.push_back(base);
		mDraw.indices.push_back(base + 1);
		mDraw.indices.push_back(base + 2);
		mDraw.indices.push_back(base);
		mDraw.indices.push_back(base + 2);
		mDraw.indices.push_back(base + 3);
	}

	void Gui::drawLine(glm::vec2 from, glm::vec2 to, uint32_t color, float width) {
		const glm::vec2 along = to - from;
		const float length = std::sqrt(along.x * along.x + along.y * along.y);
		if (length <= 0.0001f || width <= 0.0f) return;

		// Half a thickness out either side of the centre line. A line is its own quad
		// rather than a rotated rect because there is no transform in this pipeline:
		// every vertex is already in interface pixels.
		const glm::vec2 across = glm::vec2{ -along.y, along.x } / length * (width * 0.5f);
		setTexture(nullptr);
		addQuadPoints(from - across, from + across, to + across, to - across, color);
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

	void Gui::addText(float penX, float baselineY, const char* text, uint32_t color,
	                  TextStyle font) {
		if (!mFont || !text) return;
		addTextRange(penX, baselineY, text, static_cast<int>(std::strlen(text)), color, font);
	}

	float Gui::addTextRange(float penX, float baselineY, const char* text, int count,
	                        uint32_t color, TextStyle font) {
		if (!mFont || !text) return penX;
		const int index = mFont->indexForSize(font.size);
		const float bold = font.bold ? boldOffset(mFont->pixelHeight(index)) : 0.0f;
		setTexture(nullptr);
		// `count` is a byte count, and a glyph is a codepoint: the walk is over
		// characters even though the caller measured the string in bytes.
		const char* p = text;
		const char* end = text + count;
		while (p < end) {
			uint32_t cp = 0;
			p += Utf8::decode(p, end, cp);
			GlyphQuad q;
			if (mFont->quadFor(cp, penX, baselineY, q, index)) { // advances penX
				addQuad(q.x0, q.y0, q.x1, q.y1, q.u0, q.v0, q.u1, q.v1, color);
				// The second pass is the whole of bold: one glyph, one pixel over.
				if (bold > 0.0f) {
					addQuad(q.x0 + bold, q.y0, q.x1 + bold, q.y1,
					        q.u0, q.v0, q.u1, q.v1, color);
				}
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
	bool Gui::focusable(uint32_t wid, const Rect& rect, Variant ringVariant) {
		// Registering is how a widget joins the Tab order and gets a ring; it says
		// nothing about the pointer. Claiming the press here would mean a widget that
		// merely *has* focus keeps it no matter where the click landed, which is the
		// opposite of what a click on empty space should do.
		mFocusables.push_back(Focusable{ wid, rect, ringVariant });
		return mFocused == wid;
	}

	bool Gui::focusActivated(uint32_t wid) const {
		if (mFocused != wid) return false;
		for (const GuiEditKey key : mInput.editKeys) {
			if (key == GuiEditKey::Enter || key == GuiEditKey::Space) return true;
		}
		return false;
	}

	int Gui::focusStep(uint32_t wid) const {
		if (mFocused != wid) return 0;
		for (const GuiEditKey key : mInput.editKeys) {
			if (key == GuiEditKey::Left || key == GuiEditKey::Up) return -1;
			if (key == GuiEditKey::Right || key == GuiEditKey::Down) return 1;
		}
		return 0;
	}

	bool Gui::focusEscaped(uint32_t wid) const {
		if (mFocused != wid) return false;
		for (const GuiEditKey key : mInput.editKeys) {
			if (key == GuiEditKey::Escape) return true;
		}
		return false;
	}

	void Gui::beginPanel(const char* id, const Rect& rect, Variant variant) {
		const PanelStyle& s = mTheme.panel(variant);
		const uint32_t wid = scopedId(id);
		mScopeStack.push_back(wid);
		pushClip(glm::vec4(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h));
		// A panel has no hover state, so these move only when its variant does -- which
		// is the case they exist for, since a variant may be a binding.
		const uint32_t body = mMotion.colour(wid ^ kMotionFill, s.body,
		                                     s.motion.seconds, s.motion.curve);
		const float radius = mMotion.value(wid ^ kMotionRadius, s.radius,
		                                   s.motion.seconds, s.motion.curve);
		addFrame(rect, body, s.border, s.borderWidth, radius);              // body
		if (s.accentHeight > 0.0f) {
			addRect({ rect.x, rect.y, rect.w, s.accentHeight }, s.accent);  // accent strip
		}
	}

	void Gui::endPanel() {
		popClip();
		mScopeStack.pop_back();
	}

	void Gui::label(const char* text, glm::vec2 pos, uint32_t color, TextStyle font) {
		if (!mFont) return;
		addText(pos.x, pos.y + baseline(font), text, color, font);
	}

	void Gui::labelSpans(const char* text, int begin, int count, glm::vec2 pos,
	                     uint32_t base, const TextSpan* spans, size_t spanCount,
	                     TextStyle font) {
		if (!mFont || !text || count <= 0) return;
		float pen = pos.x;
		const float baseY = pos.y + baseline(font);
		int at = begin;
		const int end = begin + count;

		// Walk the slice, drawing whatever is between here and the next boundary. The
		// spans are sorted and disjoint, so this is one pass over both -- no search per
		// character, and no substring anywhere.
		size_t next = 0;
		while (next < spanCount && spans[next].start + spans[next].length <= at) ++next;

		while (at < end) {
			if (next < spanCount && spans[next].start <= at) {
				// Inside a span: draw to whichever comes first, its end or the slice's.
				const int stop = (std::min)(end, spans[next].start + spans[next].length);
				pen = addTextRange(pen, baseY, text + at, stop - at, spans[next].color, font);
				at = stop;
				if (at >= spans[next].start + spans[next].length) ++next;
				continue;
			}
			// Outside one: draw to where the next begins, or to the end.
			const int stop = (next < spanCount) ? (std::min)(end, spans[next].start) : end;
			pen = addTextRange(pen, baseY, text + at, stop - at, base, font);
			at = stop;
		}
	}

	bool Gui::windowGrip(const char* id, const Rect& rect) {
		const uint32_t wid = scopedId(id);
		if (rect.contains(mInput.pointer)) mHot = wid;
		if (mHot == wid && mInput.pressed) mActive = wid;
		if (mActive == wid && mInput.released) mActive = 0;

		// Held is enough: there is no click to report and nothing to draw. A drag that
		// began here goes on even once the pointer has left the bar, which is what makes
		// a window follow the pointer across the screen rather than stopping at its own
		// edge.
		const bool held = (mActive == wid);
		if (held) {
			mWindowGrabbed = true;
			// The window is about to move under a pointer that has not itself moved, so
			// nothing else would ask for the next frame.
			mAwake = true;
		}
		return held;
	}

	bool Gui::button(const char* id, const char* text, const Rect& rect, Variant variant,
	                 TextAlign align, float alignNudge) {
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
		// Clicking one points the keyboard at it, the way clicking a field does.
		if (mHot == wid && mInput.pressed) setFocus(wid);
		const bool hasFocus = focusable(wid, rect);
		if (hasFocus && focusActivated(wid)) clicked = true;

		uint32_t color = s.normal;
		// Pressed-looking while the key that presses it is what activated it. There is
		// no held state to read for a key, so this is the frame it happens on.
		if (hasFocus && focusActivated(wid)) color = s.pressed;
		if (mActive == wid)      color = s.pressed; // pressed
		else if (mHot == wid)    color = s.hovered; // hovered
		// On its way there rather than already there, when the theme asks for it. The
		// three states above are unchanged: what a button *should* look like is still
		// decided by what is happening to it, and only how it gets there is new.
		color = mMotion.colour(wid ^ kMotionFill, color, s.motion.seconds, s.motion.curve);
		// And the corners, for the same reason and over the same time: a button whose
		// variant changed is a different shape as well as a different colour, and only
		// one of the two used to travel.
		const float radius = mMotion.value(wid ^ kMotionRadius, s.radius,
		                                   s.motion.seconds, s.motion.curve);
		addFrame(rect, color, s.border, s.borderWidth, radius);

		if (mFont) {
			// Inset by the same padding a button measures itself with, so aligned text
			// sits where the edge of a centred button's text would be rather than
			// against the border.
			const float pad = (align == TextAlign::Center) ? 0.0f : kButtonPadX;
			const float room = rect.w - pad * 2.0f;
			const float tx = rect.x + pad + alignOffset(align, room, measureText(text, s.font), alignNudge);
			float baselineY = rect.y + rect.h * 0.5f + baseline(s.font) * 0.35f;
			addText(tx, baselineY, text, s.text, s.font);
		}
		return clicked;
	}
}
