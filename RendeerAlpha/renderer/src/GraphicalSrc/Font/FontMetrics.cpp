#include <GraphicalSrc/FontAtlas.h>
#include <GraphicalSrc/GlyphRanges.h>
#include <Core/Utf8.h>
#include <Logger/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace RDA {

	static constexpr size_t kMaxReportedMissing = 8;

	// Two sizes closer than this are one size. It is far below what could show, and it is
	// here so that a number arrived at by arithmetic does not become a size of its own.
	static constexpr float kSameSize = 0.01f;

	const FontAtlas::Size& FontAtlas::at(int index) const {
		static const Size empty;
		if (mSizes.empty()) return empty;
		if (index < 0 || index >= static_cast<int>(mSizes.size())) return mSizes[0];
		return mSizes[index];
	}

	int FontAtlas::missingCodepoints() const {
		int missing = 0;
		for (uint8_t present : mPresent) if (!present) ++missing;
		return missing;
	}

	int FontAtlas::indexForSize(float px) const {
		// Written this way round so that NaN, which a <viewport> can send, is turned away too:
		// every comparison with it is false, and it would otherwise become a size.
		if (!(px > 0.0f) || mSizes.empty()) return 0;
		int best = 0;
		float bestDelta = std::fabs(mSizes[0].pixelHeight - px);
		for (int i = 1; i < static_cast<int>(mSizes.size()); ++i) {
			const float delta = std::fabs(mSizes[i].pixelHeight - px);
			if (delta < bestDelta) { best = i; bestDelta = delta; }
		}
		if (bestDelta < kSameSize) return best;
		if (std::find(mUnbaked.begin(), mUnbaked.end(), px) != mUnbaked.end()) return best;

		const int added = addSize(px);
		if (added >= 0) return added;

		// Nothing is scaled, so a size the atlas will not take is drawn at the nearest one it
		// holds. That is a decision the author did not make, and it is worth one line in the
		// log naming both numbers -- once per size, not once per frame.
		if (mUnbaked.size() < kMaxReportedMissing) {
			mUnbaked.push_back(px);
			RDA_LOG_WARNING("The font atlas cannot take " << px << "px text, so it is drawn at "
			                << mSizes[best].pixelHeight << "px. It holds sizes up to "
			                << kMaxAddedHeight << " pixels tall, and " << kMaxAddedSizes
			                << " beyond the ones baked at startup.");
		}
		return best;
	}

	// Said once per codepoint, and only for the first few. Text that cannot be drawn is
	// the sort of thing that is either one stray character or an entire document in a
	// script this does not support, and the second must not fill the log.
	void FontAtlas::noteMissing(uint32_t cp) const {
		// Bounded, and that matters more than the message. This runs once per glyph per
		// frame, so an unbounded list of what could not be drawn would turn a document in
		// an unsupported script into a linear scan over thousands of entries, every
		// frame, for as long as it is on screen. After a handful the point has been made.
		if (mMissing.size() > kMaxReportedMissing) return;
		if (std::find(mMissing.begin(), mMissing.end(), cp) != mMissing.end()) return;
		mMissing.push_back(cp);
		if (mMissing.size() > kMaxReportedMissing) {
			RDA_LOG_WARNING("Further undrawable codepoints will not be reported.");
			return;
		}

		// Since anything outside GlyphRanges.h is baked on demand from whichever face has
		// it, getting here at all means no face did -- which is a fact about the fonts
		// this application named, and fixable by naming another. The one exception is a
		// codepoint inside the preloaded table, where the body font is the only one asked.
		const bool inTable = glyphSlot(cp) >= 0;
		char code[16];
		std::snprintf(code, sizeof(code), "U+%04X", cp);
		if (inTable) {
			RDA_LOG_WARNING(code << " is drawn as a box: the body font does not have it.");
		} else {
			RDA_LOG_WARNING(code << " is drawn as a box: none of the fonts given has a"
			                        " glyph for it. Add a face that does, after a ';' in"
			                        " the font path.");
		}
	}

	const BakedGlyph* FontAtlas::glyphFor(uint32_t cp, int index) const {
		const Size& size = at(index);
		if (size.glyphs.empty()) return nullptr;

		// The preloaded table: an array index, and what almost every character hits.
		const int slot = glyphSlot(cp);
		if (size.preloaded && slot >= 0 && static_cast<size_t>(slot) < mPresent.size()
		    && mPresent[static_cast<size_t>(slot)]) {
			return &size.glyphs[static_cast<size_t>(slot)];
		}

		// Then whatever has already been baked, and then baking it.
		const auto found = size.onDemand.find(cp);
		if (found != size.onDemand.end()) return &found->second;
		if (bakeOnDemand(cp, index)) {
			const auto made = size.onDemand.find(cp);
			if (made != size.onDemand.end()) return &made->second;
		}

		noteMissing(cp);
		if (static_cast<size_t>(kReplacementSlot) < mPresent.size()
		    && mPresent[static_cast<size_t>(kReplacementSlot)]) {
			return &size.glyphs[static_cast<size_t>(kReplacementSlot)];
		}
		return nullptr;
	}

	bool FontAtlas::quadFor(uint32_t cp, float& penX, float baselineY, GlyphQuad& out, int index) const {
		const Size& size = at(index);
		if (size.glyphs.empty()) return false;

		const BakedGlyph* glyph = glyphFor(cp, index);
		if (!glyph) {
			// Nothing to draw and nothing to draw it with: advance by a space so the rest
			// of the line keeps its position.
			penX += size.glyphs[kSpaceSlot].xadvance;
			return false;
		}
		const BakedGlyph& b = *glyph;
		if (b.x1 <= b.x0 || b.y1 <= b.y0) {
			// An empty box -- a space, or a mark the font draws as nothing. It still
			// advances; it just has no quad, which saves one per space in every label.
			penX += b.xadvance;
			return false;
		}
		// Snapped to a whole *physical* pixel and then expressed in layout units again. A
		// glyph whose bitmap lands half a pixel off the grid is sampled between two texels
		// and comes out soft, which at 150% would undo the point of baking it at 150%.
		float rx = std::floor((penX + b.xoff) * mScale + 0.5f) / mScale;
		float ry = std::floor((baselineY + b.yoff) * mScale + 0.5f) / mScale;
		out.x0 = rx;
		out.y0 = ry;
		out.x1 = rx + (b.x1 - b.x0) / mScale;
		out.y1 = ry + (b.y1 - b.y0) / mScale;
		out.u0 = b.x0 / mAtlasW;
		out.v0 = b.y0 / mAtlasH;
		out.u1 = b.x1 / mAtlasW;
		out.v1 = b.y1 / mAtlasH;
		penX += b.xadvance;
		return true;
	}

	float FontAtlas::advance(uint32_t cp, int index) const {
		const Size& size = at(index);
		if (size.glyphs.empty()) return 0.0f;
		const BakedGlyph* glyph = glyphFor(cp, index);
		return glyph ? glyph->xadvance : size.glyphs[kSpaceSlot].xadvance;
	}

	float FontAtlas::textWidth(const std::string& text, int index) const {
		return textWidth(text.data(), text.data() + text.size(), index);
	}

	float FontAtlas::textWidth(const char* begin, const char* end, int index) const {
		const Size& size = at(index);
		if (size.glyphs.empty() || !begin || !end || end <= begin) return 0.0f;
		float width = 0.0f;
		while (begin < end) {
			uint32_t cp = 0;
			begin += Utf8::decode(begin, end, cp);
			const BakedGlyph* glyph = glyphFor(cp, index);
			width += glyph ? glyph->xadvance : size.glyphs[kSpaceSlot].xadvance;
		}
		return width;
	}
}
