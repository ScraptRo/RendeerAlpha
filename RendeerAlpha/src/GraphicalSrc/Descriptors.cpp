#define VK_USE_PLATFORM_WIN32_KHR
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
	bool DescriptorAllocator::init(uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& sizes) {
		destroy();
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = maxSets;
		poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
		poolInfo.pPoolSizes = sizes.data();

		if (vkCreateDescriptorPool(getDevice(), &poolInfo, nullptr, &mPool) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create descriptor pool");
			mPool = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	VkDescriptorSet DescriptorAllocator::allocate(VkDescriptorSetLayout layout) {
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = mPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet set = VK_NULL_HANDLE;
		if (vkAllocateDescriptorSets(getDevice(), &allocInfo, &set) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to allocate descriptor set (pool exhausted?)");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void DescriptorAllocator::destroy() {
		if (mPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(getDevice(), mPool, nullptr);
			mPool = VK_NULL_HANDLE;
		}
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
