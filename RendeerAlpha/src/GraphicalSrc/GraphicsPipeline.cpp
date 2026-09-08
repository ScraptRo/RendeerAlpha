#include <GraphicalSrc/GraphicsPipeline.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>

namespace RDA {

	static VkPipelineColorBlendAttachmentState defaultBlendAttachment() {
		VkPipelineColorBlendAttachmentState state{};
		state.blendEnable = VK_FALSE;
		state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		return state;
	}

	// ---- GraphicsPipeline ----------------------------------------------------------
	GraphicsPipeline::~GraphicsPipeline() {
		destroy();
	}

	GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept {
		mPipeline = other.mPipeline;
		mLayout = other.mLayout;
		mOwnsLayout = other.mOwnsLayout;
		other.mPipeline = VK_NULL_HANDLE;
		other.mLayout = VK_NULL_HANDLE;
		other.mOwnsLayout = false;
	}

	GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept {
		if (this != &other) {
			destroy();
			mPipeline = other.mPipeline;
			mLayout = other.mLayout;
			mOwnsLayout = other.mOwnsLayout;
			other.mPipeline = VK_NULL_HANDLE;
			other.mLayout = VK_NULL_HANDLE;
			other.mOwnsLayout = false;
		}
		return *this;
	}

	void GraphicsPipeline::bind(VkCommandBuffer cmd) const {
		if (mPipeline != VK_NULL_HANDLE) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
		}
	}

	void GraphicsPipeline::destroy() {
		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE) return;

		if (mPipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(device, mPipeline, nullptr);
			mPipeline = VK_NULL_HANDLE;
		}
		if (mLayout != VK_NULL_HANDLE && mOwnsLayout) {
			vkDestroyPipelineLayout(device, mLayout, nullptr);
		}
		mLayout = VK_NULL_HANDLE;
		mOwnsLayout = false;
	}

	void setViewportAndScissor(VkCommandBuffer cmd, VkExtent2D extent) {
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);
	}

	// ---- PipelineBuilder -----------------------------------------------------------
	PipelineBuilder::PipelineBuilder() {
		mBlendAttachments.assign(1, defaultBlendAttachment());
	}

	PipelineBuilder& PipelineBuilder::addShader(const Shader& shader, const char* entryPoint) {
		if (!shader.isValid()) {
			RDA_LOG_ERROR("Adding an invalid shader to a pipeline");
			return *this;
		}
		mStages.push_back(shader.stageInfo(entryPoint));
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setVertexInput(const VkVertexInputBindingDescription& binding,
	                                                 const std::vector<VkVertexInputAttributeDescription>& attributes) {
		mBinding = binding;
		mAttributes = attributes;
		mHasVertexInput = true;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setTopology(VkPrimitiveTopology topology) {
		mTopology = topology;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setCull(VkCullModeFlags cullMode, VkFrontFace frontFace) {
		mCullMode = cullMode;
		mFrontFace = frontFace;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setDepth(bool test, bool write, VkCompareOp op) {
		mDepthTest = test;
		mDepthWrite = write;
		mDepthOp = op;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setColorAttachmentCount(uint32_t count) {
		mBlendAttachments.assign(count, defaultBlendAttachment());
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setBlendAttachment(uint32_t index,
	                                                     const VkPipelineColorBlendAttachmentState& state) {
		if (index >= mBlendAttachments.size()) {
			RDA_LOG_ERROR("Blend attachment index " << index << " is out of range; call setColorAttachmentCount first");
			return *this;
		}
		mBlendAttachments[index] = state;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setTarget(VkRenderPass renderPass, uint32_t subpass) {
		mRenderPass = renderPass;
		mSubpass = subpass;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::addDescriptorSetLayout(VkDescriptorSetLayout setLayout) {
		mSetLayouts.push_back(setLayout);
		return *this;
	}

	PipelineBuilder& PipelineBuilder::addPushConstantRange(const VkPushConstantRange& range) {
		mPushRanges.push_back(range);
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setLayout(VkPipelineLayout existingLayout) {
		mExternalLayout = existingLayout;
		return *this;
	}

	GraphicsPipeline PipelineBuilder::build() {
		GraphicsPipeline result;

		if (mStages.empty()) {
			RDA_LOG_ERROR("Cannot build a pipeline with no shader stages");
			return result;
		}
		if (mRenderPass == VK_NULL_HANDLE) {
			RDA_LOG_ERROR("Cannot build a pipeline without a target render pass");
			return result;
		}

		VkDevice device = getDevice();

		// Layout: reuse the caller's, or create one we own from the set layouts.
		VkPipelineLayout layout = mExternalLayout;
		bool ownsLayout = false;
		if (layout == VK_NULL_HANDLE) {
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount = static_cast<uint32_t>(mSetLayouts.size());
			layoutInfo.pSetLayouts = mSetLayouts.empty() ? nullptr : mSetLayouts.data();
			layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(mPushRanges.size());
			layoutInfo.pPushConstantRanges = mPushRanges.empty() ? nullptr : mPushRanges.data();
			if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
				RDA_LOG_ERROR("Failed to create pipeline layout");
				return result;
			}
			ownsLayout = true;
		}

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		if (mHasVertexInput) {
			vertexInput.vertexBindingDescriptionCount = 1;
			vertexInput.pVertexBindingDescriptions = &mBinding;
			vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(mAttributes.size());
			vertexInput.pVertexAttributeDescriptions = mAttributes.data();
		}

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = mTopology;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// Viewport and scissor are dynamic; counts still have to be declared here.
		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo raster{};
		raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		raster.depthClampEnable = VK_FALSE;
		raster.rasterizerDiscardEnable = VK_FALSE;
		raster.polygonMode = mPolygonMode;
		raster.lineWidth = 1.0f;
		raster.cullMode = mCullMode;
		raster.frontFace = mFrontFace;
		raster.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisample{};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.sampleShadingEnable = VK_FALSE;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = mDepthTest ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = mDepthWrite ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = mDepthOp;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlend{};
		colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlend.logicOpEnable = VK_FALSE;
		colorBlend.attachmentCount = static_cast<uint32_t>(mBlendAttachments.size());
		colorBlend.pAttachments = mBlendAttachments.data();

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkGraphicsPipelineCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		createInfo.stageCount = static_cast<uint32_t>(mStages.size());
		createInfo.pStages = mStages.data();
		createInfo.pVertexInputState = &vertexInput;
		createInfo.pInputAssemblyState = &inputAssembly;
		createInfo.pViewportState = &viewportState;
		createInfo.pRasterizationState = &raster;
		createInfo.pMultisampleState = &multisample;
		createInfo.pDepthStencilState = &depthStencil;
		createInfo.pColorBlendState = &colorBlend;
		createInfo.pDynamicState = &dynamicState;
		createInfo.layout = layout;
		createInfo.renderPass = mRenderPass;
		createInfo.subpass = mSubpass;
		createInfo.basePipelineHandle = VK_NULL_HANDLE;

		VkPipeline pipeline = VK_NULL_HANDLE;
		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create graphics pipeline");
			if (ownsLayout) {
				vkDestroyPipelineLayout(device, layout, nullptr);
			}
			return result;
		}

		result.mPipeline = pipeline;
		result.mLayout = layout;
		result.mOwnsLayout = ownsLayout;
		return result;
	}
}
