#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalSrc/GuiRenderer.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/Shader.h>
#include <GraphicalObjects/Texture.h>
#include <Logger/Logger.h>
#include <algorithm>

namespace RDA {

	struct GuiPush {
		glm::vec2 scale;
		glm::vec2 translate;
	};

	static const char* kGuiVertexSource = R"GLSL(
		#version 450
		layout(location = 0) in vec2 inPos;
		layout(location = 1) in vec2 inUV;
		layout(location = 2) in vec4 inColor;
		layout(push_constant) uniform Push { vec2 scale; vec2 translate; } pc;
		layout(location = 0) out vec2 vUV;
		layout(location = 1) out vec4 vColor;
		void main() {
			gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
			vUV = inUV;
			vColor = inColor;
		}
	)GLSL";

	// Font/coverage: the atlas is single-channel, used as alpha (solid quads sample the
	// white texel).
	static const char* kAtlasFragmentSource = R"GLSL(
		#version 450
		layout(location = 0) in vec2 vUV;
		layout(location = 1) in vec4 vColor;
		layout(set = 0, binding = 0) uniform sampler2D atlas;
		layout(location = 0) out vec4 outColor;
		void main() {
			outColor = vec4(vColor.rgb, vColor.a * texture(atlas, vUV).r);
		}
	)GLSL";

	// Image/color: sample an RGBA texture directly, tinted by the vertex color.
	static const char* kImageFragmentSource = R"GLSL(
		#version 450
		layout(location = 0) in vec2 vUV;
		layout(location = 1) in vec4 vColor;
		layout(set = 0, binding = 0) uniform sampler2D image;
		layout(location = 0) out vec4 outColor;
		void main() {
			outColor = texture(image, vUV) * vColor;
		}
	)GLSL";

	static VkDeviceSize roundUpCapacity(VkDeviceSize needed) {
		VkDeviceSize cap = 4096;
		while (cap < needed) cap *= 2;
		return cap;
	}

	bool GuiRenderer::init(VkRenderPass targetRenderPass, const std::string& fontPath,
	                       float fontHeight, uint32_t framesInFlight) {
		if (!mFont.bake(fontPath, fontHeight)) {
			RDA_LOG_ERROR("GUI: failed to bake font atlas");
			return false;
		}

		mSetLayout = DescriptorLayoutBuilder()
			.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build();
		if (mSetLayout == VK_NULL_HANDLE) return false;

		if (!mPool.init(1, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } })) return false;
		mAtlasSet = mPool.allocate(mSetLayout);
		if (mAtlasSet == VK_NULL_HANDLE) return false;
		DescriptorWriter()
			.writeImage(0, mFont.texture().view(), mFont.texture().sampler(),
			            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.update(mAtlasSet);

		if (!buildPipelineSet(targetRenderPass, mAtlasPipeline, mImagePipeline, &mCompositePipeline)) {
			return false;
		}

		mFrames.resize(framesInFlight);

		// A small per-frame pool for transient image-texture descriptor sets.
		VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 };
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 8;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &size;
		mImagePools.resize(framesInFlight, VK_NULL_HANDLE);
		for (uint32_t i = 0; i < framesInFlight; ++i) {
			if (vkCreateDescriptorPool(getDevice(), &poolInfo, nullptr, &mImagePools[i]) != VK_SUCCESS) {
				RDA_LOG_ERROR("GUI: failed to create image descriptor pool");
				return false;
			}
		}

		RDA_LOG_SUCCES("GUI renderer initialized");
		return true;
	}

	bool GuiRenderer::buildLayerPipelines(VkRenderPass layerPass) {
		if (mLayerPipelinesReady) return true;
		if (!buildPipelineSet(layerPass, mLayerAtlasPipeline, mLayerImagePipeline, nullptr)) {
			return false;
		}
		mLayerPipelinesReady = true;
		return true;
	}

	bool GuiRenderer::buildPipelineSet(VkRenderPass renderPass, GraphicsPipeline& atlasOut,
	                                   GraphicsPipeline& imageOut, GraphicsPipeline* compositeOut) {
		Shader vertex = Shader::fromSource(kGuiVertexSource, ShaderStage::Vertex, "gui.vert");
		Shader atlasFrag = Shader::fromSource(kAtlasFragmentSource, ShaderStage::Fragment, "gui_atlas.frag");
		Shader imageFrag = Shader::fromSource(kImageFragmentSource, ShaderStage::Fragment, "gui_image.frag");
		if (!vertex.isValid() || !atlasFrag.isValid() || !imageFrag.isValid()) {
			RDA_LOG_ERROR("GUI: failed to build shaders");
			return false;
		}

		VkVertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(GuiVertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		std::vector<VkVertexInputAttributeDescription> attributes(3);
		attributes[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GuiVertex, pos) };
		attributes[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GuiVertex, uv) };
		attributes[2] = { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GuiVertex, color) };

		VkPipelineColorBlendAttachmentState blend{};
		blend.blendEnable = VK_TRUE;
		blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.colorBlendOp = VK_BLEND_OP_ADD;
		blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.alphaBlendOp = VK_BLEND_OP_ADD;
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPushConstantRange push{};
		push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		push.offset = 0;
		push.size = sizeof(GuiPush);

		auto build = [&](const Shader& frag) {
			return PipelineBuilder()
				.addShader(vertex)
				.addShader(frag)
				.setVertexInput(binding, attributes)
				.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
				.setCull(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
				.setDepth(false, false)
				.setColorAttachmentCount(1)
				.setBlendAttachment(0, blend)
				.addDescriptorSetLayout(mSetLayout)
				.addPushConstantRange(push)
				.setTarget(renderPass, 0)
				.build();
		};

		atlasOut = build(atlasFrag);
		imageOut = build(imageFrag);
		if (!atlasOut.isValid() || !imageOut.isValid()) {
			RDA_LOG_ERROR("GUI: failed to build pipelines");
			return false;
		}

		if (compositeOut) {
			// Same shader as the image pipeline, premultiplied blending — for compositing
			// a GUI layer that was rendered onto a transparent target.
			blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			*compositeOut = build(imageFrag);
			if (!compositeOut->isValid()) {
				RDA_LOG_ERROR("GUI: failed to build the composite pipeline");
				return false;
			}
		}
		return true;
	}

	VkDescriptorSet GuiRenderer::imageSetFor(const Texture* texture, uint32_t frameIndex) {
		auto it = mImageSets.find(texture);
		if (it != mImageSets.end()) return it->second;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = mImagePools[frameIndex];
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mSetLayout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		if (vkAllocateDescriptorSets(getDevice(), &allocInfo, &set) != VK_SUCCESS || !texture->isValid()) {
			return VK_NULL_HANDLE;
		}
		DescriptorWriter()
			.writeImage(0, texture->view(), texture->sampler(),
			            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.update(set);
		mImageSets[texture] = set;
		return set;
	}

	void GuiRenderer::beginFrame(uint32_t frameIndex) {
		if (frameIndex >= mImagePools.size()) return;
		// Recycle last use of this frame slot's transient sets. Done once per frame
		// because a frame can record several times (GUI layer, then composite).
		vkResetDescriptorPool(getDevice(), mImagePools[frameIndex], 0);
		mImageSets.clear();
	}

	void GuiRenderer::record(VkCommandBuffer cmd, const GuiDrawData& data,
	                         VkExtent2D targetExtent, uint32_t frameIndex, uint64_t drawVersion,
	                         const Texture* skipTexture, bool intoLayer) {
		// A pipeline is only valid with the render pass it was built against.
		const GraphicsPipeline& atlasPipeline = intoLayer ? mLayerAtlasPipeline : mAtlasPipeline;
		const GraphicsPipeline& imagePipeline = intoLayer ? mLayerImagePipeline : mImagePipeline;
		if (!atlasPipeline.isValid() || !imagePipeline.isValid()) return;
		if (data.commands.empty() || data.vertices.empty() || data.indices.empty()) return;
		if (frameIndex >= mFrames.size()) return;

		VkDeviceSize vertexSize = data.vertices.size() * sizeof(GuiVertex);
		VkDeviceSize indexSize = data.indices.size() * sizeof(uint16_t);
		DynamicBuffers& fb = mFrames[frameIndex];
		bool reallocated = false;
		if (vertexSize > fb.vertexCapacity) {
			fb.vertexCapacity = roundUpCapacity(vertexSize);
			fb.vertices.create(fb.vertexCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, MemoryResidence::CpuToGpu);
			reallocated = true;
		}
		if (indexSize > fb.indexCapacity) {
			fb.indexCapacity = roundUpCapacity(indexSize);
			fb.indices.create(fb.indexCapacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, MemoryResidence::CpuToGpu);
			reallocated = true;
		}

		// Skip the copy when this frame's buffers already hold exactly this geometry —
		// the common case for a UI that is sitting still.
		if (reallocated || !fb.hasUpload || fb.uploadedVersion != drawVersion) {
			fb.vertices.upload(data.vertices.data(), vertexSize);
			fb.indices.upload(data.indices.data(), indexSize);
			fb.uploadedVersion = drawVersion;
			fb.hasUpload = true;
		}

		GuiPush push{};
		push.scale = { 2.0f / targetExtent.width, 2.0f / targetExtent.height };
		push.translate = { -1.0f, -1.0f };

		VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(targetExtent.width), static_cast<float>(targetExtent.height), 0.0f, 1.0f };
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkBuffer vertexBuffer = fb.vertices.handle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, fb.indices.handle(), 0, VK_INDEX_TYPE_UINT16);

		// Push constants once — both pipeline layouts are identical/compatible.
		vkCmdPushConstants(cmd, atlasPipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

		VkPipeline bound = VK_NULL_HANDLE;
		for (const GuiDrawCmd& command : data.commands) {
			if (skipTexture && command.texture == skipTexture) continue;
			bool isImage = command.texture != nullptr;
			VkPipeline wantPipeline = isImage ? imagePipeline.handle() : atlasPipeline.handle();
			VkPipelineLayout layout = isImage ? imagePipeline.layout() : atlasPipeline.layout();
			VkDescriptorSet set = isImage ? imageSetFor(command.texture, frameIndex) : mAtlasSet;
			if (set == VK_NULL_HANDLE) continue;

			if (wantPipeline != bound) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wantPipeline);
				bound = wantPipeline;
			}
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);

			float x0 = (std::max)(command.clip.x, 0.0f);
			float y0 = (std::max)(command.clip.y, 0.0f);
			float x1 = (std::min)(command.clip.z, static_cast<float>(targetExtent.width));
			float y1 = (std::min)(command.clip.w, static_cast<float>(targetExtent.height));
			if (x1 <= x0 || y1 <= y0) continue;
			VkRect2D scissor{};
			scissor.offset = { static_cast<int32_t>(x0), static_cast<int32_t>(y0) };
			scissor.extent = { static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0) };
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			vkCmdDrawIndexed(cmd, command.indexCount, 1, command.indexOffset, 0, 0);
		}
	}

	void GuiRenderer::recordComposite(VkCommandBuffer cmd, VkExtent2D targetExtent, uint32_t frameIndex,
	                                  const Texture* sceneTexture, const Rect& sceneRect,
	                                  const Texture* layerTexture) {
		if (frameIndex >= mFrames.size() || !layerTexture) return;
		DynamicBuffers& fb = mFrames[frameIndex];

		// Quad 0: the live scene, in the Viewport widget's rect. Quad 1: the cached GUI
		// layer over the whole target.
		const uint32_t white = rgba(255, 255, 255);
		GuiVertex vertices[8];
		auto quad = [&](int base, const Rect& r) {
			vertices[base + 0] = { { r.x,       r.y       }, { 0.0f, 0.0f }, white };
			vertices[base + 1] = { { r.x + r.w, r.y       }, { 1.0f, 0.0f }, white };
			vertices[base + 2] = { { r.x + r.w, r.y + r.h }, { 1.0f, 1.0f }, white };
			vertices[base + 3] = { { r.x,       r.y + r.h }, { 0.0f, 1.0f }, white };
		};
		quad(0, sceneRect);
		quad(4, { 0.0f, 0.0f, static_cast<float>(targetExtent.width), static_cast<float>(targetExtent.height) });
		const uint16_t indices[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };

		if (!fb.compositeCreated) {
			fb.compositeVertices.create(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, MemoryResidence::CpuToGpu);
			fb.compositeIndices.create(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, MemoryResidence::CpuToGpu);
			fb.compositeIndices.upload(indices, sizeof(indices)); // never changes
			fb.compositeCreated = true;
		}
		fb.compositeVertices.upload(vertices, sizeof(vertices));

		GuiPush push{};
		push.scale = { 2.0f / targetExtent.width, 2.0f / targetExtent.height };
		push.translate = { -1.0f, -1.0f };

		VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(targetExtent.width),
		                     static_cast<float>(targetExtent.height), 0.0f, 1.0f };
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor{ { 0, 0 }, targetExtent };
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		VkBuffer vertexBuffer = fb.compositeVertices.handle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, fb.compositeIndices.handle(), 0, VK_INDEX_TYPE_UINT16);
		vkCmdPushConstants(cmd, mImagePipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

		// The scene first, then the GUI layer over it — so anything the GUI draws on top
		// of the viewport (a floating container, say) still covers the scene.
		if (sceneTexture && sceneRect.w > 0.0f && sceneRect.h > 0.0f) {
			if (VkDescriptorSet set = imageSetFor(sceneTexture, frameIndex)) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mImagePipeline.handle());
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mImagePipeline.layout(),
				                        0, 1, &set, 0, nullptr);
				vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
			}
		}
		if (VkDescriptorSet set = imageSetFor(layerTexture, frameIndex)) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCompositePipeline.handle());
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCompositePipeline.layout(),
			                        0, 1, &set, 0, nullptr);
			vkCmdDrawIndexed(cmd, 6, 1, 6, 0, 0);
		}
	}

	void GuiRenderer::destroy() {
		VkDevice device = getDevice();

		mAtlasPipeline.destroy();
		mImagePipeline.destroy();
		mCompositePipeline.destroy();
		mLayerAtlasPipeline.destroy();
		mLayerImagePipeline.destroy();
		mLayerPipelinesReady = false;
		mFrames.clear();

		for (VkDescriptorPool pool : mImagePools) {
			if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
		}
		mImagePools.clear();
		mImageSets.clear();

		mPool.destroy();
		if (mSetLayout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, mSetLayout, nullptr);
			mSetLayout = VK_NULL_HANDLE;
		}
		mFont.destroy();
	}
}
