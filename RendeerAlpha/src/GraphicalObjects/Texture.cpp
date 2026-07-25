#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalObjects/Texture.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <Logger/Logger.h>
#include <vendor/vma/vk_mem_alloc.h>
#include <vendor/stb_image/stb_image.h>
#include <functional>

namespace RDA {

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
	                             VkImageLayout oldLayout, VkImageLayout newLayout) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = aspect;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
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
		other.mImage = VK_NULL_HANDLE;
		other.mAllocation = nullptr;
		other.mView = VK_NULL_HANDLE;
		other.mSampler = VK_NULL_HANDLE;
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
			other.mImage = VK_NULL_HANDLE;
			other.mAllocation = nullptr;
			other.mView = VK_NULL_HANDLE;
			other.mSampler = VK_NULL_HANDLE;
		}
		return *this;
	}

	bool Texture::create(const TextureDesc& desc) {
		destroy();
		mFormat = desc.format;
		mExtent = { desc.width, desc.height };
		mAspect = desc.aspect;
		mLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = desc.format;
		imageInfo.extent = { desc.width, desc.height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = desc.usage;
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
		viewInfo.subresourceRange.levelCount = 1;
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
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = props.limits.maxSamplerAnisotropy;
			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.minLod = 0.0f;
			samplerInfo.maxLod = 0.0f;
			if (vkCreateSampler(getDevice(), &samplerInfo, nullptr, &mSampler) != VK_SUCCESS) {
				RDA_LOG_ERROR("Failed to create sampler");
				destroy();
				return false;
			}
		}
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

		immediateSubmit([=](VkCommandBuffer cmd) {
			recordTransition(cmd, image, aspect,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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

			recordTransition(cmd, image, aspect,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
