#include <GraphicalSrc/FontAtlas.h>
#include <Core/Utf8.h>
#include <Logger/Logger.h>
#include <vendor/stb_truetype/stb_truetype.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace RDA {

	static constexpr int kAtlasWidth = 1024;
	static constexpr int kMaxAtlasHeight = 4096;
	static constexpr size_t kMaxReportedMissing = 8;

	namespace {
		// Packing asks for a bitmap and does not report how much of it was used, so how
		// tall a size needs to be is only known once its glyphs have been placed. Each
		// size is packed into a scratch buffer of its own and the bands are stacked
		// afterwards, which is also what keeps every glyph of one size in one band.
		struct Band {
			std::vector<unsigned char>    pixels; // kAtlasWidth * used
			int                           used = 0;
			std::vector<stbtt_packedchar> chars;  // indexed by glyphSlot()
		};
	}

	bool FontAtlas::bake(const std::string& ttfPath, const std::vector<float>& pixelHeights) {
		if (pixelHeights.empty()) {
			RDA_LOG_ERROR("Font atlas asked for no sizes: " << ttfPath);
			return false;
		}
		std::ifstream file(ttfPath, std::ios::binary | std::ios::ate);
		if (!file) {
			RDA_LOG_ERROR("Font file not found: " << ttfPath);
			return false;
		}
		std::streamsize size = file.tellg();
		file.seekg(0);
		std::vector<unsigned char> ttf(static_cast<size_t>(size));
		if (!file.read(reinterpret_cast<char*>(ttf.data()), size)) {
			RDA_LOG_ERROR("Failed to read font file: " << ttfPath);
			return false;
		}

		stbtt_fontinfo font{};
		if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
			RDA_LOG_ERROR("Not a font this can read: " << ttfPath);
			return false;
		}

		// ---- which of the wanted codepoints this font actually draws -----------------
		//
		// Asked before baking, because the packer will happily bake .notdef for a
		// codepoint the font does not have, and .notdef is blank in plenty of fonts. A
		// blank where a letter should be is exactly the failure this whole table exists
		// to end, so a glyph the font does not have is recorded as absent and drawn as
		// the replacement box instead.
		mPresent.assign(static_cast<size_t>(kGlyphCount), 0);
		mMissing.clear();
		int missing = 0;
		{
			int slot = 0;
			for (const GlyphBlock& block : kGlyphBlocks) {
				for (uint32_t k = 0; k < block.count; ++k, ++slot) {
					const int cp = static_cast<int>(block.first + k);
					const bool has = stbtt_FindGlyphIndex(&font, cp) != 0;
					mPresent[static_cast<size_t>(slot)] = has ? 1u : 0u;
					if (!has) ++missing;
				}
			}
			// The replacement box is the engine's, not the font's, and it is drawn into
			// every band below. It is present whatever the font does.
			if (!mPresent[static_cast<size_t>(kReplacementSlot)]) --missing;
			mPresent[static_cast<size_t>(kReplacementSlot)] = 1u;
		}

		// ---- pack each size on its own, then stack the bands -------------------------
		std::vector<Band> bands(pixelHeights.size());
		std::vector<unsigned char> scratch(static_cast<size_t>(kAtlasWidth) * kMaxAtlasHeight);
		int totalRows = 0;
		for (size_t i = 0; i < pixelHeights.size(); ++i) {
			std::fill(scratch.begin(), scratch.end(), static_cast<unsigned char>(0));
			bands[i].chars.assign(static_cast<size_t>(kGlyphCount), stbtt_packedchar{});

			// One range per block, each writing into its own slice of the slot table, so
			// what comes out is already indexed by glyphSlot().
			std::vector<stbtt_pack_range> ranges(static_cast<size_t>(kGlyphBlockCount));
			int slot = 0;
			for (int b = 0; b < kGlyphBlockCount; ++b) {
				ranges[b] = stbtt_pack_range{};
				ranges[b].font_size = pixelHeights[i];
				ranges[b].first_unicode_codepoint_in_range = static_cast<int>(kGlyphBlocks[b].first);
				ranges[b].array_of_unicode_codepoints = nullptr;
				ranges[b].num_chars = static_cast<int>(kGlyphBlocks[b].count);
				ranges[b].chardata_for_range = bands[i].chars.data() + slot;
				slot += static_cast<int>(kGlyphBlocks[b].count);
			}

			stbtt_pack_context pack{};
			if (!stbtt_PackBegin(&pack, scratch.data(), kAtlasWidth, kMaxAtlasHeight, 0, 1, nullptr)) {
				RDA_LOG_ERROR("Font atlas could not begin packing " << ttfPath
				              << " at " << pixelHeights[i] << "px");
				return false;
			}
			const int packed = stbtt_PackFontRanges(&pack, ttf.data(), 0,
			                                        ranges.data(), kGlyphBlockCount);
			stbtt_PackEnd(&pack);
			if (!packed) {
				RDA_LOG_ERROR("Font atlas too small to bake " << ttfPath
				              << " at " << pixelHeights[i] << "px");
				return false;
			}

			// The bottom of the lowest glyph placed is how tall this band has to be.
			int used = 0;
			for (const stbtt_packedchar& c : bands[i].chars) {
				used = (std::max)(used, static_cast<int>(c.y1));
			}
			used += 1; // the row the lowest box's bottom edge sits on
			if (used <= 1 || used > kMaxAtlasHeight) {
				RDA_LOG_ERROR("Font atlas packed nothing for " << ttfPath
				              << " at " << pixelHeights[i] << "px");
				return false;
			}

			// The replacement box is drawn here rather than taken from the font.
			//
			// It has to exist, because it is what everything undrawable falls back to,
			// and a fallback that is itself missing puts the text straight back to blank
			// space -- which is the entire failure this class was rewritten to end. It
			// cannot be taken from the font, because plenty of fonts have no U+FFFD:
			// Cascadia Mono, the one that ships here, is one of them. So it is a hollow
			// rectangle stroked into the band below the glyphs, at every size, always.
			const int stroke = (std::max)(1, static_cast<int>(std::lround(pixelHeights[i] / 16.0f)));
			const int boxW = (std::max)(4, static_cast<int>(std::lround(pixelHeights[i] * 0.5f)));
			const int boxH = (std::max)(6, static_cast<int>(std::lround(pixelHeights[i] * 0.66f)));
			if (used + boxH + 1 > kMaxAtlasHeight) {
				RDA_LOG_ERROR("Font atlas has no room for a replacement box at "
				              << pixelHeights[i] << "px");
				return false;
			}
			const int boxY = used;
			for (int y = 0; y < boxH; ++y) {
				for (int x = 0; x < boxW; ++x) {
					const bool edge = x < stroke || x >= boxW - stroke
					               || y < stroke || y >= boxH - stroke;
					if (edge) scratch[static_cast<size_t>(boxY + y) * kAtlasWidth + x] = 255;
				}
			}
			used += boxH + 1;

			stbtt_packedchar& box = bands[i].chars[static_cast<size_t>(kReplacementSlot)];
			box.x0 = 0;
			box.y0 = static_cast<unsigned short>(boxY);
			box.x1 = static_cast<unsigned short>(boxW);
			box.y1 = static_cast<unsigned short>(boxY + boxH);
			box.xoff = 1.0f;
			box.yoff = -static_cast<float>(boxH); // its foot sits on the baseline
			box.xadvance = static_cast<float>(boxW) + 2.0f;

			bands[i].used = used;
			bands[i].pixels.assign(scratch.begin(),
			                       scratch.begin() + static_cast<size_t>(kAtlasWidth) * used);
			totalRows += used;
		}

		// Four rows for the white block, and two spare so bilinear sampling at the edge of
		// the last glyph cannot reach into it.
		int atlasHeight = 1;
		while (atlasHeight < totalRows + 6) atlasHeight *= 2;
		if (atlasHeight > kMaxAtlasHeight) {
			RDA_LOG_ERROR("Font atlas would need " << atlasHeight << " rows for "
			              << pixelHeights.size() << " sizes; ask for fewer, or smaller ones");
			return false;
		}

		std::vector<unsigned char> bitmap(static_cast<size_t>(kAtlasWidth) * atlasHeight, 0);
		mSizes.clear();
		mSizes.reserve(pixelHeights.size());

		int asc = 0, desc = 0, gap = 0;
		stbtt_GetFontVMetrics(&font, &asc, &desc, &gap);

		int atlasY = 0;
		for (size_t i = 0; i < bands.size(); ++i) {
			std::copy(bands[i].pixels.begin(), bands[i].pixels.end(),
			          bitmap.begin() + static_cast<size_t>(kAtlasWidth) * atlasY);

			const float scale = stbtt_ScaleForPixelHeight(&font, pixelHeights[i]);
			Size baked;
			baked.pixelHeight = pixelHeights[i];
			baked.ascent = asc * scale;
			baked.lineAdvance = (asc - desc + gap) * scale;
			baked.glyphs.resize(static_cast<size_t>(kGlyphCount));
			for (int c = 0; c < kGlyphCount; ++c) {
				const stbtt_packedchar& b = bands[i].chars[static_cast<size_t>(c)];
				// Only the vertical box moves: the band was blitted down the atlas, and
				// everything else about a glyph is relative to the pen.
				baked.glyphs[static_cast<size_t>(c)] = {
					b.x0, static_cast<unsigned short>(b.y0 + atlasY),
					b.x1, static_cast<unsigned short>(b.y1 + atlasY),
					b.xoff, b.yoff, b.xadvance,
				};
			}
			mSizes.push_back(std::move(baked));
			atlasY += bands[i].used;
		}

		// Reserve a small solid-white block below the glyphs, so solid rects can sample
		// the same atlas as text and everything batches into one draw.
		int wy = atlasY + 1;
		if (wy + 4 >= atlasHeight) wy = atlasHeight - 5;
		for (int j = 0; j < 4; ++j) {
			for (int i = 0; i < 4; ++i) {
				bitmap[static_cast<size_t>(wy + j) * kAtlasWidth + i] = 255;
			}
		}
		mWhiteUV = glm::vec2(2.0f / kAtlasWidth, (wy + 2.0f) / atlasHeight);
		mAtlasW = static_cast<float>(kAtlasWidth);
		mAtlasH = static_cast<float>(atlasHeight);

		TextureDesc imageDesc{};
		imageDesc.width = kAtlasWidth;
		imageDesc.height = atlasHeight;
		imageDesc.format = VK_FORMAT_R8_UNORM;
		imageDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		imageDesc.withSampler = true;
		if (!mTexture.create(imageDesc)) {
			RDA_LOG_ERROR("Failed to create font atlas texture");
			return false;
		}
		if (!mTexture.uploadPixels(bitmap.data(),
		                           static_cast<VkDeviceSize>(kAtlasWidth) * atlasHeight)) {
			RDA_LOG_ERROR("Failed to upload font atlas");
			return false;
		}

		std::string sizeList;
		for (size_t i = 0; i < pixelHeights.size(); ++i) {
			if (i) sizeList += ", ";
			sizeList += std::to_string(static_cast<int>(std::lround(pixelHeights[i])));
		}
		RDA_LOG_SUCCES("Font atlas baked: " << ttfPath << " at " << sizeList
		               << "px (" << kAtlasWidth << "x" << atlasHeight << "), "
		               << (kGlyphCount - missing) << " of " << kGlyphCount << " codepoints");
		// Which ones, by name, rather than only how many. A font not covering the whole
		// range is a fact about that font and not a fault: it is worth stating once at
		// startup and worth stating precisely, but it is not a warning, because nothing
		// has gone wrong until something actually asks to draw one of them. That is what
		// noteMissing() is for, and it fires per codepoint, on demand.
		if (missing > 0) {
			std::string list;
			int shown = 0;
			int slot = 0;
			for (const GlyphBlock& block : kGlyphBlocks) {
				for (uint32_t k = 0; k < block.count; ++k, ++slot) {
					if (mPresent[static_cast<size_t>(slot)]) continue;
					if (shown == 12) { list += ", ..."; }
					if (shown >= 12) { ++shown; continue; }
					if (shown) list += ", ";
					char code[16];
					std::snprintf(code, sizeof(code), "U+%04X", block.first + k);
					list += code;
					++shown;
				}
			}
			RDA_LOG_INFO(missing << " of them are not in this font and draw as a box: " << list);
		}
		return true;
	}

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
		if (px <= 0.0f || mSizes.empty()) return 0;
		int best = 0;
		float bestDelta = std::fabs(mSizes[0].pixelHeight - px);
		for (int i = 1; i < static_cast<int>(mSizes.size()); ++i) {
			const float delta = std::fabs(mSizes[i].pixelHeight - px);
			if (delta < bestDelta) { best = i; bestDelta = delta; }
		}
		// Nothing is scaled, so a size that was not baked is drawn at the nearest one that
		// was. That is a decision the author did not make, and it is worth one line in the
		// log naming both numbers -- once per size, not once per frame.
		if (std::fabs(mSizes[best].pixelHeight - px) > 0.5f
		    && std::find(mUnbaked.begin(), mUnbaked.end(), px) == mUnbaked.end()) {
			mUnbaked.push_back(px);
			RDA_LOG_WARNING("Theme asks for " << px << "px text, which was not baked; drawing at "
			                << mSizes[best].pixelHeight << "px. Add it to GuiConfig::fontSizes.");
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

		// Two different problems, and telling them apart is the whole value of the line:
		// a codepoint outside GlyphRanges.h is a limit of this engine, one inside it is a
		// limit of the font the application chose. Only the second can be fixed by
		// changing a setting.
		const bool inTable = glyphSlot(cp) >= 0;
		char code[16];
		std::snprintf(code, sizeof(code), "U+%04X", cp);
		if (inTable) {
			RDA_LOG_WARNING(code << " is drawn as a box: the font in use does not have it.");
		} else {
			RDA_LOG_WARNING(code << " is drawn as a box: it is outside the range in"
			                        " GlyphRanges.h, which is Latin only.");
		}
	}

	int FontAtlas::slotFor(uint32_t cp) const {
		const int slot = glyphSlot(cp);
		if (slot >= 0 && static_cast<size_t>(slot) < mPresent.size() && mPresent[static_cast<size_t>(slot)]) {
			return slot;
		}
		noteMissing(cp);
		if (static_cast<size_t>(kReplacementSlot) < mPresent.size()
		    && mPresent[static_cast<size_t>(kReplacementSlot)]) {
			return kReplacementSlot;
		}
		return -1;
	}

	bool FontAtlas::quadFor(uint32_t cp, float& penX, float baselineY, GlyphQuad& out, int index) const {
		const Size& size = at(index);
		if (size.glyphs.empty()) return false;

		const int slot = slotFor(cp);
		if (slot < 0) {
			// Nothing to draw and nothing to draw it with: advance by a space so the rest
			// of the line keeps its position.
			penX += size.glyphs[kSpaceSlot].xadvance;
			return false;
		}
		const BakedGlyph& b = size.glyphs[static_cast<size_t>(slot)];
		if (b.x1 <= b.x0 || b.y1 <= b.y0) {
			// An empty box -- a space, or a mark the font draws as nothing. It still
			// advances; it just has no quad, which saves one per space in every label.
			penX += b.xadvance;
			return false;
		}
		float rx = std::floor(penX + b.xoff + 0.5f);
		float ry = std::floor(baselineY + b.yoff + 0.5f);
		out.x0 = rx;
		out.y0 = ry;
		out.x1 = rx + (b.x1 - b.x0);
		out.y1 = ry + (b.y1 - b.y0);
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
		const int slot = slotFor(cp);
		return size.glyphs[static_cast<size_t>(slot < 0 ? kSpaceSlot : slot)].xadvance;
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
			const int slot = slotFor(cp);
			width += size.glyphs[static_cast<size_t>(slot < 0 ? kSpaceSlot : slot)].xadvance;
		}
		return width;
	}
}
