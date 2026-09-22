#include <GraphicalSrc/MemoryBuffer.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
#include <vendor/vma/vma.h>
#include <cstring>

namespace RDA {

	// One-shot buffer copy on the graphics queue, using a transient command pool.
	// Uploads are infrequent, so creating a short-lived pool here keeps callers simple.
	static void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
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

		VkBufferCopy region{};
		region.size = size;
		vkCmdCopyBuffer(cmd, src, dst, 1, &region);

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

	MemoryBuffer::~MemoryBuffer() {
		destroy();
	}

	MemoryBuffer::MemoryBuffer(MemoryBuffer&& other) noexcept {
		mBuffer = other.mBuffer;
		mAllocation = other.mAllocation;
		mSize = other.mSize;
		mResidence = other.mResidence;
		mMapped = other.mMapped;
		other.mBuffer = VK_NULL_HANDLE;
		other.mAllocation = nullptr;
		other.mSize = 0;
		other.mMapped = nullptr;
	}

	MemoryBuffer& MemoryBuffer::operator=(MemoryBuffer&& other) noexcept {
		if (this != &other) {
			destroy();
			mBuffer = other.mBuffer;
			mAllocation = other.mAllocation;
			mSize = other.mSize;
			mResidence = other.mResidence;
			mMapped = other.mMapped;
			other.mBuffer = VK_NULL_HANDLE;
			other.mAllocation = nullptr;
			other.mSize = 0;
			other.mMapped = nullptr;
		}
		return *this;
	}

	bool MemoryBuffer::create(VkDeviceSize size, VkBufferUsageFlags usage, MemoryResidence residence) {
		destroy();
		mSize = size;
		mResidence = residence;

		// GpuOnly buffers are filled via a staging copy, so they always need TRANSFER_DST.
		if (residence == MemoryResidence::GpuOnly) {
			usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		}

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		if (residence != MemoryResidence::GpuOnly) {
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		}

		if (vmaCreateBuffer(getAllocator(), &bufferInfo, &allocInfo, &mBuffer, &mAllocation, nullptr) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to allocate buffer");
			mBuffer = VK_NULL_HANDLE;
			mAllocation = nullptr;
			return false;
		}
		return true;
	}

	bool MemoryBuffer::upload(const void* data, VkDeviceSize size, VkDeviceSize offset) {
		if (!isValid() || size == 0) return false;

		if (mResidence != MemoryResidence::GpuOnly) {
			void* dst = nullptr;
			if (vmaMapMemory(getAllocator(), mAllocation, &dst) != VK_SUCCESS) {
				RDA_LOG_ERROR("Failed to map buffer for upload");
				return false;
			}
			std::memcpy(static_cast<char*>(dst) + offset, data, static_cast<size_t>(size));
			vmaUnmapMemory(getAllocator(), mAllocation);
			return true;
		}

		// GpuOnly: stage through a host-visible buffer, then copy on the GPU.
		MemoryBuffer staging;
		if (!staging.create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryResidence::CpuToGpu)) {
			return false;
		}
		if (!staging.upload(data, size, 0)) {
			return false;
		}
		copyBuffer(staging.handle(), mBuffer, size);
		return true;
	}

	void* MemoryBuffer::map() {
		if (mResidence == MemoryResidence::GpuOnly || !isValid()) return nullptr;
		if (!mMapped) {
			vmaMapMemory(getAllocator(), mAllocation, &mMapped);
		}
		return mMapped;
	}

	void MemoryBuffer::unmap() {
		if (mMapped) {
			vmaUnmapMemory(getAllocator(), mAllocation);
			mMapped = nullptr;
		}
	}

	void MemoryBuffer::destroy() {
		// The allocator is gone once the engine has shut down (rendeerClose), which
		// already released every allocation with the device. Buffers that outlive it
		// — e.g. locals in main declared before rendeerClose() — must not touch it.
		VmaAllocator allocator = getAllocator();
		if (allocator == nullptr) {
			mMapped = nullptr;
			mBuffer = VK_NULL_HANDLE;
			mAllocation = nullptr;
			mSize = 0;
			return;
		}

		if (mMapped) {
			vmaUnmapMemory(allocator, mAllocation);
			mMapped = nullptr;
		}
		if (mBuffer != VK_NULL_HANDLE) {
			vmaDestroyBuffer(allocator, mBuffer, mAllocation);
			mBuffer = VK_NULL_HANDLE;
			mAllocation = nullptr;
		}
		mSize = 0;
	}
}
