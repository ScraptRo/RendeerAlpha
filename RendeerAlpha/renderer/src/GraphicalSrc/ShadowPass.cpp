#include <GraphicalSrc/ShadowPass.h>
#include <GraphicalSrc/Shader.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Mesh.h>
#include <Logger/Logger.h>
#include <glm/gtc/matrix_transform.hpp> // lookAt / ortho
#include <array>
#include <cmath>
#include <vector>

namespace RDA {

	namespace {
		const char* kVertexSource = R"GLSL(
			#version 450
			layout(location = 0) in vec3 inPosition;

			struct ObjectData { mat4 model; mat4 normalMatrix; vec4 baseColor; vec4 surface; };
			layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer {
				ObjectData objects[];
			} objectBuffer;

			layout(push_constant) uniform Push { mat4 lightViewProj; uint objectIndex; } push;

			void main() {
				gl_Position = push.lightViewProj *
				              objectBuffer.objects[push.objectIndex].model * vec4(inPosition, 1.0);
			}
		)GLSL";

		// No colour attachment, so this writes nothing; it exists because the pipeline
		// still needs a fragment stage.
		const char* kFragmentSource = R"GLSL(
			#version 450
			void main() {}
		)GLSL";

		// The matrix travels in the push constant rather than the uniform, so this pass
		// does not depend on the frame's uniform having been written yet.
		struct ShadowPush {
			glm::mat4 lightViewProj;
			uint32_t  objectIndex;
		};
	}

	glm::mat4 ShadowPass::viewProjectionFor(const DirectionalLight& sun) {
		glm::vec3 dir = glm::normalize(sun.direction);
		// lookAt degenerates when the view direction is parallel to `up`.
		const glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f)
		                                               : glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 eye = sun.shadowCenter - dir * (sun.shadowDepth * 0.5f);
		const glm::mat4 view = glm::lookAt(eye, sun.shadowCenter, up);
		// No Y flip here, unlike the camera: this projection is only ever used to write
		// and then read the same map, so it just has to agree with itself.
		const glm::mat4 proj = glm::ortho(-sun.shadowExtent, sun.shadowExtent,
		                                  -sun.shadowExtent, sun.shadowExtent,
		                                  0.0f, sun.shadowDepth);
		return proj * view;
	}

	bool ShadowPass::init(VkDescriptorSetLayout sceneSetLayout) {
		if (!mTarget.createDepthOnly(kSize, kSize)) {
			RDA_LOG_ERROR("Failed to create the shadow map target");
			return false;
		}

		Shader vertex = Shader::fromSource(kVertexSource, ShaderStage::Vertex, "shadow.vert");
		Shader fragment = Shader::fromSource(kFragmentSource, ShaderStage::Fragment, "shadow.frag");
		if (!vertex.isValid() || !fragment.isValid()) {
			RDA_LOG_ERROR("Failed to build the shadow shaders");
			return false;
		}

		VkPushConstantRange push{};
		push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		push.offset = 0;
		push.size = sizeof(ShadowPush);

		// Only position is read, so only that attribute is described.
		VkVertexInputBindingDescription binding = Vertex::getBindingDescription();
		std::array<VkVertexInputAttributeDescription, 3> all = Vertex::getAttributeDescriptions();
		std::vector<VkVertexInputAttributeDescription> attributes = { all[0] };

		mPipeline = PipelineBuilder()
			.addShader(vertex)
			.addShader(fragment)
			.setVertexInput(binding, attributes)
			.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			// Front faces are culled instead of back ones: rendering the *back* of each
			// object into the shadow map pushes the recorded depth away from the surface
			// that will be shaded, which removes most self-shadowing acne before any
			// depth bias is involved.
			.setCull(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
			.setDepth(true, true, VK_COMPARE_OP_LESS)
			.setColorAttachmentCount(0)
			.addDescriptorSetLayout(sceneSetLayout)
			.addPushConstantRange(push)
			.setTarget(mTarget.renderPass(), 0)
			.build();

		if (!mPipeline.isValid()) {
			RDA_LOG_ERROR("Failed to build the shadow pipeline");
			return false;
		}
		return true;
	}

	void ShadowPass::record(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
	                        const glm::mat4& lightViewProjection, const Scene& scene) {
		VkClearValue clear{};
		clear.depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo info{};
		info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		info.renderPass = mTarget.renderPass();
		info.framebuffer = mTarget.framebuffer(0);
		info.renderArea.offset = { 0, 0 };
		info.renderArea.extent = mTarget.extent();
		info.clearValueCount = 1;
		info.pClearValues = &clear;
		vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);

		setViewportAndScissor(cmd, mTarget.extent());
		mPipeline.bind(cmd);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.layout(),
			0, 1, &sceneSet, 0, nullptr);

		// Everything in the scene casts, including objects the camera cannot see — an
		// object behind the viewer can still throw a shadow into frame, so this walks the
		// full list rather than the camera-culled one. The index must follow the scene's
		// item order, because the object buffer was staged in exactly that order.
		uint32_t index = 0;
		for (const DrawItem& item : scene.items()) {
			const uint32_t objectIndex = index++;
			if (!item.mesh.IsValid() || !item.mesh->isValid()) continue;

			ShadowPush push{ lightViewProjection, objectIndex };
			vkCmdPushConstants(cmd, mPipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT,
				0, sizeof(push), &push);
			item.mesh->recordDraw(cmd);
		}

		vkCmdEndRenderPass(cmd);
	}

	void ShadowPass::destroy() {
		mPipeline.destroy();
		mTarget.destroy();
	}
}
