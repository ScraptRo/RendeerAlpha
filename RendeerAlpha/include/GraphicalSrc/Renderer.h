#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalSrc/FrameBuffer.h>
#include <GraphicalSrc/GraphicsPipeline.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <GraphicalSrc/Descriptors.h>
#include <GraphicalSrc/MaterialResources.h>
#include <GraphicalSrc/ShadowPass.h>
#include <GraphicalSrc/ForwardPass.h>
#include <GraphicalSrc/SceneBindings.h>
#include <GraphicalSrc/SceneFrame.h>
#include <Core/BuildMode.h>
#include <unordered_map>
#include <GraphicalSrc/GuiRenderer.h>
#include <GraphicalObjects/Material.h>
#include <string>

namespace RDA {
	class Window;

	// The frame loop. It owns the machinery a window needs to have frames in flight
	// (command buffers, fences, semaphores), the scene data bound at set 0, and the
	// targets everything draws into — then hands the recording to the techniques.
	//
	// It used to be all of that *and* every technique: the forward pipeline and its
	// shaders, the shadow pass, the material sets, in one file of thirteen hundred lines.
	// Those are now ForwardPass, ShadowPass and MaterialResources, which is what the
	// engine's design said in the first place — the technique layer sits above
	// GraphicsPipeline, not inside the renderer. Adding a deferred or batched path is
	// writing a class beside those, and teaching recordFrame() to call it.
	//
	// What is deliberately still here is the *policy*: which pass runs, in what order,
	// into which target. That is one frame's shape, and it belongs in one place.
	class Renderer {
	public:
		static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

		// Builds everything against `window`'s surface FrameBuffer: the forward pipeline
		// and the GUI overlay renderer (which bakes the font atlas from fontPath). In
		// ViewportMode::Widget it also builds an offscreen scene target.
		// With `guiEnabled` false the GUI backend is never built (no font atlas, no GUI
		// pipelines) and no frame records a GUI pass; `mode` is forced to Fullscreen,
		// since without widgets there is nothing to display an offscreen scene.
		bool init(Window& window, const std::string& fontPath, float fontHeight,
		          const std::vector<float>& fontSizes, ViewportMode mode,
		          bool guiEnabled = true);

		// Whether this renderer has a GUI backend at all.
		bool guiEnabled() const { return mGuiEnabled; }
		void destroy();

		// The baked font atlas, so a window's Gui frontend can share its CPU metrics.
		const FontAtlas& fontAtlas() const { return mGuiRenderer.fontAtlas(); }

		// In Widget mode, the offscreen color texture the scene is rendered into (for a
		// Viewport widget to display); nullptr in Fullscreen mode.
		const Texture* sceneTexture() const;

		// The offscreen target's colour format and render pass. A C++ application that
		// records into a viewport builds its pipelines against these; both outlive every
		// resize, so it builds them once.
		VkFormat sceneColorFormat() const { return mSceneColorFormat; }
		VkRenderPass sceneRenderPass() const;

		// Widget mode: (re)size the offscreen scene target to `extent` (the Viewport
		// widget's size), so the scene is rendered at the viewport's resolution/aspect.
		// A no-op if the size is unchanged.
		void ensureSceneTarget(VkExtent2D extent);

		// Renders one frame into the current target and presents it to the window.
		void drawWindow(Window& window);

		FrameBuffer* target() const { return mTarget; }

		void waitIdle();

		// Releases the frame resources held for a window that is going away. Must be
		// called before the window is destroyed and only once the GPU is idle, since the
		// fences and semaphores may still be in use by a frame in flight.
		void forgetWindow(const Window* window);

		// Releases the GUI's descriptor set for a texture that is about to be destroyed.
		// Safe to call for a texture that was never drawn with.
		void forgetTexture(const Texture* texture) {
			mGuiRenderer.forgetTexture(texture);
		}

		// Culling belongs to the pass that does it; these forward to it so the
		// application still has one place to ask.
		using CullStats = ForwardPass::CullStats;
		const CullStats& cullStats() const { return mForward.cullStats(); }
		void setFrustumCulling(bool enabled) { mForward.setFrustumCulling(enabled); }
		bool frustumCulling() const { return mForward.frustumCulling(); }

		// Widget mode only: rasterise the GUI into a cached layer and composite it, so an
		// animating scene stops re-drawing an unchanged GUI every frame. Off by default —
		// see AppConfig::cacheGuiLayer.
		void setGuiLayerCaching(bool enabled) { mGuiLayerCaching = enabled; }

		// A material bound to this renderer's forward pipeline. Meshes added to the
		// Scene need a material; the camera set and per-object transform are supplied
		// by the renderer, so this material carries no descriptor sets of its own.
		Material forwardMaterial();
		// Same, with a descriptor set holding its maps. Missing maps fall back to the
		// engine's neutral defaults, so the shader never has to branch on "has texture".
		Material forwardMaterial(const MaterialTextures& textures);

		// Returns a material's descriptor set so the next material can have it. Pair it
		// with waitIdle(): a frame in flight may still be bound to it.
		//
		// A Material is a value, so copies of it share one set. This releases that set,
		// which leaves every copy describing nothing — call it once, when the last of them
		// is finished with. It is safe on a material that has already been released.
		void releaseMaterial(Material& material);

	private:
		// The pool is shared: command buffers are only ever recorded from the render
		// thread, which is what a pool's thread affinity actually constrains.
		VkCommandPool mCommandPool = VK_NULL_HANDLE;

		// Everything a window needs to have frames of its own in flight.
		//
		// These cannot be shared between windows. Two windows drawn in one pass would
		// otherwise wait on the same fence, acquire into the same semaphore and record
		// into the same command buffer while it was still executing — a semaphore
		// signalled by one acquire and waited on by two submissions is not a race that
		// shows up as a clean failure, it shows up as intermittent corruption.
		struct WindowFrames {
			// One frame in flight, whole.
			//
			// This was seven parallel vectors at its worst — command buffers, semaphores,
			// fences, camera uniforms, camera sets, object buffers, object capacities —
			// every one of them indexed by the same number, with nothing but convention
			// keeping them in step. Convention is exactly what failed: the buffers were
			// indexed by frame while each window advanced its own counter, so two windows
			// shared them, and the parallel layout is what made that invisible. A frame is
			// now an object, and an object cannot be half-indexed.
			struct Frame {
				VkCommandBuffer command        = VK_NULL_HANDLE;
				VkSemaphore     imageAvailable = VK_NULL_HANDLE;
				VkFence         fence          = VK_NULL_HANDLE;
				SceneFrame      scene;          // set 0: camera, lights, object transforms
			};

			// Per window, not just per frame in flight. The scene data was shared once, on
			// the reasoning that only a window rendering a 3D scene of its own could
			// notice — and that reasoning was wrong twice over. It made two windows showing
			// different views overwrite each other, and the identical mistake in the GUI's
			// geometry buffers hit *interface-only* windows, which the same argument had
			// declared safe. Anything indexed by frame alone is shared between windows,
			// whatever it holds.
			std::vector<Frame> frames;
			uint32_t           currentFrame = 0;

			Frame&       operator[](uint32_t i)       { return frames[i]; }
			const Frame& operator[](uint32_t i) const { return frames[i]; }
		};
		// Created the first time a window is drawn, so nothing has to be told in advance
		// which windows will exist.
		std::unordered_map<const Window*, WindowFrames> mWindowFrames;
		WindowFrames* framesFor(const Window* window);
		bool createFrameSceneResources(WindowFrames& frames);
		// Destroys everything a window's frames own and empties them. The single place
		// that knows what a Frame holds, so the half-built ones a failed framesFor() has
		// to throw away are released by the same code that releases a finished window's —
		// which is what those failure paths used to get wrong, since erasing the map entry
		// frees the buffers but leaves every raw Vulkan handle behind.
		void releaseFrames(WindowFrames& frames);
		void destroyWindowFrames();

		// The target the renderer draws into. Borrowed, not owned — by default it points
		// at the window's surface FrameBuffer, which the window builds and rebuilds.
		FrameBuffer* mTarget = nullptr;
		// Whether an application redirected the target. Without an override each window
		// draws into its own surface framebuffer, which is what lets one renderer serve
		// several windows; with one, the redirect wins and stays put.

		// The shape of set 0 and the pool the per-window sets come from. The buffers and
		// sets themselves live in WindowFrames; only what every window shares is here.
		VkDescriptorSetLayout        mCameraSetLayout = VK_NULL_HANDLE;
		DescriptorAllocator          mDescriptorPool;

		// Staging for the per-object upload. Genuinely shared, unlike the GPU buffer it
		// feeds: it is filled and uploaded within a single call and never read across
		// windows or frames, so one of them keeping its capacity is all it is for.
		std::vector<ObjectData> mObjectScratch;

		// Set 1: what a material binds. Owns its layout, its pool and the neutral
		// fallbacks — none of it per frame or per window, which is why it is not here.
		MaterialResources mMaterials;

		// The two techniques. Each owns its own pipeline, its own shaders and its own walk
		// over the scene; what is left here is the frame loop and the targets they draw
		// into. A third — deferred, batched — is written beside these rather than inside
		// this class.
		ShadowPass  mShadowPass;
		ForwardPass mForward;

		// Draws the window's Gui — as an overlay in Fullscreen mode, or the whole surface
		// pass (with the scene shown by a Viewport widget) in Widget mode.
		GuiRenderer mGuiRenderer;

		// Widget mode: the scene is rendered here, then sampled by a Viewport widget.
		ViewportMode mViewportMode = ViewportMode::Fullscreen;
		bool         mGuiEnabled = true;
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

		// The window/frame pair being drawn is passed, never stashed. It was a member once
		// (`mCurrentFrame`), which is how the buffers it indexed came to be shared without
		// anyone deciding they should be: a helper that reads an index it was not given
		// cannot be checked at the call site.
		// `frameIndex` is still needed alongside the frame itself: GuiRenderer keeps its
		// own per-window geometry buffers and indexes them by it.
		void recordFrame(VkCommandBuffer cmd, Window& window, uint32_t imageIndex,
		                 WindowFrames::Frame& frame, uint32_t frameIndex);
	};
}
