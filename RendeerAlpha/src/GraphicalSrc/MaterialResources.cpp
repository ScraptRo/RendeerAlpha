#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalSrc/MaterialResources.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
#include <vector>

namespace RDA {

	bool MaterialResources::init() {
		mLayout = DescriptorLayoutBuilder()
			.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build();
		if (mLayout == VK_NULL_HANDLE) return false;

		// Materials are created during setup and kept, never recycled. The allocator opens
		// another block if an application makes more than this, so it is a block size
		// rather than the ceiling it used to be.
		constexpr uint32_t kMaterialsPerBlock = 128;
		std::vector<VkDescriptorPoolSize> sizes = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaterialsPerBlock * 3 },
		};
		if (!mPool.init(kMaterialsPerBlock, sizes)) return false;

		TextureDesc white{};
		white.width = white.height = 1;
		white.format = VK_FORMAT_R8G8B8A8_UNORM; // data, not colour: no sRGB decode
		if (!mWhite.create(white)) return false;
		const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
		if (!mWhite.uploadPixels(whitePixel, sizeof(whitePixel))) return false;

		TextureDesc flat = white;
		if (!mFlatNormal.create(flat)) return false;
		const uint8_t flatPixel[4] = { 128, 128, 255, 255 };
		if (!mFlatNormal.uploadPixels(flatPixel, sizeof(flatPixel))) return false;

		return true;
	}

	VkDescriptorSet MaterialResources::allocateSet(const MaterialTextures& textures) {
		VkDescriptorSet set = mPool.allocate(mLayout);
		if (set == VK_NULL_HANDLE) {
			RDA_LOG_ERROR("Out of material descriptor sets; drawing untextured");
			return VK_NULL_HANDLE;
		}

		// Fall back to the neutral defaults for anything the caller left unset.
		auto pick = [&](const obj_ref<Texture>& ref, const Texture& fallback) -> const Texture& {
			return (ref.IsValid() && ref->isValid()) ? *ref : fallback;
		};
		const Texture& albedo = pick(textures.albedo, mWhite);
		const Texture& normal = pick(textures.normal, mFlatNormal);
		const Texture& metalRough = pick(textures.metallicRoughness, mWhite);

		DescriptorWriter()
			.writeImage(0, albedo.view(), albedo.sampler(),
			            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.writeImage(1, normal.view(), normal.sampler(),
			            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.writeImage(2, metalRough.view(), metalRough.sampler(),
			            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.update(set);
		return set;
	}

	void MaterialResources::releaseSet(VkDescriptorSet set) {
		// Straight back to the allocator's free list, so the next material reuses it
		// instead of the pool growing by one more.
		mPool.recycle(mLayout, set);
	}

	void MaterialResources::destroy() {
		mWhite.destroy();
		mFlatNormal.destroy();
		mPool.destroy();
		if (mLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(getDevice(), mLayout, nullptr);
			mLayout = VK_NULL_HANDLE;
		}
	}
}
