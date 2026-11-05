#pragma once
#include <cstdint>

namespace RDA {

	// The codepoints the font atlas bakes.
	//
	// Contiguous blocks rather than a set of codepoints, for two reasons. A TrueType
	// packer wants to be asked in ranges, so this is the shape the baking already needs.
	// And it makes "where is this glyph" a walk over seven entries instead of a hash
	// lookup, on a path that runs once per glyph per frame.
	//
	// What is here is Latin: every accented letter used by a Western, Central or Eastern
	// European language written in Latin script, the Romanian comma-below pair that lives
	// outside Latin Extended-A, the punctuation an editor produces on its own -- curly
	// quotes, the dashes, the ellipsis -- and the euro.
	//
	// What is not here is everything else: Greek, Cyrillic, Hebrew, Arabic, any CJK.
	// Those need more than a wider range. They need shaping, bidirectional layout, and an
	// atlas that is not baked once at startup at every size. Drawing them wrongly would
	// be worse than an interface that says plainly it does not draw them -- which is what
	// the replacement box at the end of this table is for.
	struct GlyphBlock {
		uint32_t first;
		uint32_t count;
	};

	// Ascending, and glyphSlot() relies on that.
	inline constexpr GlyphBlock kGlyphBlocks[] = {
		{ 0x0020,  95 }, // Basic Latin, space to '~'
		{ 0x00A0,  96 }, // Latin-1 Supplement: the Western European accents, and NBSP
		{ 0x0100, 128 }, // Latin Extended-A: Polish, Czech, Hungarian, Turkish, Baltic
		{ 0x0218,   4 }, // S and T with comma below -- the correct Romanian letters
		{ 0x2010,  24 }, // General Punctuation: dashes, curly quotes, bullet, ellipsis
		{ 0x20AC,   1 }, // Euro
		{ 0xFFFD,   1 }, // Replacement character, drawn for anything not in this table
	};
	inline constexpr int kGlyphBlockCount =
		static_cast<int>(sizeof(kGlyphBlocks) / sizeof(kGlyphBlocks[0]));

	// Total slots, so nothing has to repeat the arithmetic above to size a table.
	inline constexpr int kGlyphCount = [] {
		int total = 0;
		for (const GlyphBlock& block : kGlyphBlocks) total += static_cast<int>(block.count);
		return total;
	}();

	// The slot `cp` occupies in a baked table, or -1 when the atlas does not hold it.
	inline constexpr int glyphSlot(uint32_t cp) {
		int base = 0;
		for (const GlyphBlock& block : kGlyphBlocks) {
			if (cp >= block.first && cp - block.first < block.count) {
				return base + static_cast<int>(cp - block.first);
			}
			base += static_cast<int>(block.count);
		}
		return -1;
	}

	// Two slots worth naming: what a space is (the fallback advance for a glyph that
	// cannot be drawn at all) and what everything undrawable is drawn as.
	inline constexpr int kSpaceSlot = 0;
	inline constexpr int kReplacementSlot = kGlyphCount - 1;

	// The table is small, ordered and load-bearing, so it checks itself rather than
	// waiting for a wrong glyph on screen to say it was edited carelessly.
	static_assert(glyphSlot(0x0020) == kSpaceSlot,        "space must be the first slot");
	static_assert(glyphSlot(0xFFFD) == kReplacementSlot,  "the replacement box must be last");
	static_assert(glyphSlot(0x007E) == 94,                "Basic Latin must be contiguous from 0");
	static_assert(glyphSlot(0x007F) == -1,                "DEL is not a drawable character");
	static_assert(glyphSlot(0x0219) == glyphSlot(0x0218) + 1, "the Romanian block must be in order");
	static_assert(kGlyphCount == 349,                     "update this when the table changes");
}
