#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <deque>
#include <unordered_map>
#include <vector>

namespace RDA {

	class DescriptorLayoutBuilder {
	public:
		DescriptorLayoutBuilder& addBinding(uint32_t binding, VkDescriptorType type,
		                                    VkShaderStageFlags stages, uint32_t count = 1);
		VkDescriptorSetLayout build();

	private:
		std::vector<VkDescriptorSetLayoutBinding> mBindings;
	};

	// Hands out descriptor sets, opening another pool whenever the current one runs out.
	//
	// It used to hold exactly one pool sized by its caller, which made every `maxSets`
	// argument a guess that had to be right for ever. Some of them were not: the
	// renderer's sized itself for one window's frames, so the second window to draw got
	// VK_NULL_HANDLE back — and a null set is not a crash, it is a bound-nothing draw that
	// shows up as a blank window or a validation error a long way from the cause.
	//
	// Growing removes the guess. `setsPerPool` is now a block size rather than a ceiling:
	// too small only costs another pool, and running out is no longer possible while the
	// device has memory.
	//
	// Sets handed back through recycle() are reused rather than returned to the driver, so
	// what the pools have to cover is the *peak* number of sets alive at once, not the
	// total ever asked for. Without that, a runtime whose applications come and go over an
	// afternoon opens a new pool block every few windows and never closes one — every
	// window that ever existed still holding its share.
	//
	// Reuse rather than vkFreeDescriptorSets because a recycled set needs no
	// VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT (which invites pool fragmentation),
	// needs no map from set back to the pool it came from, and is cheaper than freeing and
	// allocating again. The caller rewrites its bindings before use either way.
	class DescriptorAllocator {
	public:
		bool init(uint32_t setsPerPool, const std::vector<VkDescriptorPoolSize>& sizes);
		// Null only if the device itself refuses; a full pool is answered with a new one.
		VkDescriptorSet allocate(VkDescriptorSetLayout layout);
		// Hands a set back for the next allocate() of the same layout.
		//
		// It must not be in use by the GPU: this is the same rule as freeing one, and the
		// callers meet it the same way — by waiting before they release a window's frames.
		// The set is not reset here, because every caller writes all of its bindings before
		// binding it.
		void recycle(VkDescriptorSetLayout layout, VkDescriptorSet set);

		void destroy();
		bool isValid() const { return !mPools.empty(); }

	private:
		bool addPool();

		std::vector<VkDescriptorPool>     mPools;   // the last one is the one being filled
		std::vector<VkDescriptorPoolSize> mSizes;   // kept so another block can be built
		uint32_t                          mSetsPerPool = 0;
		// Returned sets, by the layout they were built for. Keyed by layout because a set
		// can only be reused for the shape it was allocated with.
		std::unordered_map<VkDescriptorSetLayout, std::vector<VkDescriptorSet>> mRecycled;
	};

	class DescriptorWriter {
	public:
		DescriptorWriter& writeBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size,
		                              VkDeviceSize offset, VkDescriptorType type);
		DescriptorWriter& writeImage(uint32_t binding, VkImageView view, VkSampler sampler,
		                             VkImageLayout layout, VkDescriptorType type);

		void update(VkDescriptorSet set);

	private:
		std::deque<VkDescriptorBufferInfo> mBufferInfos;
		std::deque<VkDescriptorImageInfo>  mImageInfos;
		std::vector<VkWriteDescriptorSet>  mWrites;
	};
}
