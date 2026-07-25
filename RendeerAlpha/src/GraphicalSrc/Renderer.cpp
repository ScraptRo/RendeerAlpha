#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalSrc/Renderer.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/Swapchain.h>
#include <GraphicalSrc/Shader.h>
#include <GraphicalObjects/Window.h>
#include <GraphicalObjects/Scene.h>
#include <Logger/Logger.h>
#include <array>
#include <thread>
#include <chrono>

namespace RDA {

	// Matches the CameraUBO block in the forward shaders below (std140).
	struct CameraUniform {
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 lightDirection; // xyz = direction the light travels; w unused
	};

	static const char* kForwardVertexSource = R"GLSL(
		#version 450
		layout(location = 0) in vec3 inPosition;
		layout(location = 1) in vec3 inNormal;

		layout(set = 0, binding = 0) uniform CameraUBO {
			mat4 view;
			mat4 proj;
			vec4 lightDirection;
		} camera;

		layout(push_constant) uniform Push {
			mat4 model;
		} push;

		layout(location = 0) out vec3 vWorldNormal;

		void main() {
			gl_Position = camera.proj * camera.view * push.model * vec4(inPosition, 1.0);
			vWorldNormal = mat3(push.model) * inNormal;
		}
	)GLSL";

	static const char* kForwardFragmentSource = R"GLSL(
		#version 450
		layout(location = 0) in vec3 vWorldNormal;

		layout(set = 0, binding = 0) uniform CameraUBO {
			mat4 view;
			mat4 proj;
			vec4 lightDirection;
		} camera;

		layout(location = 0) out vec4 outColor;

		void main() {
			vec3 n = normalize(vWorldNormal);
			vec3 l = normalize(-camera.lightDirection.xyz);
			float diffuse = max(dot(n, l), 0.0);
			vec3 base = vec3(0.80, 0.80, 0.85);
			outColor = vec4(base * (0.15 + 0.85 * diffuse), 1.0);
		}
	)GLSL";

	bool Renderer::init(Window& window, const std::string& fontPath, float fontHeight) {
		// Draw into the window's own surface FrameBuffer by default; the pipeline below
		// is built against its render pass.
		mTarget = &window.getFrameBuffer();
		if (mTarget == nullptr || !mTarget->isValid()) {
			RDA_LOG_ERROR("Window has no valid FrameBuffer to render into");
			return false;
		}
		if (!createFrameResources()) return false;
		if (!createCameraResources()) return false;
		if (!createForwardPipeline()) return false;

		// The GUI overlay shares the target's render pass (drawn after the scene).
		if (!mGuiRenderer.init(mTarget->renderPass(), fontPath, fontHeight, MAX_FRAMES_IN_FLIGHT)) {
			return false;
		}

		RDA_LOG_SUCCES("Forward renderer initialized");
		return true;
	}

	bool Renderer::createFrameResources() {
		GPUInfo& gpu = getGPU();
		VkDevice device = gpu.LDevice;

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = gpu.graphicsFamily;
		if (vkCreateCommandPool(device, &poolInfo, nullptr, &mCommandPool) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create command pool");
			return false;
		}

		mCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = mCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
		if (vkAllocateCommandBuffers(device, &allocInfo, mCommandBuffers.data()) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to allocate command buffers");
			return false;
		}

		mImageAvailable.resize(MAX_FRAMES_IN_FLIGHT);
		mInFlight.resize(MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so frame 0 doesn't deadlock

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &mImageAvailable[i]) != VK_SUCCESS ||
				vkCreateFence(device, &fenceInfo, nullptr, &mInFlight[i]) != VK_SUCCESS) {
				RDA_LOG_ERROR("Failed to create frame synchronization objects");
				return false;
			}
		}
		return true;
	}

	bool Renderer::createCameraResources() {
		// One uniform buffer + one descriptor set per frame in flight.
		mCameraSetLayout = DescriptorLayoutBuilder()
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
			.build();
		if (mCameraSetLayout == VK_NULL_HANDLE) return false;

		std::vector<VkDescriptorPoolSize> sizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT },
		};
		if (!mDescriptorPool.init(MAX_FRAMES_IN_FLIGHT, sizes)) return false;

		mCameraUniforms.resize(MAX_FRAMES_IN_FLIGHT);
		mCameraSets.resize(MAX_FRAMES_IN_FLIGHT);
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			if (!mCameraUniforms[i].create(sizeof(CameraUniform),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryResidence::CpuToGpu)) {
				RDA_LOG_ERROR("Failed to create camera uniform buffer");
				return false;
			}
			mCameraSets[i] = mDescriptorPool.allocate(mCameraSetLayout);
			if (mCameraSets[i] == VK_NULL_HANDLE) return false;

			DescriptorWriter()
				.writeBuffer(0, mCameraUniforms[i].handle(), sizeof(CameraUniform), 0,
				             VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
				.update(mCameraSets[i]);
		}
		return true;
	}

	bool Renderer::createForwardPipeline() {
		Shader vertex = Shader::fromSource(kForwardVertexSource, ShaderStage::Vertex, "forward.vert");
		Shader fragment = Shader::fromSource(kForwardFragmentSource, ShaderStage::Fragment, "forward.frag");
		if (!vertex.isValid() || !fragment.isValid()) {
			RDA_LOG_ERROR("Failed to build forward shaders");
			return false;
		}

		VkPushConstantRange modelPush{};
		modelPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		modelPush.offset = 0;
		modelPush.size = sizeof(glm::mat4);

		// The mesh buffer is interleaved position/normal/uv, but the forward shader
		// only reads position and normal, so describe just those two (the stride still
		// steps over the whole Vertex). The uv attribute joins here once it's textured.
		VkVertexInputBindingDescription binding = Vertex::getBindingDescription();
		std::array<VkVertexInputAttributeDescription, 3> allAttributes = Vertex::getAttributeDescriptions();
		std::vector<VkVertexInputAttributeDescription> attributes = { allAttributes[0], allAttributes[1] };

		mForwardPipeline = PipelineBuilder()
			.addShader(vertex)
			.addShader(fragment)
			.setVertexInput(binding, attributes)
			.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			// Cull nothing for now: it keeps a first mesh visible regardless of its
			// winding. Batching/deferred later will want proper back-face culling.
			.setCull(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
			.setDepth(true, true, VK_COMPARE_OP_LESS)
			.addDescriptorSetLayout(mCameraSetLayout)
			.addPushConstantRange(modelPush)
			.setTarget(mTarget->renderPass(), 0)
			.build();

		if (!mForwardPipeline.isValid()) {
			RDA_LOG_ERROR("Failed to build the forward pipeline");
			return false;
		}
		return true;
	}

	Material Renderer::forwardMaterial() const {
		Material material;
		material.setPipeline(&mForwardPipeline);
		return material;
	}

	void Renderer::destroy() {
		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE) return;

		mGuiRenderer.destroy();
		mForwardPipeline.destroy();

		mCameraUniforms.clear();
		mCameraSets.clear();
		mDescriptorPool.destroy();
		if (mCameraSetLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, mCameraSetLayout, nullptr);
			mCameraSetLayout = VK_NULL_HANDLE;
		}

		for (size_t i = 0; i < mImageAvailable.size(); i++) {
			vkDestroySemaphore(device, mImageAvailable[i], nullptr);
			vkDestroyFence(device, mInFlight[i], nullptr);
		}
		mImageAvailable.clear();
		mInFlight.clear();

		if (mCommandPool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(device, mCommandPool, nullptr);
			mCommandPool = VK_NULL_HANDLE;
		}

		// mTarget is borrowed (the window owns the surface FrameBuffer), so it is not
		// destroyed here.
		mTarget = nullptr;
	}

	void Renderer::waitIdle() {
		VkDevice device = getDevice();
		if (device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(device);
		}
	}

	void Renderer::updateCamera(uint32_t frame) {
		const Scene& scene = getScene();
		CameraUniform data{};
		data.view = scene.camera.view;
		data.proj = scene.camera.proj;
		data.lightDirection = glm::vec4(scene.lightDirection, 0.0f);
		mCameraUniforms[frame].upload(&data, sizeof(data));
	}

	void Renderer::drawWindow(Window& window) {
		GPUInfo& gpu = getGPU();
		VkDevice device = gpu.LDevice;
		Swapchain& swapchain = window.getSwapchain();

		// Minimized: no surface to render to. Idle briefly so the loop doesn't spin.
		VkExtent2D windowExtent = window.cachedExtent();
		if (windowExtent.width == 0 || windowExtent.height == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			return;
		}

		vkWaitForFences(device, 1, &mInFlight[mCurrentFrame], VK_TRUE, UINT64_MAX);

		uint32_t imageIndex = 0;
		VkResult acquire = vkAcquireNextImageKHR(
			device, swapchain.handle(), UINT64_MAX,
			mImageAvailable[mCurrentFrame], VK_NULL_HANDLE, &imageIndex);

		if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
			// The window rebuilds its swapchain and its surface FrameBuffer together;
			// mTarget points at that same FrameBuffer, so it stays valid afterwards.
			window.recreateSwapchain();
			return;
		}
		if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
			RDA_LOG_ERROR("Failed to acquire swapchain image");
			return;
		}

		// If a previous frame is still rendering into this image, wait for it.
		VkFence& imageInFlight = swapchain.imageInFlight(imageIndex);
		if (imageInFlight != VK_NULL_HANDLE) {
			vkWaitForFences(device, 1, &imageInFlight, VK_TRUE, UINT64_MAX);
		}
		imageInFlight = mInFlight[mCurrentFrame];

		vkResetFences(device, 1, &mInFlight[mCurrentFrame]);

		updateCamera(mCurrentFrame);

		VkCommandBuffer cmd = mCommandBuffers[mCurrentFrame];
		vkResetCommandBuffer(cmd, 0);
		recordFrame(cmd, window, imageIndex);

		VkSemaphore waitSemaphores[] = { mImageAvailable[mCurrentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSemaphore signalSemaphores[] = { swapchain.renderFinishedSemaphore(imageIndex) };

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(gpu.graphicsQueue, 1, &submitInfo, mInFlight[mCurrentFrame]) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to submit draw command buffer");
			return;
		}

		VkSwapchainKHR swapchains[] = { swapchain.handle() };
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;
		presentInfo.pImageIndices = &imageIndex;

		VkResult present = vkQueuePresentKHR(gpu.presentQueue, &presentInfo);
		if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR || window.wasResized()) {
			window.recreateSwapchain();
		} else if (present != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to present swapchain image");
		}

		mCurrentFrame = (mCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void Renderer::recordFrame(VkCommandBuffer cmd, Window& window, uint32_t imageIndex) {
		Swapchain& swapchain = window.getSwapchain();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to begin command buffer");
			return;
		}

		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { { 0.02f, 0.02f, 0.03f, 1.0f } }; // dark slate
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = mTarget->renderPass();
		renderPassInfo.framebuffer = mTarget->framebuffer(imageIndex);
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapchain.extent();
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		setViewportAndScissor(cmd, swapchain.extent());

		mForwardPipeline.bind(cmd);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mForwardPipeline.layout(),
			0, 1, &mCameraSets[mCurrentFrame], 0, nullptr);

		// One draw per scene item. This flat loop is exactly what batching and
		// instancing will later replace, without the render pass above changing.
		const Scene& scene = getScene();
		for (const DrawItem& item : scene.items()) {
			if (!item.mesh.IsValid() || !item.mesh->isValid()) continue;

			vkCmdPushConstants(cmd, mForwardPipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT,
				0, sizeof(glm::mat4), &item.transform);
			item.mesh->recordDraw(cmd);
		}

		// GUI overlay: same pass, after the scene, no clear — draws on top.
		mGuiRenderer.record(cmd, window.gui().drawData(), swapchain.extent(), mCurrentFrame);

		vkCmdEndRenderPass(cmd);

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to record command buffer");
		}
	}
}
