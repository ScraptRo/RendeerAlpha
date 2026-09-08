#pragma once
#include <cstdint>
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
		// Build a full mip chain and filter between levels. Worth it for anything
		// sampled at a distance or at a grazing angle — without it a detailed surface
		// aliases badly in motion, and the anisotropic filter has nothing to work with.
		// Ignored for attachments and for formats the device cannot linearly blit.
		bool               mipmapped = false;
		// Sampler wrapping. A shadow map wants CLAMP_TO_BORDER with a white (far) border,
		// so anything outside the mapped area reads as "nothing in front of it" — lit —
		// rather than wrapping around and shadowing the wrong part of the scene.
		VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkBorderColor        borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
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

		VkImage        image()   const { return mImage; }
		VkImageView    view()    const { return mView; }
		VkSampler      sampler() const { return mSampler; }
		VkFormat       format()  const { return mFormat; }
		VkExtent2D     extent()  const { return mExtent; }
		VkImageLayout  layout()  const { return mLayout; }
		uint32_t       mipLevels() const { return mMipLevels; }
		bool           isValid() const { return mImage != VK_NULL_HANDLE; }

		// Identity of the image currently inside this object, as opposed to identity of
		// the object itself.
		//
		// The two are not the same, and assuming they were has already cost this engine a
		// bug. A Texture that belongs to a render target is a *member* of that target, so
		// it keeps its address when the target is rebuilt at a new size — while the image,
		// view and sampler inside it are destroyed and replaced. Anything that caches by
		// `const Texture*` (a descriptor set, most obviously) is therefore keyed on
		// something that does not change when the thing it describes does, and will go on
		// naming a destroyed view.
		//
		// So: cache on the pointer if you like, but record this alongside it and compare.
		// Values come from a process-wide counter and are never reused, which is what a
		// recycled VkImageView handle cannot promise. Zero means "holds no image", and no
		// live texture ever has revision 0.
		uint64_t       revision() const { return mRevision; }

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
		uint32_t           mMipLevels = 1;
		// Bumped on every successful create(), cleared on destroy(). See revision().
		uint64_t           mRevision = 0;
	};
}
