#include <GraphicalObjects/Gui/Canvas.h>
#include <Logger/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// Canvas, the draw-list builder: the geometry every widget is built from -- quads, rects,
// rounded rects, frames, lines, meshes -- and the clip, texture and opacity state that new
// geometry is stamped with. Text is in CanvasText.cpp.
namespace RDA {

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

	// A clip large enough to mean "unclipped"; the backend clamps it to the target.
	static constexpr glm::vec4 kFullClip{ 0.0f, 0.0f, 1.0e6f, 1.0e6f };

	// ---- the frame ----------------------------------------------------------------
	void Canvas::beginFrame(const glm::vec2& viewport) {
		mClipStack.clear();
		// Seeded to the target rather than to "infinite": widgets read the current clip
		// as the area they are laid out inside, so the outermost one has to be a real
		// size or anything anchored at the root would stretch to the fallback's extent.
		mCurrentClip = (viewport.x > 0.0f && viewport.y > 0.0f)
			? glm::vec4(0.0f, 0.0f, viewport.x, viewport.y)
			: kFullClip;
		mCurrentTexture = nullptr;
	}

	void Canvas::beginWalk() {
		mDraw.clear();
		mOpacity = 1.0f;
		mOpacityStack.clear();
		mCmdStart = 0;
	}

	void Canvas::reuse(size_t vertices, size_t indices, size_t commands) {
		mDraw.vertices.resize(vertices);
		mDraw.indices.resize(indices);
		mDraw.commands.resize(commands);
		mCmdStart = static_cast<uint32_t>(indices);
	}

	uint64_t Canvas::fingerprint() const {
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
		return hash;
	}

	// ---- public low-level drawing -------------------------------------------------
	void Canvas::drawRect(const Rect& rect, uint32_t color) {
		addRect(rect, color);
	}

	void Canvas::drawRectRounded(const Rect& rect, uint32_t color, float radius) {
		addRectRounded(rect, color, radius);
	}

	void Canvas::image(const Rect& r, const Texture* texture, uint32_t tint) {
		if (!texture) return;
		setTexture(texture);
		addQuad(r.x, r.y, r.x + r.w, r.y + r.h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
		setTexture(nullptr); // the image is its own command; go back to the atlas
	}
	void Canvas::pushOpacity(float alpha) {
		mOpacityStack.push_back(mOpacity);
		// Multiplied, not replaced: a half-faded thing inside a half-faded thing is a
		// quarter there, which is what nesting has to mean for it to compose.
		mOpacity *= (alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha));
	}

	void Canvas::popOpacity() {
		if (mOpacityStack.empty()) return;
		mOpacity = mOpacityStack.back();
		mOpacityStack.pop_back();
	}

	void Canvas::pushClipRect(const Rect& rect) {
		pushClip(glm::vec4(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h));
	}
	void Canvas::popClipRect() {
		popClip();
	}
	Rect Canvas::currentClipRect() const {
		return { mCurrentClip.x, mCurrentClip.y, mCurrentClip.z - mCurrentClip.x, mCurrentClip.w - mCurrentClip.y };
	}

	// ---- geometry -----------------------------------------------------------------
	void Canvas::addQuad(float x0, float y0, float x1, float y1,
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

	void Canvas::addQuadPoints(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
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

	void Canvas::drawLine(glm::vec2 from, glm::vec2 to, uint32_t color, float width) {
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

	bool Canvas::drawMesh(const WidgetMesh& mesh, const Rect& rect, int from, int into, float t,
	                   const uint32_t* colours, size_t colourCount, uint32_t base) {
		if (!mesh.valid() || rect.w <= 0.0f || rect.h <= 0.0f) return false;

		// Kept between calls rather than allocated per widget: this runs once per
		// mesh-drawn widget per frame, and a vector that grows to the largest mesh and
		// stays there costs one allocation for the life of the program.
		static std::vector<GuiVertex> resolved;
		mesh.resolve(from, into, t, rect, colours, colourCount, base, resolved);
		if (resolved.empty()) return false;

		// The same texture solid geometry always uses -- the atlas' white texel -- so a
		// mesh batches with every rect and glyph around it and costs no extra draw.
		setTexture(nullptr);
		const glm::vec2 white = mFont ? mFont->whiteUV() : glm::vec2(0.0f);

		const size_t first = mDraw.vertices.size();
		if (first + resolved.size() > 0xFFFFu) return false;  // one buffer, 16-bit indices
		for (GuiVertex one : resolved) {
			one.uv = white;
			// The fade every other piece of geometry gets in addQuad. A mesh does not go
			// through it, so a collapsing panel would have faded everything inside
			// itself except the meshes.
			if (mOpacity < 1.0f) one.color = fadeTo(one.color, mOpacity);
			mDraw.vertices.push_back(one);
		}
		for (uint16_t index : mesh.indices()) {
			mDraw.indices.push_back(static_cast<uint16_t>(first + index));
		}
		return true;
	}

	void Canvas::addRect(const Rect& r, uint32_t color) {
		setTexture(nullptr); // solid quads sample the atlas' white texel
		glm::vec2 w = mFont ? mFont->whiteUV() : glm::vec2(0.0f);
		addQuad(r.x, r.y, r.x + r.w, r.y + r.h, w.x, w.y, w.x, w.y, color);
	}

	void Canvas::addRectRounded(const Rect& r, uint32_t color, float radius) {
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

	void Canvas::addFrame(const Rect& r, uint32_t fill, uint32_t border, float borderWidth, float radius) {
		if (borderWidth <= 0.0f) { addRectRounded(r, fill, radius); return; }
		const float bw = (std::min)(borderWidth, (std::min)(r.w, r.h) * 0.5f);
		addRectRounded(r, border, radius);
		addRectRounded({ r.x + bw, r.y + bw, r.w - 2.0f * bw, r.h - 2.0f * bw },
		               fill, (std::max)(0.0f, radius - bw));
	}

	// ---- clipping / draw commands -------------------------------------------------
	void Canvas::flushCmd() {
		uint32_t count = static_cast<uint32_t>(mDraw.indices.size());
		if (count > mCmdStart) {
			mDraw.commands.push_back({ mCurrentClip, mCmdStart, count - mCmdStart, mCurrentTexture });
			mCmdStart = count;
		}
	}

	void Canvas::setTexture(const Texture* texture) {
		if (texture != mCurrentTexture) {
			flushCmd();
			mCurrentTexture = texture;
		}
	}

	void Canvas::pushClip(const glm::vec4& clip) {
		flushCmd();
		mClipStack.push_back(mCurrentClip);
		// Intersect with the parent clip so nested panels stay inside their parents.
		mCurrentClip = glm::vec4(
			(std::max)(mCurrentClip.x, clip.x),
			(std::max)(mCurrentClip.y, clip.y),
			(std::min)(mCurrentClip.z, clip.z),
			(std::min)(mCurrentClip.w, clip.w));
	}

	void Canvas::popClip() {
		flushCmd();
		mCurrentClip = mClipStack.back();
		mClipStack.pop_back();
	}
}
