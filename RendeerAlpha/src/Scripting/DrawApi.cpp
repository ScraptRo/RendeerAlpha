#include <Scripting/DrawApi.h>
#include <algorithm>

namespace RDA::Script {

	uint32_t DrawApi::packColor(uint32_t rgba) {
		// 0xRRGGBBAA as a script writes it, to R in the low byte as the vertex wants.
		const uint32_t r = (rgba >> 24) & 0xFFu;
		const uint32_t g = (rgba >> 16) & 0xFFu;
		const uint32_t b = (rgba >> 8) & 0xFFu;
		const uint32_t a = rgba & 0xFFu;
		return r | (g << 8) | (b << 16) | (a << 24);
	}

	void DrawApi::begin() {
		mDraw.clear();
		mCmdStart = 0;
		mTexture = 0;
		mClipStack.clear();
		mClip = { 0.0f, 0.0f, mWidth, mHeight };
	}

	void DrawApi::flush() {
		const uint32_t count = static_cast<uint32_t>(mDraw.indices.size());
		if (count <= mCmdStart) return; // nothing drawn since the last one

		GuiDrawCmd cmd{};
		cmd.clip = mClip;
		cmd.indexOffset = mCmdStart;
		cmd.indexCount = count - mCmdStart;
		// A texture id, not a pointer: the runtime resolves it against this client's own
		// table. 0 means the atlas, which is what solid quads and text sample.
		cmd.texture = reinterpret_cast<const Texture*>(static_cast<uintptr_t>(mTexture));
		mDraw.commands.push_back(cmd);
		mCmdStart = count;
	}

	void DrawApi::end() {
		flush();
		// Cheap and order-sensitive: enough to tell one frame's geometry from another so
		// an unchanged frame is not resent.
		uint64_t hash = 1469598103934665603ull;
		auto feed = [&hash](const void* data, size_t bytes) {
			const uint8_t* at = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < bytes; ++i) { hash ^= at[i]; hash *= 1099511628211ull; }
		};
		feed(mDraw.vertices.data(), mDraw.vertices.size() * sizeof(GuiVertex));
		feed(mDraw.indices.data(), mDraw.indices.size() * sizeof(uint16_t));
		for (const GuiDrawCmd& cmd : mDraw.commands) {
			feed(&cmd.clip, sizeof(cmd.clip));
			feed(&cmd.texture, sizeof(cmd.texture));
		}
		mVersion = hash;
	}

	void DrawApi::setTexture(uint32_t textureId) {
		if (textureId == mTexture) return;
		flush(); // a command is one texture
		mTexture = textureId;
	}

	void DrawApi::pushClip(float x, float y, float w, float h) {
		flush(); // a command is one clip
		mClipStack.push_back(mClip);
		// Intersected with what is already in force: a script cannot widen the area its
		// caller restricted it to, only narrow it.
		const glm::vec4 wanted{ x, y, x + w, y + h };
		mClip = { (std::max)(mClip.x, wanted.x), (std::max)(mClip.y, wanted.y),
		          (std::min)(mClip.z, wanted.z), (std::min)(mClip.w, wanted.w) };
	}

	void DrawApi::popClip() {
		if (mClipStack.empty()) return;
		flush();
		mClip = mClipStack.back();
		mClipStack.pop_back();
	}

	void DrawApi::rect(float x, float y, float w, float h, uint32_t color) {
		if (w <= 0.0f || h <= 0.0f) return;
		setTexture(0);

		// Solid fills sample the atlas's white texel, so they batch with text.
		const glm::vec2 white = mFont
			? glm::vec2{ mFont->header.whiteU, mFont->header.whiteV }
			: glm::vec2{ 0.0f, 0.0f };
		const uint32_t packed = packColor(color);
		const uint16_t base = static_cast<uint16_t>(mDraw.vertices.size());

		mDraw.vertices.push_back(GuiVertex{ { x,     y     }, white, packed });
		mDraw.vertices.push_back(GuiVertex{ { x + w, y     }, white, packed });
		mDraw.vertices.push_back(GuiVertex{ { x + w, y + h }, white, packed });
		mDraw.vertices.push_back(GuiVertex{ { x,     y + h }, white, packed });
		const uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
		for (uint16_t o : order) mDraw.indices.push_back(static_cast<uint16_t>(base + o));
	}

	float DrawApi::text(const std::string& value, float x, float y, uint32_t color) {
		if (!mFont || !mFont->valid()) return 0.0f;
		setTexture(0); // glyphs come from the atlas

		const uint32_t packed = packColor(color);
		const float atlasW = mFont->header.atlasWidth;
		const float atlasH = mFont->header.atlasHeight;
		if (atlasW <= 0.0f || atlasH <= 0.0f) return 0.0f;

		// `y` is the top of the line, which is what a caller means; the glyph offsets are
		// relative to the baseline, so the ascent moves the pen down to it.
		const float baseline = y + mFont->header.ascent;
		float pen = x;

		for (char c : value) {
			const uint32_t code = static_cast<unsigned char>(c);
			if (code < mFont->header.firstCodepoint) { pen += mFont->advance(c); continue; }
			const uint32_t index = code - mFont->header.firstCodepoint;
			if (index >= mFont->glyphs.size()) { pen += mFont->advance(c); continue; }

			const Runtime::WireGlyph& g = mFont->glyphs[index];
			const float x0 = pen + g.xoff;
			const float y0 = baseline + g.yoff;
			const float x1 = x0 + static_cast<float>(g.x1 - g.x0);
			const float y1 = y0 + static_cast<float>(g.y1 - g.y0);

			// A space has no box; advancing is all it does.
			if (g.x1 > g.x0 && g.y1 > g.y0) {
				const float u0 = static_cast<float>(g.x0) / atlasW;
				const float v0 = static_cast<float>(g.y0) / atlasH;
				const float u1 = static_cast<float>(g.x1) / atlasW;
				const float v1 = static_cast<float>(g.y1) / atlasH;

				const uint16_t base = static_cast<uint16_t>(mDraw.vertices.size());
				mDraw.vertices.push_back(GuiVertex{ { x0, y0 }, { u0, v0 }, packed });
				mDraw.vertices.push_back(GuiVertex{ { x1, y0 }, { u1, v0 }, packed });
				mDraw.vertices.push_back(GuiVertex{ { x1, y1 }, { u1, v1 }, packed });
				mDraw.vertices.push_back(GuiVertex{ { x0, y1 }, { u0, v1 }, packed });
				const uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
				for (uint16_t o : order) mDraw.indices.push_back(static_cast<uint16_t>(base + o));
			}
			pen += g.xadvance;
		}
		return pen - x;
	}

	void DrawApi::image(float x, float y, float w, float h, uint32_t textureId, uint32_t tint) {
		if (w <= 0.0f || h <= 0.0f || textureId == 0) return;
		setTexture(textureId);

		const uint32_t packed = packColor(tint);
		const uint16_t base = static_cast<uint16_t>(mDraw.vertices.size());
		mDraw.vertices.push_back(GuiVertex{ { x,     y     }, { 0.0f, 0.0f }, packed });
		mDraw.vertices.push_back(GuiVertex{ { x + w, y     }, { 1.0f, 0.0f }, packed });
		mDraw.vertices.push_back(GuiVertex{ { x + w, y + h }, { 1.0f, 1.0f }, packed });
		mDraw.vertices.push_back(GuiVertex{ { x,     y + h }, { 0.0f, 1.0f }, packed });
		const uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
		for (uint16_t o : order) mDraw.indices.push_back(static_cast<uint16_t>(base + o));
	}

	float DrawApi::measure(const std::string& value) const {
		return mFont ? mFont->textWidth(value.c_str()) : 0.0f;
	}

	float DrawApi::lineHeight() const {
		return mFont ? mFont->lineHeight() : 0.0f;
	}

	bool DrawApi::hit(float x, float y, float w, float h) const {
		const glm::vec2 p = mInput.pointer;
		// Half-open, so adjacent rectangles cannot both claim the same pixel.
		return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
	}
}
