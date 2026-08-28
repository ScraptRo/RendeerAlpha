#include <GraphicalSrc/Descriptors.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>

namespace RDA {

	// ---- DescriptorLayoutBuilder ---------------------------------------------------
	DescriptorLayoutBuilder& DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type,
	                                                             VkShaderStageFlags stages, uint32_t count) {
		VkDescriptorSetLayoutBinding layoutBinding{};
		layoutBinding.binding = binding;
		layoutBinding.descriptorType = type;
		layoutBinding.descriptorCount = count;
		layoutBinding.stageFlags = stages;
		mBindings.push_back(layoutBinding);
		return *this;
	}

	VkDescriptorSetLayout DescriptorLayoutBuilder::build() {
		VkDescriptorSetLayoutCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		createInfo.bindingCount = static_cast<uint32_t>(mBindings.size());
		createInfo.pBindings = mBindings.empty() ? nullptr : mBindings.data();

		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		if (vkCreateDescriptorSetLayout(getDevice(), &createInfo, nullptr, &layout) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create descriptor set layout");
			return VK_NULL_HANDLE;
		}
		return layout;
	}

	// ---- DescriptorAllocator -------------------------------------------------------
	bool DescriptorAllocator::init(uint32_t setsPerPool, const std::vector<VkDescriptorPoolSize>& sizes) {
		destroy();
		if (setsPerPool == 0 || sizes.empty()) return false;
		// Kept so a later block can be built to the same shape. A pool's sizes cannot be
		// changed after creation, so the only way to grow is another pool exactly like it.
		mSetsPerPool = setsPerPool;
		mSizes = sizes;
		return addPool();
	}

	bool DescriptorAllocator::addPool() {
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = mSetsPerPool;
		poolInfo.poolSizeCount = static_cast<uint32_t>(mSizes.size());
		poolInfo.pPoolSizes = mSizes.data();

		VkDescriptorPool pool = VK_NULL_HANDLE;
		if (vkCreateDescriptorPool(getDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create descriptor pool");
			return false;
		}
		mPools.push_back(pool);
		return true;
	}

	VkDescriptorSet DescriptorAllocator::allocate(VkDescriptorSetLayout layout) {
		if (mPools.empty()) {
			RDA_LOG_ERROR("Descriptor allocator used before init()");
			return VK_NULL_HANDLE;
		}

		// A set handed back earlier costs nothing to reuse, and reusing it is what keeps
		// the pools sized to how many are alive at once rather than to how many have ever
		// been asked for.
		auto returned = mRecycled.find(layout);
		if (returned != mRecycled.end() && !returned->second.empty()) {
			VkDescriptorSet set = returned->second.back();
			returned->second.pop_back();
			return set;
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = mPools.back();
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet set = VK_NULL_HANDLE;
		VkResult result = vkAllocateDescriptorSets(getDevice(), &allocInfo, &set);
		if (result == VK_SUCCESS) return set;

		// A full or fragmented pool is the expected answer once enough windows or
		// materials exist, not a failure: open another block and ask again. Anything else
		// is the device saying no, and retrying would not change that.
		if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
			RDA_LOG_ERROR("Failed to allocate descriptor set");
			return VK_NULL_HANDLE;
		}
		if (!addPool()) return VK_NULL_HANDLE;

		allocInfo.descriptorPool = mPools.back();
		if (vkAllocateDescriptorSets(getDevice(), &allocInfo, &set) != VK_SUCCESS) {
			// A fresh pool that cannot satisfy one set means the block is smaller than a
			// single set needs — a sizing mistake no amount of growing will fix.
			RDA_LOG_ERROR("Failed to allocate descriptor set from a fresh pool: the pool "
			              "block is too small for even one set of this layout");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void DescriptorAllocator::recycle(VkDescriptorSetLayout layout, VkDescriptorSet set) {
		if (set == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) return;
		mRecycled[layout].push_back(set);
	}

	void DescriptorAllocator::destroy() {
		VkDevice device = getDevice();
		if (device != VK_NULL_HANDLE) {
			// The sets inside go with the pool, so none needs freeing individually.
			for (VkDescriptorPool pool : mPools) {
				if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
			}
		}
		mPools.clear();
		mSizes.clear();
		mSetsPerPool = 0;
		// The recycled ones lived in those pools, so they are gone with them.
		mRecycled.clear();
	}

	// ---- DescriptorWriter ----------------------------------------------------------
	DescriptorWriter& DescriptorWriter::writeBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size,
	                                                VkDeviceSize offset, VkDescriptorType type) {
		VkDescriptorBufferInfo& info = mBufferInfos.emplace_back();
		info.buffer = buffer;
		info.offset = offset;
		info.range = size;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstBinding = binding;
		write.descriptorCount = 1;
		write.descriptorType = type;
		write.pBufferInfo = &info;
		mWrites.push_back(write);
		return *this;
	}

	DescriptorWriter& DescriptorWriter::writeImage(uint32_t binding, VkImageView view, VkSampler sampler,
	                                               VkImageLayout layout, VkDescriptorType type) {
		VkDescriptorImageInfo& info = mImageInfos.emplace_back();
		info.sampler = sampler;
		info.imageView = view;
		info.imageLayout = layout;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstBinding = binding;
		write.descriptorCount = 1;
		write.descriptorType = type;
		write.pImageInfo = &info;
		mWrites.push_back(write);
		return *this;
	}

	DescriptorWriter& DescriptorWriter::writeInputAttachment(uint32_t binding, VkImageView view, VkImageLayout layout) {
		return writeImage(binding, view, VK_NULL_HANDLE, layout, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
	}

	void DescriptorWriter::update(VkDescriptorSet set) {
		if (set == VK_NULL_HANDLE || mWrites.empty()) return;
		for (VkWriteDescriptorSet& write : mWrites) {
			write.dstSet = set;
		}
		vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(mWrites.size()), mWrites.data(), 0, nullptr);
	}
}
