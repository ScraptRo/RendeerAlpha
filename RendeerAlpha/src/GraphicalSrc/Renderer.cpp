#include <GraphicalSrc/Renderer.h>
#include <GraphicalObjects/Viewports.h>
#include <GraphicalSrc/SceneBindings.h>
#include <GraphicalSrc/SceneFrame.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/Swapchain.h>
#include <GraphicalSrc/Shader.h>
#include <GraphicalObjects/Window.h>
#include <GraphicalObjects/Scene.h>
#include <Logger/Logger.h>
#include <glm/gtc/matrix_transform.hpp> // lookAt / ortho, for the light's matrix
#include <array>
#include <cmath>
#include <thread>
#include <chrono>

namespace RDA {

	// Set 0's layout lives in SceneBindings.h; the forward technique in ForwardPass.

	bool Renderer::init(Window& window, const std::string& fontPath, float fontHeight,
	                    const std::vector<float>& fontSizes, ViewportMode mode,
	                    bool guiEnabled) {
		mGuiEnabled = guiEnabled;
		// A Viewport widget is the only thing that can display an offscreen scene, so
		// without a GUI there is nothing to render into one.
		if (!mGuiEnabled && mode == ViewportMode::Widget) {
			RDA_LOG_WARNING("ViewportMode::Widget needs a GUI to display the scene; using Fullscreen");
			mode = ViewportMode::Fullscreen;
		}
		mViewportMode = mode;

		// Draw into the window's own surface FrameBuffer by default; the pipeline below
		// is built against its render pass.
		mTarget = &window.getFrameBuffer();
		if (mTarget == nullptr || !mTarget->isValid()) {
			RDA_LOG_ERROR("Window has no valid FrameBuffer to render into");
			return false;
		}
		if (!createFrameResources()) return false;
		if (!createCameraResources()) return false;
		// Before the material resources so binding 2 of the frame sets is written while
		// they are still untouched by any frame.
		if (!mShadowPass.init(mCameraSetLayout)) return false;
		if (!mMaterials.init()) return false;

		// Widget mode: build the offscreen scene target first, so the forward pipeline can
		// be built against the render pass it will actually draw the scene into. It starts
		// window-sized; the engine resizes it to the Viewport widget from the next frame.
		if (mViewportMode == ViewportMode::Widget) {
			mSceneColorFormat = mTarget->colorFormat();
			ensureSceneTarget(window.cachedExtent());
			if (!mSceneTarget.isValid()) {
				RDA_LOG_ERROR("Failed to create the offscreen scene target");
				return false;
			}
		}

		// Built against whichever pass the scene is actually drawn into: the offscreen
		// target in Widget mode, the window surface otherwise.
		VkRenderPass scenePass = (mViewportMode == ViewportMode::Widget && mSceneTarget.isValid())
			? mSceneTarget.renderPass() : mTarget->renderPass();
		if (!mForward.init(scenePass, mCameraSetLayout, mMaterials.layout())) return false;

		// The GUI always draws into the surface pass. Skipped entirely when the
		// application has no GUI — that is where the font bake and the GUI pipelines are
		// paid for, so an application without one never builds them.
		if (mGuiEnabled) {
			if (!mGuiRenderer.init(mTarget->renderPass(), fontPath, fontHeight, fontSizes,
			                          MAX_FRAMES_IN_FLIGHT)) {
				return false;
			}
		}

		// Parenthesised: the log macros expand to `oss << x`, and << binds tighter than
		// ?:, so an unwrapped ternary would stream the condition instead of the message.
		RDA_LOG_SUCCES((mGuiEnabled ? "Forward renderer initialized"
		                            : "Forward renderer initialized (no GUI)"));
		return true;
	}

	void Renderer::ensureSceneTarget(VkExtent2D extent) {
		// Frames the requested size must hold steady before we commit to a resize (~0.1s).
		static constexpr int kSettleFrames = 6;

		if (mViewportMode != ViewportMode::Widget) return;
		if (extent.width == 0 || extent.height == 0) return;

		if (mSceneTarget.isValid() &&
			mSceneTarget.extent().width == extent.width && mSceneTarget.extent().height == extent.height) {
			mSceneSettleFrames = 0; // already at the requested size
			return;
		}

		// Debounce: an active drag changes the size every frame, so wait until it stops
		// changing. The viewport briefly samples the old-sized texture (a slight stretch)
		// during the settle — cheap and transient — instead of recreating each frame.
		if (mSceneTarget.isValid()) {
			if (extent.width == mPendingSceneExtent.width && extent.height == mPendingSceneExtent.height) {
				if (++mSceneSettleFrames < kSettleFrames) return;
			} else {
				mPendingSceneExtent = extent;
				mSceneSettleFrames = 0;
				return;
			}
			// Settled: the old target may still be in flight, so drain before recreating.
			vkDeviceWaitIdle(getDevice());
		}

		// The colour texture is a member of the target, so recreating it keeps its address
		// while replacing the view and sampler inside it. Anything caching by that address
		// — the GUI's image descriptor sets — would go on pointing at the destroyed view,
		// so what points at it is released first. A no-op on the first creation, when
		// nothing has sampled it yet.
		forgetTexture(&mSceneTarget.colorTexture());

		mSceneTarget.createOffscreen(extent.width, extent.height, mSceneColorFormat);
		mSceneSettleFrames = 0;
	}

	VkRenderPass Renderer::sceneRenderPass() const {
		if (mViewportMode == ViewportMode::Widget && mSceneTarget.isValid()) {
			return mSceneTarget.renderPass();
		}
		return VK_NULL_HANDLE;
	}

	const Texture* Renderer::sceneTexture() const {
		if (mViewportMode == ViewportMode::Widget && mSceneTarget.isValid()) {
			return &mSceneTarget.colorTexture();
		}
		return nullptr;
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

		// Per-window frame resources are built on first draw; only the pool is made here.
		return true;
	}

	bool Renderer::createCameraResources() {
		// One uniform buffer + one descriptor set per frame in flight.
		mCameraSetLayout = DescriptorLayoutBuilder()
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
			.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build();
		if (mCameraSetLayout == VK_NULL_HANDLE) return false;

		// A block of one window's worth of frames. The allocator opens another whenever
		// this one fills, so this is the granularity of growth, not a limit on windows —
		// which it used to be, silently, at exactly one window.
		constexpr uint32_t kSetsPerBlock = MAX_FRAMES_IN_FLIGHT * 4;
		std::vector<VkDescriptorPoolSize> sizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSetsPerBlock },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSetsPerBlock },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerBlock },
		};
		if (!mDescriptorPool.init(kSetsPerBlock, sizes)) return false;

		// The buffers and sets themselves are built per window, by framesFor().
		return true;
	}

	// One window's set 0, for each frame in flight.
	bool Renderer::createFrameSceneResources(WindowFrames& frames) {
		for (WindowFrames::Frame& frame : frames.frames) {
			if (!frame.scene.create(mDescriptorPool, mCameraSetLayout)) return false;
			// The shadow map exists by now: ShadowPass::init() runs during init(), and
			// this runs on a window's first draw.
			if (mShadowPass.isValid()) frame.scene.bindShadowMap(mShadowPass.depthTexture());
		}
		return true;
	}

	// Depth-only: position through the light's matrix, nothing else. It still reads the
	// object buffer, so it uses the same set 0 and the same per-draw index as the main
	// pass — one source of truth for where an object is.
	Material Renderer::forwardMaterial(const MaterialTextures& textures) {
		Material material;
		material.setPipeline(&mForward.pipeline());
		material.textures = textures; // keeps the textures alive alongside the set

		VkDescriptorSet set = mMaterials.allocateSet(textures);
		if (set == VK_NULL_HANDLE) return material; // drawn untextured rather than not at all
		material.setSets(1, { set }); // set 1; set 0 is the frame's scene data
		return material;
	}

	void Renderer::releaseMaterial(Material& material) {
		for (VkDescriptorSet set : material.takeSets()) mMaterials.releaseSet(set);
	}

	Material Renderer::forwardMaterial() {
		// Still gets a descriptor set, filled with the neutral defaults. The shader uses
		// set 1 unconditionally, so a material without one would be a bound-descriptor
		// error at draw time rather than simply "untextured".
		return forwardMaterial(MaterialTextures{});
	}

	void Renderer::destroy() {
		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE) return;

		mGuiRenderer.destroy();
		mSceneTarget.destroy();
		mGuiLayer.destroy();
		mShadowPass.destroy();
		mForward.destroy();


		// Before the pool: every window's set 0 was allocated from it, and its buffers
		// are freed here. Ordering the other way round works only by accident.
		destroyWindowFrames();

		mMaterials.destroy();
		mDescriptorPool.destroy();
		if (mCameraSetLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, mCameraSetLayout, nullptr);
			mCameraSetLayout = VK_NULL_HANDLE;
		}

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

	Renderer::WindowFrames* Renderer::framesFor(const Window* window) {
		auto found = mWindowFrames.find(window);
		if (found != mWindowFrames.end()) return &found->second;

		VkDevice device = getDevice();

		// Allocated in one call, as the API wants, then handed out one per frame.
		std::vector<VkCommandBuffer> commandBuffers(MAX_FRAMES_IN_FLIGHT);
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = mCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
		if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to allocate command buffers for a window");
			return nullptr;
		}

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so frame 0 doesn't deadlock

		WindowFrames frames;
		frames.frames.resize(MAX_FRAMES_IN_FLIGHT);
		// Handed out before anything else can fail, so every one of them has an owner that
		// releaseFrames() will find. Assigning them as the loop below went would strand the
		// ones past the failure with nothing pointing at them.
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) frames[i].command = commandBuffers[i];

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			WindowFrames::Frame& frame = frames[i];
			// Into locals, then stored: a failed create leaves its handle undefined rather
			// than null, and passing that to vkDestroy* would be worse than the leak.
			VkSemaphore semaphore = VK_NULL_HANDLE;
			VkFence fence = VK_NULL_HANDLE;
			const bool ok =
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) == VK_SUCCESS &&
				vkCreateFence(device, &fenceInfo, nullptr, &fence) == VK_SUCCESS;
			if (ok) {
				frame.imageAvailable = semaphore;
				frame.fence = fence;
				continue;
			}
			// Keep whichever half was made so it goes with the rest.
			if (semaphore != VK_NULL_HANDLE) frame.imageAvailable = semaphore;
			RDA_LOG_ERROR("Failed to create frame synchronization objects for a window");
			releaseFrames(frames);
			return nullptr;
		}

		// Inserted before the scene resources are built, because building them needs a
		// stable home: the MemoryBuffers inside are move-only and own device memory, and
		// creating them into a local that is then moved would work but leaves two objects
		// briefly owning the same allocation.
		auto inserted = mWindowFrames.emplace(window, std::move(frames));
		WindowFrames& stored = inserted.first->second;
		if (!createFrameSceneResources(stored)) {
			RDA_LOG_ERROR("Failed to create scene resources for a window");
			// Erasing alone would free the buffers and leave every semaphore, fence and
			// command buffer behind — they are raw handles with no destructor.
			releaseFrames(stored);
			mWindowFrames.erase(inserted.first);
			return nullptr;
		}
		return &stored;
	}

	void Renderer::releaseFrames(WindowFrames& frames) {
		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE) { frames.frames.clear(); return; }

		// The command buffers are freed in one call, so they are gathered back up;
		// everything else a frame owns is released as we go.
		std::vector<VkCommandBuffer> commandBuffers;
		commandBuffers.reserve(frames.frames.size());
		for (WindowFrames::Frame& frame : frames.frames) {
			if (frame.imageAvailable != VK_NULL_HANDLE) {
				vkDestroySemaphore(device, frame.imageAvailable, nullptr);
			}
			if (frame.fence != VK_NULL_HANDLE) vkDestroyFence(device, frame.fence, nullptr);
			if (frame.command != VK_NULL_HANDLE) commandBuffers.push_back(frame.command);
			// Its set 0 buffers go too. The sets themselves are the pool's to free.
			frame.scene.destroy();
		}
		if (!commandBuffers.empty() && mCommandPool != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(device, mCommandPool,
				static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
		}
		frames.frames.clear();
	}

	void Renderer::forgetWindow(const Window* window) {
		auto found = mWindowFrames.find(window);
		if (found == mWindowFrames.end()) return;
		releaseFrames(found->second);
		mWindowFrames.erase(found);
		// Its GUI geometry buffers go the same way, and for the same reason: a frame in
		// flight may still have been reading them until the caller waited.
		mGuiRenderer.forgetWindow(window);
		// The target may have been pointing at the framebuffer that is about to go.
		if (mTarget) mTarget = nullptr;
	}

	void Renderer::destroyWindowFrames() {
		// Runs before the command pool and the descriptor pool are destroyed, so freeing
		// each window's frames individually is still valid here.
		for (auto& entry : mWindowFrames) releaseFrames(entry.second);
		mWindowFrames.clear();
	}

	void Renderer::drawWindow(Window& window) {
		GPUInfo& gpu = getGPU();
		VkDevice device = gpu.LDevice;
		Swapchain& swapchain = window.getSwapchain();

		// This window's own fences, semaphores and command buffers. Built on first draw.
		WindowFrames* frames = framesFor(&window);
		if (!frames) return;
		// Handed to every helper below rather than stashed on the renderer. This frame
		// belongs to this window and to nothing else.
		const uint32_t frameIndex = frames->currentFrame;
		WindowFrames::Frame& frame = (*frames)[frameIndex];

		// Every window draws into its own surface framebuffer. All windows share the
		// surface format, so the pipelines built against the first one stay compatible.
		mTarget = &window.getFrameBuffer();

		// Minimized: no surface to render to. Idle briefly so the loop doesn't spin.
		VkExtent2D windowExtent = window.cachedExtent();
		if (windowExtent.width == 0 || windowExtent.height == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			return;
		}

		// It has a size now but no swapchain: it was minimised when it was created, or
		// while a rebuild was refused. This is the frame that gives it one, and there
		// is nothing to acquire from until it has.
		if (!swapchain.isValid()) {
			window.recreateSwapchain();
			return;
		}

		vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX);

		uint32_t imageIndex = 0;
		VkResult acquire = vkAcquireNextImageKHR(
			device, swapchain.handle(), UINT64_MAX,
			frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);

		if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
			// The window rebuilds its swapchain and its surface FrameBuffer together;
			// mTarget points at that same FrameBuffer, so it stays valid afterwards. The
			// offscreen scene target follows the Viewport widget (resized by the engine),
			// not the window, so it needs nothing here.
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
		imageInFlight = frame.fence;

		vkResetFences(device, 1, &frame.fence);

		// Both run after the fence wait above, so this frame's buffers and descriptor set
		// are not being read by anything in flight.
		// This window's set 0 for this frame. Both writes land in buffers nothing else
		// can reach, and the fence above means nothing in flight is reading them.
		const Scene& scene = getScene();
		frame.scene.updateCamera(scene, ShadowPass::viewProjectionFor(scene.sun));
		frame.scene.updateObjects(scene, mObjectScratch);

		VkCommandBuffer cmd = frame.command;
		vkResetCommandBuffer(cmd, 0);
		recordFrame(cmd, window, imageIndex, frame, frameIndex);

		VkSemaphore waitSemaphores[] = { frame.imageAvailable };
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

		if (vkQueueSubmit(gpu.graphicsQueue, 1, &submitInfo, frame.fence) != VK_SUCCESS) {
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

		frames->currentFrame = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	// The colour a viewport's own target starts each frame at, in the same 0xAABBGGRR
	// packing as every other colour here.
	//
	// Decoded on the way, for the same reason the GUI's vertex shader decodes: the target
	// is an sRGB format, so a clear value is written through the same linear->sRGB encode
	// as everything else, and a colour a person wrote is already encoded. This is the one
	// colour that reaches an attachment without passing through a shader, which is why it
	// needs saying twice -- and why a background was still too light after the shader was
	// fixed.
	static float srgbToLinear(float c) {
		return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}

	static VkClearColorValue unpackClear(uint32_t color) {
		const auto channel = [](uint32_t byte) {
			return srgbToLinear(static_cast<float>(byte) / 255.0f);
		};
		return VkClearColorValue{ {
			channel( color        & 0xFFu),
			channel((color >>  8) & 0xFFu),
			channel((color >> 16) & 0xFFu),
			// Alpha is coverage, not light, and was never gamma-encoded.
			static_cast<float>((color >> 24) & 0xFFu) / 255.0f,
		} };
	}

	static void beginPass(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer framebuffer,
	                      VkExtent2D extent, bool transparentClear = false,
	                      const VkClearColorValue* clearColor = nullptr) {
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = transparentClear
			? VkClearColorValue{ { 0.0f, 0.0f, 0.0f, 0.0f } } // GUI layer: nothing drawn = see-through
			: clearColor ? *clearColor
			: VkClearColorValue{ { 0.02f, 0.02f, 0.03f, 1.0f } }; // dark slate
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo info{};
		info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		info.renderPass = pass;
		info.framebuffer = framebuffer;
		info.renderArea.offset = { 0, 0 };
		info.renderArea.extent = extent;
		info.clearValueCount = static_cast<uint32_t>(clearValues.size());
		info.pClearValues = clearValues.data();
		vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
	}

	bool Renderer::ensureGuiLayer(VkExtent2D extent) {
		if (extent.width == 0 || extent.height == 0) return false;
		if (mGuiLayer.isValid() && mGuiLayer.extent().width == extent.width &&
		    mGuiLayer.extent().height == extent.height) {
			return true;
		}
		// Size follows the window. Recreating drops the cached content, so the layer is
		// marked invalid and re-rasterised on this frame.
		vkDeviceWaitIdle(getDevice());
		// Same as the scene target: the layer's colour texture keeps its address across a
		// resize, so the descriptor set describing it has to go before the view it names.
		forgetTexture(&mGuiLayer.colorTexture());
		mGuiLayer.destroy();
		if (!mGuiLayer.createOffscreen(extent.width, extent.height, mSceneColorFormat)) {
			RDA_LOG_ERROR("Failed to create the GUI layer target");
			return false;
		}
		// The GUI pipelines are per-render-pass, so the layer needs its own set. The
		// render pass survives a resize, so this only ever builds once.
		if (!mGuiRenderer.buildLayerPipelines(mGuiLayer.renderPass())) return false;
		mGuiLayerValid = false;
		return true;
	}

	void Renderer::recordFrame(VkCommandBuffer cmd, Window& window, uint32_t imageIndex,
	                           WindowFrames::Frame& frame, uint32_t frameIndex) {
		Swapchain& swapchain = window.getSwapchain();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to begin command buffer");
			return;
		}

		// The shadow map is rebuilt first: every later pass samples it, and its render
		// pass already declares the dependency that makes those writes visible.
		if (getScene().sun.castsShadows) {
			mShadowPass.record(cmd, frame.scene.set(),
			                   ShadowPass::viewProjectionFor(getScene().sun), getScene());
		}

		if (!mGuiEnabled) {
			// No GUI: the scene is the frame. One pass, straight to the window surface.
			beginPass(cmd, mTarget->renderPass(), mTarget->framebuffer(imageIndex), swapchain.extent());
			setViewportAndScissor(cmd, swapchain.extent());
			mForward.record(cmd, frame.scene.set(), getScene());
			vkCmdEndRenderPass(cmd);

			if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
				RDA_LOG_ERROR("Failed to record command buffer");
			}
			return;
		}

		mGuiRenderer.beginFrame(&window, frameIndex);

		// What the window starts the frame at, from the theme. Read once: all three of the
		// surface passes below want the same answer, and the two that draw the GUI over it
		// only show it where nothing was drawn -- which is exactly what a background is.
		const VkClearColorValue surfaceClear =
			unpackClear(window.gui().backgroundColor());

		if (mViewportMode == ViewportMode::Widget && mSceneTarget.isValid()) {
			// Pass 1: the scene into its offscreen target (sampleable afterwards).
			const VkClearColorValue sceneClear =
				unpackClear(viewports().clearColor(viewports().gpuViewport()));
			beginPass(cmd, mSceneTarget.renderPass(), mSceneTarget.framebuffer(0),
			          mSceneTarget.extent(), /*transparentClear*/ false, &sceneClear);
			setViewportAndScissor(cmd, mSceneTarget.extent());
			mForward.record(cmd, frame.scene.set(), getScene());
			// Then whatever the application records for itself, into the same target and
			// the same pass. After the scene rather than instead of it: an application
			// that only wants its own drawing simply adds no meshes, and one that wants
			// both -- a gizmo over a model -- gets the order it expects.
			viewports().record(cmd, mSceneTarget.renderPass(), mSceneTarget.extent(), frameIndex);
			vkCmdEndRenderPass(cmd);

			Gui& gui = window.gui();
			const bool layerReady = mGuiLayerCaching && ensureGuiLayer(swapchain.extent());
			if (layerReady) {
				// Pass 2 (only when the GUI actually changed): rasterise the whole GUI
				// once into the cached layer, leaving the Viewport widget's quad out so
				// its area stays transparent for the live scene to show through.
				if (!mGuiLayerValid || mGuiLayerVersion != gui.drawVersion()) {
					beginPass(cmd, mGuiLayer.renderPass(), mGuiLayer.framebuffer(0),
					          mGuiLayer.extent(), /*transparentClear*/ true);
					setViewportAndScissor(cmd, mGuiLayer.extent());
					mGuiRenderer.record(cmd, &window, gui.drawData(), mGuiLayer.extent(), frameIndex,
					                    gui.drawVersion(), /*skipTexture*/ sceneTexture(),
					                    /*intoLayer*/ true);
					vkCmdEndRenderPass(cmd);
					mGuiLayerVersion = gui.drawVersion();
					mGuiLayerValid = true;
				}

				// Pass 3: two quads — the live scene, then the cached GUI over it.
				beginPass(cmd, mTarget->renderPass(), mTarget->framebuffer(imageIndex),
				          swapchain.extent(), /*transparentClear*/ false, &surfaceClear);
				mGuiRenderer.recordComposite(cmd, &window, swapchain.extent(), frameIndex,
				                             sceneTexture(), gui.viewportRect(),
				                             &mGuiLayer.colorTexture());
				vkCmdEndRenderPass(cmd);
			} else {
				// No layer (allocation failed): fall back to drawing the GUI directly.
				beginPass(cmd, mTarget->renderPass(), mTarget->framebuffer(imageIndex),
				          swapchain.extent(), /*transparentClear*/ false, &surfaceClear);
				mGuiRenderer.record(cmd, &window, gui.drawData(), swapchain.extent(), frameIndex,
				                    gui.drawVersion());
				vkCmdEndRenderPass(cmd);
			}
		} else {
			// Scene then GUI overlay, both into the surface, in one pass.
			beginPass(cmd, mTarget->renderPass(), mTarget->framebuffer(imageIndex),
				          swapchain.extent(), /*transparentClear*/ false, &surfaceClear);
			setViewportAndScissor(cmd, swapchain.extent());
			mForward.record(cmd, frame.scene.set(), getScene());
			mGuiRenderer.record(cmd, &window, window.gui().drawData(), swapchain.extent(), frameIndex,
			                    window.gui().drawVersion());
			vkCmdEndRenderPass(cmd);
		}

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to record command buffer");
		}
	}
}
