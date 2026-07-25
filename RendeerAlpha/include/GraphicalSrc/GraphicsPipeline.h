#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/Shader.h>

namespace RDA {

	class PipelineBuilder;

	class GraphicsPipeline {
	public:
		GraphicsPipeline() = default;
		~GraphicsPipeline();

		GraphicsPipeline(const GraphicsPipeline&) = delete;
		GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
		GraphicsPipeline(GraphicsPipeline&& other) noexcept;
		GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;

		void bind(VkCommandBuffer cmd) const;

		VkPipeline       handle() const { return mPipeline; }
		VkPipelineLayout layout() const { return mLayout; }
		bool             isValid() const { return mPipeline != VK_NULL_HANDLE; }

		void destroy();

	private:
		friend class PipelineBuilder;
		VkPipeline       mPipeline = VK_NULL_HANDLE;
		VkPipelineLayout mLayout = VK_NULL_HANDLE;
		bool             mOwnsLayout = false; // false when the layout was supplied by the caller
	};

	void setViewportAndScissor(VkCommandBuffer cmd, VkExtent2D extent);

	class PipelineBuilder {
	public:
		PipelineBuilder();

		PipelineBuilder& addShader(const Shader& shader, const char* entryPoint = "main");

		template<typename V>
		PipelineBuilder& setVertexType();
		PipelineBuilder& setVertexInput(const VkVertexInputBindingDescription& binding,
		                                const std::vector<VkVertexInputAttributeDescription>& attributes);
		PipelineBuilder& noVertexInput();

		PipelineBuilder& setTopology(VkPrimitiveTopology topology);
		PipelineBuilder& setPolygonMode(VkPolygonMode mode);
		PipelineBuilder& setCull(VkCullModeFlags cullMode, VkFrontFace frontFace);
		PipelineBuilder& setDepth(bool test, bool write, VkCompareOp op = VK_COMPARE_OP_LESS);

		PipelineBuilder& setColorAttachmentCount(uint32_t count);
		PipelineBuilder& setBlendAttachment(uint32_t index, const VkPipelineColorBlendAttachmentState& state);

		PipelineBuilder& setTarget(VkRenderPass renderPass, uint32_t subpass = 0);

		PipelineBuilder& addDescriptorSetLayout(VkDescriptorSetLayout setLayout);
		PipelineBuilder& addPushConstantRange(const VkPushConstantRange& range);
		PipelineBuilder& setLayout(VkPipelineLayout existingLayout);

		GraphicsPipeline build();

	private:
		std::vector<VkPipelineShaderStageCreateInfo> mStages;

		VkVertexInputBindingDescription mBinding{};
		std::vector<VkVertexInputAttributeDescription> mAttributes;
		bool mHasVertexInput = false;

		VkPrimitiveTopology mTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkPolygonMode       mPolygonMode = VK_POLYGON_MODE_FILL;
		VkCullModeFlags     mCullMode = VK_CULL_MODE_BACK_BIT;
		VkFrontFace         mFrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

		bool        mDepthTest = false;
		bool        mDepthWrite = false;
		VkCompareOp mDepthOp = VK_COMPARE_OP_LESS;

		std::vector<VkPipelineColorBlendAttachmentState> mBlendAttachments;

		VkRenderPass mRenderPass = VK_NULL_HANDLE;
		uint32_t     mSubpass = 0;

		std::vector<VkDescriptorSetLayout> mSetLayouts;
		std::vector<VkPushConstantRange>   mPushRanges;
		VkPipelineLayout                   mExternalLayout = VK_NULL_HANDLE;
	};

	template<typename V>
	PipelineBuilder& PipelineBuilder::setVertexType() {
		mBinding = V::getBindingDescription();
		auto attributes = V::getAttributeDescriptions();
		mAttributes.assign(attributes.begin(), attributes.end());
		mHasVertexInput = true;
		return *this;
	}
}
