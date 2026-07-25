#pragma once
#include <Core/Datatypes.h>

// Forward declare VMA handles to keep the heavy header out of this interface.
typedef struct VmaAllocator_T*  VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace RDA{

	// Describes a GPU image. Defaults describe a sampled sRGB color texture, but
	// the same struct builds depth / color / input attachments for the G-buffer by
	// changing usage, format and aspect.
	struct TextureDesc {
		uint32_t           width = 0;
		uint32_t           height = 0;
		VkFormat           format = VK_FORMAT_R8G8B8A8_SRGB;
		VkImageUsageFlags  usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		bool               withSampler = true; // attachments usually pass false
	};

	// RAII wrapper around a VkImage + its VMA allocation, an image view, and an
	// optional sampler. Tracks the current layout so transitions stay correct.
	// Move-only: ownership of the GPU image transfers, never copies.
	class Texture
	{
	public:
		Texture() = default;
		~Texture();

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;

		// Allocate an (empty) image from a description.
		bool create(const TextureDesc& desc);
		// Load an image file (via stb_image) into a sampled device-local texture.
		static Texture loadFromFile(const std::string& path, bool srgb = true);

		// Copy tightly-packed pixel data in and leave the image ready to sample.
		bool uploadPixels(const void* pixels, VkDeviceSize sizeBytes);

		// Immediate (blocking) layout transition. For per-frame work, prefer
		// recording a barrier into your own command buffer instead.
		void transitionLayout(VkImageLayout newLayout);

		VkImage        image()   const { return mImage; }
		VkImageView    view()    const { return mView; }
		VkSampler      sampler() const { return mSampler; }
		VkFormat       format()  const { return mFormat; }
		VkExtent2D     extent()  const { return mExtent; }
		VkImageLayout  layout()  const { return mLayout; }
		bool           isValid() const { return mImage != VK_NULL_HANDLE; }

		// Ready-to-bind descriptor for a combined image sampler.
		VkDescriptorImageInfo descriptorInfo(
			VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) const;

		void destroy();

	private:
		VkImage            mImage = VK_NULL_HANDLE;
		VmaAllocation      mAllocation = nullptr;
		VkImageView        mView = VK_NULL_HANDLE;
		VkSampler          mSampler = VK_NULL_HANDLE;
		VkFormat           mFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D         mExtent{};
		VkImageAspectFlags mAspect = VK_IMAGE_ASPECT_COLOR_BIT;
		VkImageLayout      mLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	};
}
