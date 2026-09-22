#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalObjects/Texture.h>
#include <GraphicalSrc/GlyphRanges.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace RDA {

	// One baked glyph, mirroring stbtt_packedchar so the stb header stays out of this
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

	// A baked bitmap font atlas: an R8 coverage texture holding every codepoint in
	// GlyphRanges.h at each of several sizes, plus a reserved white block, so solid UI
	// quads and text of every size sample one texture and batch into one draw. CPU glyph
	// metrics live here for the GUI frontend to lay out text; the GPU texture is what the
	// GUI backend binds.
	//
	// Several sizes rather than one that is scaled: a bitmap glyph drawn at a size it was
	// not baked at is a blurry glyph, and text is most of what a GUI draws. The cost is
	// atlas area, which is cheap, rather than a draw call per size, which is not.
	//
	// Text in and out of here is UTF-8 and lookups are by codepoint. This used to be one
	// byte per glyph over ASCII 32..126, which drew 'é' as two blank spaces -- correctly
	// laid out, silently wrong, and impossible to notice in an English test.
	class FontAtlas {
	public:
		FontAtlas();
		~FontAtlas();
		FontAtlas(const FontAtlas&) = delete;
		FontAtlas& operator=(const FontAtlas&) = delete;

		// Bakes every size given. The first is the base -- what everything that expresses
		// no opinion is drawn at -- so it is also what ascent() and lineAdvance() report
		// when nobody says otherwise.
		//
		// `ttfPath` may name several faces separated by ';'. The first is the body font
		// and decides the line metrics; the rest are asked, in order, only for a codepoint
		// the ones before them do not have. That is what lets a monospace interface show
		// an emoji without the emoji font taking over the alphabet.
		bool bake(const std::string& ttfPath, const std::vector<float>& pixelHeights);
		bool bake(const std::string& ttfPath, float pixelHeight) {
			return bake(ttfPath, std::vector<float>{ pixelHeight });
		}
		void destroy();

		// Bumped whenever a glyph is baked on demand. The GUI keeps last frame's geometry
		// when nothing it can see has changed, and an atlas that has just learned a
		// character is invisible to every one of its other checks -- the widget tree is
		// the same and so is the input, and the glyph would appear the next time
		// something else happened to force a walk.
		uint64_t revision() const { return mRevision; }

		const Texture& texture()     const { return mTexture; }

		// Sends whatever was baked on demand since the last call, as one rectangle.
		//
		// Asked for by the renderer once a frame rather than done when a glyph is baked:
		// baking happens while the interface is being walked, a frame that met twenty new
		// characters would otherwise be twenty stalls, and the descriptor set is written
		// once at start-up -- so nothing else would ever ask for the texture again.
		void uploadPending() const;
		bool           isValid()     const { return mTexture.isValid(); }
		glm::vec2      whiteUV()     const { return mWhiteUV; }
		float          atlasWidth()  const { return mAtlasW; }
		float          atlasHeight() const { return mAtlasH; }

		// How many of the codepoints in GlyphRanges.h the font actually draws. A font
		// that is missing some is not an error -- it is a fact about that font, and one
		// worth being able to ask about rather than only reading in the log.
		int  bakedCodepoints()   const { return kGlyphCount; }
		int  missingCodepoints() const;

		// The baked size to draw `px` at. Zero, or anything the atlas does not hold,
		// resolves to the nearest one it does -- nothing is ever scaled, so a size that
		// was not asked for at startup is drawn slightly wrong rather than blurrily.
		int indexForSize(float px) const;

		float pixelHeight(int index = 0) const { return at(index).pixelHeight; }
		float ascent(int index = 0)      const { return at(index).ascent; }      // top -> baseline
		float lineAdvance(int index = 0) const { return at(index).lineAdvance; } // baseline -> baseline

		// The baked table, indexed by glyphSlot(codepoint).
		const std::vector<BakedGlyph>& glyphs(int index = 0) const { return at(index).glyphs; }

		// Fills `out` for codepoint `cp` at pen (penX, baselineY) and advances penX.
		// Returns false when there is no quad to draw -- a space, or something the atlas
		// does not hold -- in which case penX has still advanced, so a line containing
		// one keeps its shape instead of collapsing.
		bool  quadFor(uint32_t cp, float& penX, float baselineY, GlyphQuad& out, int index = 0) const;

		// Advance width of one codepoint, in pixels.
		float advance(uint32_t cp, int index = 0) const;

		// Advance width of UTF-8 text, in pixels. The range form is what a text field
		// measures a caret column with, so a column stays a byte offset while the width
		// it maps to is counted in characters.
		float textWidth(const std::string& text, int index = 0) const;
		float textWidth(const char* begin, const char* end, int index = 0) const;

		// A `char` is a byte, and a byte is not a codepoint. Deleting these turns every
		// call site that still walks text one byte at a time into a compile error rather
		// than a string that draws blanks -- which is how the ASCII-only version of this
		// class went unnoticed for as long as it did.
		bool  quadFor(char, float&, float, GlyphQuad&, int) const = delete;
		float advance(char, int) const = delete;

	private:
		// One baked size. Everything that varies with size lives here; everything that
		// does not -- the texture, the white block, which codepoints the font has -- is
		// shared.
		struct Size {
			float pixelHeight = 0.0f;
			float ascent = 0.0f;
			float lineAdvance = 0.0f;
			std::vector<BakedGlyph> glyphs; // index = glyphSlot(codepoint)
			// Everything outside GlyphRanges.h that has actually been asked for, baked
			// when it was. Kept per size, because a glyph is a picture at one size.
			mutable std::unordered_map<uint32_t, BakedGlyph> onDemand;
		};
		const Size& at(int index) const;

		// The glyph to draw `cp` with at this size, or nullptr when there is nothing at
		// all to draw it with.
		//
		// Three answers in order: the preloaded table, which covers Latin and costs an
		// array index; what has already been baked on demand; and baking it now. The last
		// is why this is the only lookup -- a glyph has to exist before it can be
		// measured, and measuring happens during layout, before anything is drawn.
		const BakedGlyph* glyphFor(uint32_t cp, int index) const;

		// Bakes one codepoint at one size into the atlas' spare room, from the first face
		// that has it. False when no face does, or when the room has run out.
		bool bakeOnDemand(uint32_t cp, int index) const;

		void noteMissing(uint32_t cp) const;

		// Sizes asked for that were not baked, so each is complained about once rather
		// than every frame it is drawn. A theme naming a size nobody baked is a real
		// mistake -- it silently draws at the wrong one -- and silence would hide it.
		mutable std::vector<float> mUnbaked;

		// Codepoints asked for that this atlas cannot draw, on the same terms and for the
		// same reason. Capped, because a document in a script this does not support would
		// otherwise write a line per character in it.
		mutable std::vector<uint32_t> mMissing;

		std::vector<uint8_t> mPresent; // per slot: does the font actually have this glyph
		mutable Texture      mTexture;
		std::vector<Size>    mSizes;   // [0] is the base size
		float                mAtlasW = 0.0f;
		float                mAtlasH = 0.0f;
		glm::vec2            mWhiteUV{ 0.0f };

		// The faces, kept alive after the bake because on-demand needs them: a glyph that
		// arrives in the third hour is rasterised from the same font the first one was.
		// Behind a pointer so stb_truetype stays out of this header.
		struct Faces;
		std::unique_ptr<Faces> mFaces;

		// The atlas as the CPU knows it. A glyph is drawn into here and the rectangle it
		// touched is sent; without a copy there would be nothing to draw it into.
		mutable std::vector<uint8_t> mPixels;

		// A shelf allocator over the rows below the baked bands. Glyphs are added in the
		// order they are met, which is neither sorted by size nor knowable in advance, so
		// a shelf -- fill a row, start the next one below the tallest thing in it -- is
		// the right amount of cleverness.
		mutable int mShelfX = 0;       // next free column on the current shelf
		mutable int mShelfY = 0;       // top row of the current shelf
		mutable int mShelfHeight = 0;  // tallest glyph on it so far
		int         mDynamicTop = 0;   // first row the preloaded bands do not own
		mutable bool mRoomWarned = false;

		// The rectangle baked since the last flush, in atlas rows. Columns are not
		// tracked: a shelf is as wide as the atlas soon enough, and a row is 1 KB.
		mutable int mDirtyTop = 0;
		mutable int mDirtyBottom = 0;   // exclusive; equal means nothing pending

		mutable uint64_t mRevision = 1;
	};
}
