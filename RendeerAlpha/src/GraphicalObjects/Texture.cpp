#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalObjects/Texture.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <Logger/Logger.h>
#include <vendor/vma/vk_mem_alloc.h>
#include <vendor/stb_image/stb_image.h>
#include <atomic>
#include <functional>

namespace RDA {

	namespace {
		// Handed out by create(), never reused. Atomic because textures are created from
		// whichever thread owns the loop, and an application in ThreadMode::Owned has that
		// on a different thread from the one that started it.
		std::atomic<uint64_t> gNextTextureRevision{ 1 };
	}

	// One-shot GPU work on the graphics queue, using a transient command pool.
	static void immediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
		GPUInfo& gpu = getGPU();
		VkDevice device = gpu.LDevice;

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = gpu.graphicsFamily;
		VkCommandPool pool = VK_NULL_HANDLE;
		vkCreateCommandPool(device, &poolInfo, nullptr, &pool);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = pool;
		allocInfo.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		vkAllocateCommandBuffers(device, &allocInfo, &cmd);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &beginInfo);

		record(cmd);

		vkEndCommandBuffer(cmd);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;
		vkQueueSubmit(gpu.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(gpu.graphicsQueue);

		vkFreeCommandBuffers(device, pool, 1, &cmd);
		vkDestroyCommandPool(device, pool, nullptr);
	}

	// Records an image layout transition, choosing sensible stage/access masks for
	// the transitions we actually use. Falls back to broad masks otherwise.
	static void recordTransition(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
	                             VkImageLayout oldLayout, VkImageLayout newLayout,
	                             uint32_t baseMipLevel = 0, uint32_t levelCount = 1) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = aspect;
		barrier.subresourceRange.baseMipLevel = baseMipLevel;
		barrier.subresourceRange.levelCount = levelCount;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		// The two halves of mip generation: a level becomes readable so the next one can
		// be blitted from it, then readable by shaders once it has been used.
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else {
			barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		}

		vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	Texture::~Texture() {
		destroy();
	}

	Texture::Texture(Texture&& other) noexcept {
		mImage = other.mImage;
		mAllocation = other.mAllocation;
		mView = other.mView;
		mSampler = other.mSampler;
		mFormat = other.mFormat;
		mExtent = other.mExtent;
		mAspect = other.mAspect;
		mLayout = other.mLayout;
		mMipLevels = other.mMipLevels;
		// The image moves and keeps its identity with it; the husk left behind holds none.
		mRevision = other.mRevision;
		other.mImage = VK_NULL_HANDLE;
		other.mAllocation = nullptr;
		other.mView = VK_NULL_HANDLE;
		other.mSampler = VK_NULL_HANDLE;
		other.mRevision = 0;
	}

	Texture& Texture::operator=(Texture&& other) noexcept {
		if (this != &other) {
			destroy();
			mImage = other.mImage;
			mAllocation = other.mAllocation;
			mView = other.mView;
			mSampler = other.mSampler;
			mFormat = other.mFormat;
			mExtent = other.mExtent;
			mAspect = other.mAspect;
			mLayout = other.mLayout;
			mMipLevels = other.mMipLevels;
			mRevision = other.mRevision;
			other.mImage = VK_NULL_HANDLE;
			other.mAllocation = nullptr;
			other.mView = VK_NULL_HANDLE;
			other.mSampler = VK_NULL_HANDLE;
			other.mRevision = 0;
		}
		return *this;
	}

	bool Texture::create(const TextureDesc& desc) {
		destroy();
		mFormat = desc.format;
		mExtent = { desc.width, desc.height };
		mAspect = desc.aspect;
		mLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		mMipLevels = 1;

		VkImageUsageFlags usage = desc.usage;
		if (desc.mipmapped && desc.width > 0 && desc.height > 0) {
			// Mips are generated by blitting each level into the next, which needs the
			// format to support a linear filter on optimal tiling. Where it does not, the
			// texture stays single-level rather than failing.
			VkFormatProperties formatProps{};
			vkGetPhysicalDeviceFormatProperties(getPhysicalDevice(), desc.format, &formatProps);
			const bool canBlitLinear =
				(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) &&
				(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
				(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
			if (canBlitLinear) {
				uint32_t largest = (desc.width > desc.height) ? desc.width : desc.height;
				while (largest > 1) { ++mMipLevels; largest /= 2; }
				// Every level but the last is read from as a blit source.
				usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			} else {
				RDA_LOG_WARNING("Format cannot be linearly blitted; texture stays unmipmapped");
			}
		}

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = desc.format;
		imageInfo.extent = { desc.width, desc.height, 1 };
		imageInfo.mipLevels = mMipLevels;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = usage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO; // device-local

		if (vmaCreateImage(getAllocator(), &imageInfo, &allocInfo, &mImage, &mAllocation, nullptr) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to allocate image");
			mImage = VK_NULL_HANDLE;
			mAllocation = nullptr;
			return false;
		}

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = mImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = desc.format;
		viewInfo.subresourceRange.aspectMask = desc.aspect;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mMipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(getDevice(), &viewInfo, nullptr, &mView) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create image view");
			destroy();
			return false;
		}

		if (desc.withSampler) {
			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(getPhysicalDevice(), &props);

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.addressModeU = desc.addressMode;
			samplerInfo.addressModeV = desc.addressMode;
			samplerInfo.addressModeW = desc.addressMode;
			// Anisotropy only helps a minified colour texture; a depth attachment sampled
			// for shadows is compared, not filtered for detail.
			const bool wantsAnisotropy = (desc.aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0;
			samplerInfo.anisotropyEnable = wantsAnisotropy ? VK_TRUE : VK_FALSE;
			samplerInfo.maxAnisotropy = wantsAnisotropy ? props.limits.maxSamplerAnisotropy : 1.0f;
			samplerInfo.borderColor = desc.borderColor;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.minLod = 0.0f;
			// Without this the sampler is pinned to level 0 no matter how many the image
			// has — which is what made anisotropy pointless before.
			samplerInfo.maxLod = static_cast<float>(mMipLevels);
			if (vkCreateSampler(getDevice(), &samplerInfo, nullptr, &mSampler) != VK_SUCCESS) {
				RDA_LOG_ERROR("Failed to create sampler");
				destroy();
				return false;
			}
		}

		// Only here, on the one path that ends with a complete image. Every failure above
		// went through destroy(), which leaves the revision at 0 — so a half-built texture
		// never claims an identity, and a cache can tell the difference.
		mRevision = gNextTextureRevision.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	Texture Texture::loadFromFile(const std::string& path, bool srgb) {
		Texture texture;
		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels) {
			RDA_LOG_ERROR("Failed to load image: " << path);
			return texture;
		}

		TextureDesc desc;
		desc.width = static_cast<uint32_t>(width);
		desc.height = static_cast<uint32_t>(height);
		desc.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.withSampler = true;
		desc.mipmapped = true; // a loaded image is content, and content gets minified

		if (texture.create(desc)) {
			VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;
			texture.uploadPixels(pixels, size);
		}
		stbi_image_free(pixels);
		return texture;
	}

	bool Texture::uploadPixels(const void* pixels, VkDeviceSize sizeBytes) {
		if (!isValid() || !pixels || sizeBytes == 0) return false;

		MemoryBuffer staging;
		if (!staging.create(sizeBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryResidence::CpuToGpu)) {
			return false;
		}
		if (!staging.upload(pixels, sizeBytes)) return false;

		VkBuffer stagingHandle = staging.handle();
		VkImage image = mImage;
		VkImageAspectFlags aspect = mAspect;
		VkExtent2D extent = mExtent;
		uint32_t mipLevels = mMipLevels;

		immediateSubmit([=](VkCommandBuffer cmd) {
			// The whole chain starts as a transfer destination; level 0 is filled from the
			// staging buffer, the rest are filled by blitting down from it.
			recordTransition(cmd, image, aspect,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, mipLevels);

			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = aspect;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { extent.width, extent.height, 1 };
			vkCmdCopyBufferToImage(cmd, stagingHandle, image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			int32_t width = static_cast<int32_t>(extent.width);
			int32_t height = static_cast<int32_t>(extent.height);
			for (uint32_t level = 1; level < mipLevels; ++level) {
				// The previous level has to finish being written before it can be read.
				recordTransition(cmd, image, aspect,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					level - 1, 1);

				const int32_t nextWidth = (width > 1) ? width / 2 : 1;
				const int32_t nextHeight = (height > 1) ? height / 2 : 1;

				VkImageBlit blit{};
				blit.srcOffsets[0] = { 0, 0, 0 };
				blit.srcOffsets[1] = { width, height, 1 };
				blit.srcSubresource.aspectMask = aspect;
				blit.srcSubresource.mipLevel = level - 1;
				blit.srcSubresource.baseArrayLayer = 0;
				blit.srcSubresource.layerCount = 1;
				blit.dstOffsets[0] = { 0, 0, 0 };
				blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
				blit.dstSubresource.aspectMask = aspect;
				blit.dstSubresource.mipLevel = level;
				blit.dstSubresource.baseArrayLayer = 0;
				blit.dstSubresource.layerCount = 1;
				vkCmdBlitImage(cmd,
					image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blit, VK_FILTER_LINEAR);

				// Done as a source; hand it to the shader and move down a level.
				recordTransition(cmd, image, aspect,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					level - 1, 1);

				width = nextWidth;
				height = nextHeight;
			}

			// The smallest level was never blitted from, so it is still a transfer target.
			recordTransition(cmd, image, aspect,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				mipLevels - 1, 1);
		});

		mLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return true;
	}

	void Texture::transitionLayout(VkImageLayout newLayout) {
		if (!isValid()) return;
		VkImage image = mImage;
		VkImageAspectFlags aspect = mAspect;
		VkImageLayout oldLayout = mLayout;
		immediateSubmit([=](VkCommandBuffer cmd) {
			recordTransition(cmd, image, aspect, oldLayout, newLayout);
		});
		mLayout = newLayout;
	}

	VkDescriptorImageInfo Texture::descriptorInfo(VkImageLayout layout) const {
		VkDescriptorImageInfo info{};
		info.sampler = mSampler;
		info.imageView = mView;
		info.imageLayout = layout;
		return info;
	}

	void Texture::destroy() {
		// See MemoryBuffer::destroy — textures may outlive the engine's shutdown,
		// by which point the device/allocator have already freed everything.
		// Cleared on both paths: a destroyed texture holds no image, so it must not keep
		// answering with the identity of one. That is what lets a cache notice.
		mRevision = 0;

		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE || getAllocator() == nullptr) {
			mSampler = VK_NULL_HANDLE;
			mView = VK_NULL_HANDLE;
			mImage = VK_NULL_HANDLE;
			mAllocation = nullptr;
			return;
		}

		if (mSampler != VK_NULL_HANDLE) {
			vkDestroySampler(device, mSampler, nullptr);
			mSampler = VK_NULL_HANDLE;
		}
		if (mView != VK_NULL_HANDLE) {
			vkDestroyImageView(device, mView, nullptr);
			mView = VK_NULL_HANDLE;
		}
		if (mImage != VK_NULL_HANDLE) {
			vmaDestroyImage(getAllocator(), mImage, mAllocation);
			mImage = VK_NULL_HANDLE;
			mAllocation = nullptr;
		}
	}
}
