#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/FrameBuffer.h>
#include <GraphicalSrc/GraphicsPipeline.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <GraphicalSrc/Descriptors.h>
#include <GraphicalSrc/GuiRenderer.h>
#include <GraphicalObjects/Material.h>
#include <string>

namespace RDA {
	class Window;

	// The forward renderer. It owns the frame machinery (command buffers, per-frame
	// synchronisation), the render target for a window, one forward pipeline, and a
	// per-frame camera uniform. Each frame it walks the engine Scene and draws every
	// item through that single pipeline.
	//
	// This is deliberately the whole rendering policy in one place, so replacing it
	// — batching, instancing, a deferred path — is a change to this class and the
	// pipeline it builds, not to the window or the object layer.
	class Renderer {
	public:
		static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

		// Builds everything against `window`'s surface FrameBuffer: the forward pipeline
		// and the GUI overlay renderer (which bakes the font atlas from fontPath). In
		// ViewportMode::Widget it also builds an offscreen scene target.
		bool init(Window& window, const std::string& fontPath, float fontHeight, ViewportMode mode);
		void destroy();

		// The baked font atlas, so a window's Gui frontend can share its CPU metrics.
		const FontAtlas& fontAtlas() const { return mGuiRenderer.fontAtlas(); }

		// In Widget mode, the offscreen color texture the scene is rendered into (for a
		// Viewport widget to display); nullptr in Fullscreen mode.
		const Texture* sceneTexture() const;

		// Widget mode: (re)size the offscreen scene target to `extent` (the Viewport
		// widget's size), so the scene is rendered at the viewport's resolution/aspect.
		// A no-op if the size is unchanged.
		void ensureSceneTarget(VkExtent2D extent);

		// Renders one frame into the current target and presents it to the window.
		void drawWindow(Window& window);

		// Point the renderer at a different target. The swap is valid as long as the new
		// target's render pass is compatible with the one the pipeline was built against
		// (same color format) — which is the case for any FrameBuffer sharing the window
		// surface's format. Pass the window's own FrameBuffer to go back to the default.
		void setTarget(FrameBuffer& target) { mTarget = &target; }
		FrameBuffer* target() const { return mTarget; }

		void waitIdle();

		// Widget mode only: rasterise the GUI into a cached layer and composite it, so an
		// animating scene stops re-drawing an unchanged GUI every frame. Off by default —
		// see AppConfig::cacheGuiLayer.
		void setGuiLayerCaching(bool enabled) { mGuiLayerCaching = enabled; }

		// A material bound to this renderer's forward pipeline. Meshes added to the
		// Scene need a material; the camera set and per-object transform are supplied
		// by the renderer, so this material carries no descriptor sets of its own.
		Material forwardMaterial() const;

	private:
		VkCommandPool mCommandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> mCommandBuffers;

		std::vector<VkSemaphore> mImageAvailable;
		std::vector<VkFence>     mInFlight;
		uint32_t mCurrentFrame = 0;

		// The target the renderer draws into. Borrowed, not owned — by default it points
		// at the window's surface FrameBuffer, which the window builds and rebuilds.
		FrameBuffer* mTarget = nullptr;

		// Camera uniform: one buffer + one set per frame in flight, so updating this
		// frame's copy never races a frame the GPU is still reading.
		VkDescriptorSetLayout        mCameraSetLayout = VK_NULL_HANDLE;
		DescriptorAllocator          mDescriptorPool;
		std::vector<MemoryBuffer>    mCameraUniforms;
		std::vector<VkDescriptorSet> mCameraSets;

		GraphicsPipeline mForwardPipeline;

		// Draws the window's Gui — as an overlay in Fullscreen mode, or the whole surface
		// pass (with the scene shown by a Viewport widget) in Widget mode.
		GuiRenderer mGuiRenderer;

		// Widget mode: the scene is rendered here, then sampled by a Viewport widget.
		ViewportMode mViewportMode = ViewportMode::Fullscreen;
		FrameBuffer  mSceneTarget;
		VkFormat     mSceneColorFormat = VK_FORMAT_UNDEFINED;
		// Debounce for offscreen resizes: only recreate once the requested size settles,
		// so dragging a splitter doesn't recreate (and GPU-idle) the target every frame.
		VkExtent2D   mPendingSceneExtent{};
		int          mSceneSettleFrames = 0;

		// Widget mode: the GUI is rasterised into this layer only when it changes, and
		// composited over the live scene every frame. Without it, animating the scene
		// re-drew every panel, glyph and border of a GUI that had not changed at all.
		FrameBuffer  mGuiLayer;
		uint64_t     mGuiLayerVersion = 0;
		bool         mGuiLayerValid = false;
		bool         mGuiLayerCaching = false; // opt-in; see AppConfig::cacheGuiLayer
		// Rebuilds the layer target at `extent` when the window size changed.
		bool ensureGuiLayer(VkExtent2D extent);

		bool createFrameResources();
		bool createCameraResources();
		bool createForwardPipeline();

		void updateCamera(uint32_t frame);
		void drawSceneItems(VkCommandBuffer cmd);
		void recordFrame(VkCommandBuffer cmd, Window& window, uint32_t imageIndex);
	};
}
