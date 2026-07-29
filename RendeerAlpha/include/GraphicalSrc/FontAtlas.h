#pragma once
#include <Core/Datatypes.h>
#include <GraphicalObjects/Texture.h>
#include <string>
#include <vector>

namespace RDA {

	// One baked glyph, mirroring stbtt_bakedchar so the stb header stays out of this
	// interface (like Texture keeps VMA out of its own).
	struct BakedGlyph {
		unsigned short x0, y0, x1, y1; // glyph box in the atlas, in texels
		float xoff, yoff, xadvance;    // placement relative to the pen + advance
	};

	// The screen quad + atlas UVs for one glyph, produced by quadFor().
	struct GlyphQuad {
		float x0, y0, x1, y1; // pixels
		float u0, v0, u1, v1; // atlas UVs
	};

	// A baked bitmap font atlas: an R8 coverage texture holding ASCII 32..126 plus a
	// reserved white block, so solid UI quads and glyphs sample the same texture and
	// batch into one draw. CPU glyph metrics live here for the GUI frontend to lay out
	// text; the GPU texture is what the GUI backend binds.
	class FontAtlas {
	public:
		bool bake(const std::string& ttfPath, float pixelHeight);
		void destroy() { mTexture.destroy(); mChars.clear(); }

		const Texture& texture()     const { return mTexture; }
		bool           isValid()     const { return mTexture.isValid(); }
		glm::vec2      whiteUV()     const { return mWhiteUV; }
		float          pixelHeight() const { return mPixelHeight; }
		float          ascent()      const { return mAscent; }      // top -> baseline, px
		float          lineAdvance() const { return mLineAdvance; } // baseline -> baseline

		// Fills `out` for `c` at pen (penX, baselineY) and advances penX. Returns false
		// for a character outside the baked range (penX still advances by a space).
		bool  quadFor(char c, float& penX, float baselineY, GlyphQuad& out) const;

		// Advance width of `text` in pixels.
		float textWidth(const std::string& text) const;
		// Advance width of a single glyph in pixels (space for unknown glyphs).
		float advance(char c) const;

	private:
		Texture                 mTexture;
		std::vector<BakedGlyph> mChars;      // index = codepoint - 32
		float                   mAtlasW = 0.0f;
		float                   mAtlasH = 0.0f;
		glm::vec2               mWhiteUV{ 0.0f };
		float                   mAscent = 0.0f;
		float                   mLineAdvance = 0.0f;
		float                   mPixelHeight = 0.0f;
	};
}
