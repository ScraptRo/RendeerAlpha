#pragma once
#include <Core/Datatypes.h>

typedef struct VmaAllocator_T*  VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace RDA{
	enum class MemoryResidence {
		GpuOnly,   // device-local; fastest for the GPU, uploaded through a staging buffer
		CpuToGpu,  // host-visible & mappable; good for uniforms / staging
		// The same memory, named for the other direction: what the GPU wrote and the CPU
		// is about to read. VMA picks cached memory for it, which matters -- reading
		// uncached, write-combined memory a byte at a time is slower than the copy that
		// filled it.
		GpuToCpu,
	};

	class MemoryBuffer
	{
	public:
		MemoryBuffer() = default;
		~MemoryBuffer();

		MemoryBuffer(const MemoryBuffer&) = delete;
		MemoryBuffer& operator=(const MemoryBuffer&) = delete;
		MemoryBuffer(MemoryBuffer&& other) noexcept;
		MemoryBuffer& operator=(MemoryBuffer&& other) noexcept;

		bool create(VkDeviceSize size, VkBufferUsageFlags usage, MemoryResidence residence);
		bool upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

		void* map();
		void  unmap();
		void  destroy();

		VkBuffer     handle() const { return mBuffer; }
		VkDeviceSize size()   const { return mSize; }
		bool         isValid() const { return mBuffer != VK_NULL_HANDLE; }

	private:
		VkBuffer        mBuffer = VK_NULL_HANDLE;
		VmaAllocation   mAllocation = nullptr;
		VkDeviceSize    mSize = 0;
		MemoryResidence mResidence = MemoryResidence::GpuOnly;
		void*           mMapped = nullptr;
	};
}
