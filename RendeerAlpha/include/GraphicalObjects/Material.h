#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/GraphicsPipeline.h>

namespace RDA {

	class Material {
	public:
		Material() = default;

		void setPipeline(const GraphicsPipeline* pipeline) { mPipeline = pipeline; }
		void setSets(uint32_t firstSet, const std::vector<VkDescriptorSet>& sets) {
			mFirstSet = firstSet;
			mSets = sets;
		}

		void bind(VkCommandBuffer cmd) const;
		void bindSets(VkCommandBuffer cmd) const;
		void bindSets(VkCommandBuffer cmd, VkPipelineLayout layout) const;

		const GraphicsPipeline* pipeline() const { return mPipeline; }
		bool isValid() const { return mPipeline && mPipeline->isValid(); }

	private:
		const GraphicsPipeline*      mPipeline = nullptr;
		std::vector<VkDescriptorSet> mSets;
		uint32_t                     mFirstSet = 0;
	};
}
