#pragma once
#include <Core/Datatypes.h>
#include <deque>

namespace RDA {

	class DescriptorLayoutBuilder {
	public:
		DescriptorLayoutBuilder& addBinding(uint32_t binding, VkDescriptorType type,
		                                    VkShaderStageFlags stages, uint32_t count = 1);
		VkDescriptorSetLayout build();

	private:
		std::vector<VkDescriptorSetLayoutBinding> mBindings;
	};

	class DescriptorAllocator {
	public:
		bool init(uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& sizes);
		VkDescriptorSet allocate(VkDescriptorSetLayout layout);
		void destroy();
		bool isValid() const { return mPool != VK_NULL_HANDLE; }

	private:
		VkDescriptorPool mPool = VK_NULL_HANDLE;
	};

	class DescriptorWriter {
	public:
		DescriptorWriter& writeBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size,
		                              VkDeviceSize offset, VkDescriptorType type);
		DescriptorWriter& writeImage(uint32_t binding, VkImageView view, VkSampler sampler,
		                             VkImageLayout layout, VkDescriptorType type);
		DescriptorWriter& writeInputAttachment(uint32_t binding, VkImageView view, VkImageLayout layout);

		void update(VkDescriptorSet set);

	private:
		std::deque<VkDescriptorBufferInfo> mBufferInfos;
		std::deque<VkDescriptorImageInfo>  mImageInfos;
		std::vector<VkWriteDescriptorSet>  mWrites;
	};
}
