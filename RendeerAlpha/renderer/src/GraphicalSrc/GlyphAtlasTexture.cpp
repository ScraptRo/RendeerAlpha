#include <GraphicalSrc/GlyphAtlasTexture.h>
#include <GraphicalSrc/FontAtlas.h>
#include <Logger/Logger.h>
#include <cstddef>
#include <cstdint>

namespace RDA {

	bool GlyphAtlasTexture::create(FontAtlas& atlas) {
		if (!atlas.isValid()) {
			RDA_LOG_ERROR("Font atlas has not been baked, so there is nothing to make a texture of");
			return false;
		}
		const uint32_t width = static_cast<uint32_t>(atlas.atlasWidth());
		const uint32_t height = static_cast<uint32_t>(atlas.atlasHeight());

		TextureDesc imageDesc{};
		imageDesc.width = width;
		imageDesc.height = height;
		imageDesc.format = VK_FORMAT_R8_UNORM;
		imageDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		imageDesc.withSampler = true;
		if (!mTexture.create(imageDesc)) {
			RDA_LOG_ERROR("Failed to create font atlas texture");
			return false;
		}
		if (!mTexture.uploadPixels(atlas.pixels().data(),
		                           static_cast<VkDeviceSize>(width) * height)) {
			RDA_LOG_ERROR("Failed to upload font atlas");
			return false;
		}
		// All of it has been sent, so nothing is pending any more.
		atlas.takeDirtyRows();
		return true;
	}

	void GlyphAtlasTexture::uploadPending(FontAtlas& atlas) {
		if (!mTexture.isValid() || atlas.pixels().empty()) return;
		const FontAtlas::DirtyRows rows = atlas.takeDirtyRows();
		if (rows.empty()) return;
		const uint32_t width = static_cast<uint32_t>(atlas.atlasWidth());
		mTexture.uploadRegion(atlas.pixels().data() + static_cast<size_t>(rows.top) * width,
		                      0, static_cast<uint32_t>(rows.top),
		                      width, static_cast<uint32_t>(rows.bottom - rows.top));
	}
}
