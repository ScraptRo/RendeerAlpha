#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalSrc/FontAtlas.h>
#include <Logger/Logger.h>
#include <vendor/stb_truetype/stb_truetype.h>
#include <fstream>
#include <cmath>

namespace RDA {

	static constexpr int kFirstChar = 32;
	static constexpr int kCharCount = 95; // 32..126 inclusive

	bool FontAtlas::bake(const std::string& ttfPath, float pixelHeight) {
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

		const int W = 512, H = 512;
		std::vector<unsigned char> bitmap(static_cast<size_t>(W) * H, 0);

		std::vector<stbtt_bakedchar> cdata(kCharCount);
		int firstUnused = stbtt_BakeFontBitmap(ttf.data(), 0, pixelHeight,
			bitmap.data(), W, H, kFirstChar, kCharCount, cdata.data());
		if (firstUnused <= 0) {
			RDA_LOG_ERROR("Font atlas too small to bake " << ttfPath << " at " << pixelHeight << "px");
			return false;
		}

		// Reserve a small solid-white block below the glyphs, so solid rects can sample
		// the same atlas as text and everything batches into one draw.
		int wy = firstUnused + 1;
		if (wy + 4 >= H) wy = H - 5;
		for (int j = 0; j < 4; ++j) {
			for (int i = 0; i < 4; ++i) {
				bitmap[static_cast<size_t>(wy + j) * W + i] = 255;
			}
		}
		mWhiteUV = glm::vec2(2.0f / W, (wy + 2.0f) / H);

		// Vertical metrics for baseline placement.
		stbtt_fontinfo font{};
		stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0));
		float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);
		int asc = 0, desc = 0, gap = 0;
		stbtt_GetFontVMetrics(&font, &asc, &desc, &gap);
		mAscent = asc * scale;
		mLineAdvance = (asc - desc + gap) * scale;
		mPixelHeight = pixelHeight;
		mAtlasW = static_cast<float>(W);
		mAtlasH = static_cast<float>(H);

		mChars.resize(kCharCount);
		for (int i = 0; i < kCharCount; ++i) {
			mChars[i] = { cdata[i].x0, cdata[i].y0, cdata[i].x1, cdata[i].y1,
			              cdata[i].xoff, cdata[i].yoff, cdata[i].xadvance };
		}

		TextureDesc imageDesc{};
		imageDesc.width = W;
		imageDesc.height = H;
		imageDesc.format = VK_FORMAT_R8_UNORM;
		imageDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		imageDesc.withSampler = true;
		if (!mTexture.create(imageDesc)) {
			RDA_LOG_ERROR("Failed to create font atlas texture");
			return false;
		}
		if (!mTexture.uploadPixels(bitmap.data(), static_cast<VkDeviceSize>(W) * H)) {
			RDA_LOG_ERROR("Failed to upload font atlas");
			return false;
		}

		RDA_LOG_SUCCES("Font atlas baked: " << ttfPath << " (" << pixelHeight << "px)");
		return true;
	}

	bool FontAtlas::quadFor(char c, float& penX, float baselineY, GlyphQuad& out) const {
		unsigned char uc = static_cast<unsigned char>(c);
		if (uc < kFirstChar || uc >= kFirstChar + kCharCount || mChars.empty()) {
			// Unknown glyph: advance by a space so layout doesn't collapse.
			penX += mChars.empty() ? 0.0f : mChars[' ' - kFirstChar].xadvance;
			return false;
		}
		const BakedGlyph& b = mChars[uc - kFirstChar];
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

	float FontAtlas::textWidth(const std::string& text) const {
		if (mChars.empty()) return 0.0f;
		float width = 0.0f;
		for (char c : text) {
			unsigned char uc = static_cast<unsigned char>(c);
			if (uc < kFirstChar || uc >= kFirstChar + kCharCount) {
				width += mChars[' ' - kFirstChar].xadvance;
			} else {
				width += mChars[uc - kFirstChar].xadvance;
			}
		}
		return width;
	}
}
