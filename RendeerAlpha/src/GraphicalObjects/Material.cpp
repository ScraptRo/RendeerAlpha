#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalObjects/Material.h>

namespace RDA {

	void Material::bind(VkCommandBuffer cmd) const {
		if (!isValid()) return;
		mPipeline->bind(cmd);
		bindSets(cmd);
	}

	void Material::bindSets(VkCommandBuffer cmd) const {
		if (!isValid()) return;
		bindSets(cmd, mPipeline->layout());
	}

	void Material::bindSets(VkCommandBuffer cmd, VkPipelineLayout layout) const {
		if (mSets.empty() || layout == VK_NULL_HANDLE) return;
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
		                        mFirstSet, static_cast<uint32_t>(mSets.size()), mSets.data(),
		                        0, nullptr);
	}
}
