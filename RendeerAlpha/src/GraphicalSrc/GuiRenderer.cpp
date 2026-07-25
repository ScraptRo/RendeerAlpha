#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalSrc/GuiRenderer.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/Shader.h>
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

		layout(push_constant) uniform Push {
			vec2 scale;
			vec2 translate;
		} pc;

		layout(location = 0) out vec2 vUV;
		layout(location = 1) out vec4 vColor;

		void main() {
			gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
			vUV = inUV;
			vColor = inColor;
		}
	)GLSL";

	static const char* kGuiFragmentSource = R"GLSL(
		#version 450
		layout(location = 0) in vec2 vUV;
		layout(location = 1) in vec4 vColor;

		layout(set = 0, binding = 0) uniform sampler2D atlas;

		layout(location = 0) out vec4 outColor;

		void main() {
			// The atlas is single-channel coverage: 1.0 on the white texel (solid quads),
			// glyph coverage for text. Modulate the vertex color's alpha by it.
			float coverage = texture(atlas, vUV).r;
			outColor = vec4(vColor.rgb, vColor.a * coverage);
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

		if (!buildPipeline(targetRenderPass)) return false;

		mFrames.resize(framesInFlight);
		RDA_LOG_SUCCES("GUI renderer initialized");
		return true;
	}

	bool GuiRenderer::buildPipeline(VkRenderPass renderPass) {
		Shader vertex = Shader::fromSource(kGuiVertexSource, ShaderStage::Vertex, "gui.vert");
		Shader fragment = Shader::fromSource(kGuiFragmentSource, ShaderStage::Fragment, "gui.frag");
		if (!vertex.isValid() || !fragment.isValid()) {
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

		mPipeline = PipelineBuilder()
			.addShader(vertex)
			.addShader(fragment)
			.setVertexInput(binding, attributes)
			.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.setCull(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
			.setDepth(false, false) // UI overlays the scene; it never touches depth
			.setColorAttachmentCount(1)
			.setBlendAttachment(0, blend)
			.addDescriptorSetLayout(mSetLayout)
			.addPushConstantRange(push)
			.setTarget(renderPass, 0)
			.build();

		if (!mPipeline.isValid()) {
			RDA_LOG_ERROR("GUI: failed to build pipeline");
			return false;
		}
		return true;
	}

	void GuiRenderer::record(VkCommandBuffer cmd, const GuiDrawData& data,
	                         VkExtent2D targetExtent, uint32_t frameIndex) {
		if (data.commands.empty() || data.vertices.empty() || data.indices.empty()) return;
		if (frameIndex >= mFrames.size()) return;

		VkDeviceSize vertexSize = data.vertices.size() * sizeof(GuiVertex);
		VkDeviceSize indexSize = data.indices.size() * sizeof(uint16_t);

		DynamicBuffers& fb = mFrames[frameIndex];
		if (vertexSize > fb.vertexCapacity) {
			fb.vertexCapacity = roundUpCapacity(vertexSize);
			fb.vertices.create(fb.vertexCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, MemoryResidence::CpuToGpu);
		}
		if (indexSize > fb.indexCapacity) {
			fb.indexCapacity = roundUpCapacity(indexSize);
			fb.indices.create(fb.indexCapacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, MemoryResidence::CpuToGpu);
		}
		fb.vertices.upload(data.vertices.data(), vertexSize);
		fb.indices.upload(data.indices.data(), indexSize);

		mPipeline.bind(cmd);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.layout(),
			0, 1, &mAtlasSet, 0, nullptr);

		GuiPush push{};
		push.scale = { 2.0f / targetExtent.width, 2.0f / targetExtent.height };
		push.translate = { -1.0f, -1.0f };
		vkCmdPushConstants(cmd, mPipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

		// The scene left a full-frame viewport set; restate it so the UI is independent
		// of that, then clip per command with the scissor.
		VkViewport viewport{ 0.0f, 0.0f,
			static_cast<float>(targetExtent.width), static_cast<float>(targetExtent.height), 0.0f, 1.0f };
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkBuffer vertexBuffer = fb.vertices.handle();
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(cmd, fb.indices.handle(), 0, VK_INDEX_TYPE_UINT16);

		for (const GuiDrawCmd& command : data.commands) {
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

	void GuiRenderer::destroy() {
		VkDevice device = getDevice();

		mPipeline.destroy();
		mFrames.clear(); // frees the dynamic buffers while the allocator is alive

		mPool.destroy();
		if (mSetLayout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, mSetLayout, nullptr);
			mSetLayout = VK_NULL_HANDLE;
		}
		mFont.destroy();
	}
}
