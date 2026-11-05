#include <GraphicalSrc/FontAtlas.h>
#include <GraphicalSrc/GlyphRanges.h>
#include <Logger/Logger.h>
#include <vendor/stb_truetype/stb_truetype.h>
#include "FontInternal.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace RDA {

	static constexpr int kAtlasWidth = 1024;
	static constexpr int kMaxAtlasHeight = 4096;

	// Rows kept below the baked bands for glyphs nobody declared.
	//
	// Sized for what a session actually meets rather than for a script: a reader who
	// sees twenty distinct emoji in an afternoon is the normal case, and a thousand rows
	// holds several hundred of them at interface sizes. When it runs out the replacement
	// box comes back, which is what this did for every one of them before.
	static constexpr int kOnDemandRows = 1024;

	// One padding texel around a baked glyph, the same as the packer leaves, so bilinear
	// sampling at the edge of one cannot reach into its neighbour.
	static constexpr int kGlyphPad = 1;

	// The faces, in the order they were named. Kept for the life of the atlas: a glyph
	// baked on demand hours later is rasterised from the same font the first one was.
	struct FontAtlas::Faces {
		struct Face {
			std::vector<unsigned char> ttf;
			stbtt_fontinfo             info{};
		};
		std::vector<Face> faces;
		// The system's own faces for scripts the named ones do not cover -- Chinese,
		// Japanese, Korean, symbols -- in the order they are worth trying, and how many of
		// them have been loaded so far. Loaded one at a time, the first time a codepoint
		// nothing loaded has is asked for: a CJK face is twenty megabytes, and an interface
		// that never shows a Han character should never read one.
		std::vector<std::string> system;
		size_t                   systemLoaded = 0;
	};

	namespace {
		// Reads one face from disk. False, and nothing appended, for a file that is not
		// there or not a font.
		bool loadFace(const std::string& path, std::vector<FontAtlas::Faces::Face>& into);
	}

	FontAtlas::FontAtlas() : mFaces(std::make_unique<Faces>()) {}
	FontAtlas::~FontAtlas() = default;

	void FontAtlas::destroy() {
		mSizes.clear();
		mPresent.clear();
		mMissing.clear();
		mPixels.clear();
		if (mFaces) mFaces->faces.clear();
		mShelfX = mShelfY = mShelfHeight = 0;
		mDirtyTop = mDirtyBottom = 0;
		mRoomWarned = false;
		++mRevision;
	}

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

	void FontAtlas::setScale(float physicalPerUnit) {
		mScale = (physicalPerUnit > 0.0f) ? physicalPerUnit : 1.0f;
	}

	bool FontAtlas::bake(const std::string& ttfPath, const std::vector<float>& requestedHeights) {
		if (requestedHeights.empty()) {
			RDA_LOG_ERROR("Font atlas asked for no sizes: " << ttfPath);
			return false;
		}

		// Everything below this line is in physical pixels, because that is what an atlas
		// is: a picture. The sizes that came in are layout units, so they are converted
		// once, here, and converted back only where a measurement is handed out. See
		// setScale.
		std::vector<float> pixelHeights;
		pixelHeights.reserve(requestedHeights.size());
		for (float height : requestedHeights) pixelHeights.push_back(height * mScale);
		// One path, or several separated by ';'. The first is the body font; the rest are
		// only ever asked about a codepoint the ones before them do not have.
		std::vector<std::string> paths;
		for (size_t at = 0; at <= ttfPath.size();) {
			const size_t stop = ttfPath.find(';', at);
			std::string one = ttfPath.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
			// Trimmed, because "body.ttf; fallback.ttf" is what a person writes.
			while (!one.empty() && (one.front() == ' ' || one.front() == '\t')) one.erase(one.begin());
			while (!one.empty() && (one.back() == ' ' || one.back() == '\t')) one.pop_back();
			if (!one.empty()) paths.push_back(std::move(one));
			if (stop == std::string::npos) break;
			at = stop + 1;
		}
		if (paths.empty()) {
			RDA_LOG_ERROR("Font atlas asked for no font: " << ttfPath);
			return false;
		}

		mFaces->faces.clear();
		for (const std::string& path : paths) {
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file) {
				// The body font not being there is fatal; a fallback not being there is a
				// fact about this machine, and an interface without emoji still works.
				if (mFaces->faces.empty()) {
					RDA_LOG_ERROR("Font file not found: " << path);
					return false;
				}
				RDA_LOG_WARNING("Fallback font not found, skipping it: " << path);
				continue;
			}
			std::streamsize size = file.tellg();
			file.seekg(0);
			Faces::Face face;
			face.ttf.resize(static_cast<size_t>(size));
			if (!file.read(reinterpret_cast<char*>(face.ttf.data()), size)) {
				if (mFaces->faces.empty()) {
					RDA_LOG_ERROR("Failed to read font file: " << path);
					return false;
				}
				RDA_LOG_WARNING("Fallback font could not be read, skipping it: " << path);
				continue;
			}
			if (!stbtt_InitFont(&face.info, face.ttf.data(),
			                    stbtt_GetFontOffsetForIndex(face.ttf.data(), 0))) {
				if (mFaces->faces.empty()) {
					RDA_LOG_ERROR("Not a font this can read: " << path);
					return false;
				}
				RDA_LOG_WARNING("Fallback is not a font this can read, skipping it: " << path);
				continue;
			}
			mFaces->faces.push_back(std::move(face));
		}

		// The system's faces, after the named ones and not instead of any of them.
		mFaces->system.clear();
		mFaces->systemLoaded = 0;
		for (std::string& path : detail::systemFallbacks()) {
			bool named = false;
			for (const std::string& one : paths) {
				std::error_code ec;
				if (std::filesystem::equivalent(one, path, ec)) { named = true; break; }
			}
			if (!named) mFaces->system.push_back(std::move(path));
		}

		// Everything below bakes the preloaded blocks from the body font, as it always
		// has. The fallbacks are for what those blocks do not cover, which is decided one
		// codepoint at a time in bakeOnDemand.
		const std::vector<unsigned char>& ttf = mFaces->faces.front().ttf;
		stbtt_fontinfo& font = mFaces->faces.front().info;

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
		// The baked bands, the white block, and the room on-demand glyphs go in.
		mDynamicTop = totalRows + 6;
		int atlasHeight = 1;
		while (atlasHeight < mDynamicTop + kOnDemandRows) atlasHeight *= 2;
		if (atlasHeight > kMaxAtlasHeight) {
			// The scale is named when there is one, because at 3x the sizes in this
			// message are not the sizes anybody wrote and "ask for smaller ones" sends
			// them to the wrong file.
			std::string blame = "; ask for fewer, or smaller ones";
			if (mScale != 1.0f) {
				blame = " at " + std::to_string(static_cast<int>(std::lround(mScale * 100.0f)))
				      + "% of the sizes asked for; ask for fewer sizes, smaller ones, or a"
				        " smaller GuiConfig::uiScale";
			}
			RDA_LOG_ERROR("Font atlas would need " << atlasHeight << " rows for "
			              << pixelHeights.size() << " sizes" << blame);
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
			// Back to layout units from here on. The box stays in texels, because that is
			// what a UV is made of; everything that is a distance on screen is divided.
			baked.pixelHeight = requestedHeights[i];
			baked.ascent = asc * scale / mScale;
			baked.lineAdvance = (asc - desc + gap) * scale / mScale;
			baked.glyphs.resize(static_cast<size_t>(kGlyphCount));
			for (int c = 0; c < kGlyphCount; ++c) {
				const stbtt_packedchar& b = bands[i].chars[static_cast<size_t>(c)];
				// Only the vertical box moves: the band was blitted down the atlas, and
				// everything else about a glyph is relative to the pen.
				baked.glyphs[static_cast<size_t>(c)] = {
					b.x0, static_cast<unsigned short>(b.y0 + atlasY),
					b.x1, static_cast<unsigned short>(b.y1 + atlasY),
					b.xoff / mScale, b.yoff / mScale, b.xadvance / mScale,
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

		// Kept, because an on-demand glyph is drawn into it and the rectangle it touched
		// is what gets sent. Without a copy there would be nothing to draw into.
		mShelfX = 0;
		mShelfY = mDynamicTop;
		mShelfHeight = 0;
		mDirtyTop = mDirtyBottom = 0;
		mRoomWarned = false;
		++mRevision;

		// What on-demand glyphs are drawn into, and what a GPU image is made from.
		mPixels = std::move(bitmap);

		std::string sizeList;
		for (size_t i = 0; i < pixelHeights.size(); ++i) {
			if (i) sizeList += ", ";
			sizeList += std::to_string(static_cast<int>(std::lround(pixelHeights[i])));
		}
		// The sizes named are the ones in the atlas, which on a scaled display are not the
		// ones the application asked for -- so the scale is named too, or the line reads as
		// though the engine ignored the theme.
		std::string scaleNote;
		if (mScale != 1.0f) {
			scaleNote = " (" + std::to_string(static_cast<int>(std::lround(mScale * 100.0f)))
			          + "% of the sizes asked for)";
		}
		RDA_LOG_SUCCESS("Font atlas baked: " << ttfPath << " at " << sizeList
		               << "px" << scaleNote << " (" << kAtlasWidth << "x" << atlasHeight << "), "
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

	bool FontAtlas::bakeOnDemand(uint32_t cp, int index) const {
		if (!mFaces || mFaces->faces.empty() || mPixels.empty()) return false;
		if (index < 0 || static_cast<size_t>(index) >= mSizes.size()) return false;
		const Size& size = mSizes[static_cast<size_t>(index)];

		// The first face that has it. This is the whole of the fallback rule, and it is
		// per codepoint rather than per font: a monospace body keeps the alphabet and an
		// emoji face is asked only about what the body does not draw.
		const stbtt_fontinfo* chosen = nullptr;
		int glyph = 0;
		for (const Faces::Face& face : mFaces->faces) {
			const int found = stbtt_FindGlyphIndex(&face.info, static_cast<int>(cp));
			if (found != 0) { chosen = &face.info; glyph = found; break; }
		}
		// Nothing named has it: the system's faces, one more at a time, until one does or
		// they run out. Each is read once and kept, so the second Han character costs a
		// lookup rather than another twenty megabytes.
		while (!chosen && mFaces->systemLoaded < mFaces->system.size()) {
			const std::string& path = mFaces->system[mFaces->systemLoaded++];
			if (!loadFace(path, mFaces->faces)) continue;
			RDA_LOG_INFO("Font: " << path << " is drawing what the named fonts do not have");
			const Faces::Face& added = mFaces->faces.back();
			const int found = stbtt_FindGlyphIndex(&added.info, static_cast<int>(cp));
			if (found != 0) { chosen = &added.info; glyph = found; }
		}
		if (!chosen) {
			// Nobody has it. Remembered as the replacement box rather than simply
			// refused, so the next draw is a map hit instead of another walk over every
			// face -- which for a document in a script none of them covers is the
			// difference between one search and one per character per frame.
			const auto box = (static_cast<size_t>(kReplacementSlot) < size.glyphs.size())
				? size.glyphs[static_cast<size_t>(kReplacementSlot)] : BakedGlyph{};
			size.onDemand.emplace(cp, box);
			noteMissing(cp);
			// No revision bump: nothing new was drawn into the atlas, and forcing a walk
			// for a box that was already on screen would be a frame for nothing.
			return true;
		}

		// pixelHeight is in layout units, and a glyph is rasterised in pixels -- the same
		// conversion the preloaded sizes went through at bake time.
		const float scale = stbtt_ScaleForPixelHeight(chosen, size.pixelHeight * mScale);
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		stbtt_GetGlyphBitmapBox(chosen, glyph, scale, scale, &x0, &y0, &x1, &y1);
		const int w = x1 - x0;
		const int h = y1 - y0;

		int advanceRaw = 0, bearing = 0;
		stbtt_GetGlyphHMetrics(chosen, glyph, &advanceRaw, &bearing);

		BakedGlyph baked{};
		baked.xadvance = advanceRaw * scale / mScale;
		baked.xoff = static_cast<float>(x0) / mScale;
		baked.yoff = static_cast<float>(y0) / mScale;

		if (w <= 0 || h <= 0) {
			// A codepoint the face draws as nothing -- a space of its own, a joiner. It
			// still advances, and recording it means never asking this question again.
			size.onDemand.emplace(cp, baked);
			++mRevision;
			return true;
		}

		// A shelf: fill along a row, drop to a new one below the tallest thing on it.
		const int needW = w + kGlyphPad;
		const int needH = h + kGlyphPad;
		if (mShelfX + needW > kAtlasWidth) {
			mShelfY += mShelfHeight;
			mShelfX = 0;
			mShelfHeight = 0;
		}
		const int atlasHeight = static_cast<int>(mAtlasH);
		if (mShelfY + needH > atlasHeight) {
			if (!mRoomWarned) {
				mRoomWarned = true;
				RDA_LOG_WARNING("Font atlas has no room left for new glyphs; the rest draw "
				                "as the replacement box. Ask for fewer sizes, or smaller ones.");
			}
			return false;
		}

		const int px = mShelfX;
		const int py = mShelfY;
		// Straight into the CPU copy, one row at a time, at the atlas' stride.
		stbtt_MakeGlyphBitmap(chosen,
			mPixels.data() + static_cast<size_t>(py) * kAtlasWidth + px,
			w, h, kAtlasWidth, scale, scale, glyph);

		mShelfX += needW;
		mShelfHeight = (std::max)(mShelfHeight, needH);

		baked.x0 = static_cast<unsigned short>(px);
		baked.y0 = static_cast<unsigned short>(py);
		baked.x1 = static_cast<unsigned short>(px + w);
		baked.y1 = static_cast<unsigned short>(py + h);
		size.onDemand.emplace(cp, baked);

		// Grow the pending rectangle rather than sending now: a line that met twenty new
		// characters is one upload at the end of the frame instead of twenty stalls.
		if (mDirtyBottom <= mDirtyTop) {
			mDirtyTop = py;
			mDirtyBottom = py + h;
		} else {
			mDirtyTop = (std::min)(mDirtyTop, py);
			mDirtyBottom = (std::max)(mDirtyBottom, py + h);
		}
		++mRevision;
		return true;
	}

	FontAtlas::DirtyRows FontAtlas::takeDirtyRows() {
		const DirtyRows rows{ mDirtyTop, mDirtyBottom };
		mDirtyTop = mDirtyBottom = 0;
		return rows;
	}

	namespace {
		bool loadFace(const std::string& path, std::vector<FontAtlas::Faces::Face>& into) {
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file) return false;
			const std::streamsize size = file.tellg();
			if (size <= 0) return false;
			file.seekg(0);
			FontAtlas::Faces::Face face;
			face.ttf.resize(static_cast<size_t>(size));
			if (!file.read(reinterpret_cast<char*>(face.ttf.data()), size)) return false;
			const int offset = stbtt_GetFontOffsetForIndex(face.ttf.data(), 0);
			if (offset < 0 || !stbtt_InitFont(&face.info, face.ttf.data(), offset)) return false;
			into.push_back(std::move(face));
			return true;
		}
	}
}
