#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/FontAtlas.h>
#include <GraphicalSrc/GraphicsPipeline.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <GraphicalSrc/Descriptors.h>
#include <GraphicalObjects/Gui.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace RDA {

	class Window;

	// The GUI backend: turns a Gui's draw list into Vulkan. Owns the font atlas (its
	// GPU texture), the UI pipeline (orthographic, alpha-blended, depth off), and a
	// dynamic vertex/index buffer per window per frame-in-flight. It records into
	// whatever render pass it was built against — for the overlay case, the window
	// surface's, drawn after the scene in the same pass.
	//
	// Everything expensive is shared between windows (the atlas, the pipelines, the
	// image descriptor sets); only the geometry buffers are per window, because those
	// are the ones a frame in flight is still reading. `owner` is what separates them,
	// and it is the window being drawn.
	class GuiRenderer {
	public:
		bool init(VkRenderPass targetRenderPass, const std::string& fontPath,
		          float fontHeight, uint32_t framesInFlight);
		void destroy();

		// Prepares `owner`'s buffers for this frame. Call once per window per frame,
		// before any record*() call, since a frame may record more than once (the cached
		// GUI layer and then the composite).
		void beginFrame(const Window* owner, uint32_t frameIndex);

		// Releases the geometry buffers held for a window that is going away. Must be
		// called before the window is destroyed and only once the GPU is idle, since a
		// frame in flight may still be reading them.
		void forgetWindow(const Window* owner);

		// Releases the descriptor set held for a texture. Must be called before the
		// texture itself goes, and only once the GPU is idle. Sets are cached for as long
		// as their texture lives rather than rebuilt each frame: several windows share one
		// GuiRenderer and each advances its own frame counter, so there is no single
		// moment at which recycling them is safe for all of them.
		void forgetTexture(const Texture* texture);

		// `drawVersion` is Gui::drawVersion(): the upload is skipped when this frame's
		// buffers already hold that exact geometry (see DynamicBuffers::uploadedVersion).
		// Commands drawn with `skipTexture` are omitted — used when baking the static GUI
		// layer, so the live scene quad is left out and its area stays transparent.
		// `intoLayer` selects the pipelines built against the cached-layer render pass
		// rather than the surface one — a pipeline may only be used with the pass it was
		// built against.
		void record(VkCommandBuffer cmd, const Window* owner, const GuiDrawData& data,
		            VkExtent2D targetExtent, uint32_t frameIndex, uint64_t drawVersion,
		            const Texture* skipTexture = nullptr, bool intoLayer = false);

		// Builds the GUI pipelines for the cached-layer render pass. Called by the
		// renderer once that target exists; until then only the surface set is available.
		bool buildLayerPipelines(VkRenderPass layerPass);

		// Draws the live scene filling `sceneRect`, then a pre-rendered GUI layer over the
		// whole target. This is what replaces re-rasterising the entire GUI every frame:
		// two quads instead of every panel, glyph and border.
		void recordComposite(VkCommandBuffer cmd, const Window* owner, VkExtent2D targetExtent,
		                     uint32_t frameIndex, const Texture* sceneTexture, const Rect& sceneRect,
		                     const Texture* layerTexture);

		const FontAtlas& fontAtlas() const { return mFont; }

	private:
		struct DynamicBuffers {
			MemoryBuffer vertices;
			MemoryBuffer indices;
			VkDeviceSize vertexCapacity = 0;
			VkDeviceSize indexCapacity = 0;
			// Draw version currently resident in these buffers. Each frame-in-flight has
			// its own copy, so this is tracked per frame rather than globally: an idle GUI
			// still uploads once per frame index before every one of them is up to date.
			uint64_t     uploadedVersion = 0;
			bool         hasUpload = false;
			// Two quads (scene + GUI layer) for recordComposite. Tiny and rewritten each
			// composite, so it never grows.
			MemoryBuffer compositeVertices;
			MemoryBuffer compositeIndices;
			bool         compositeCreated = false;
		};

		// Builds an atlas + image pipeline (and optionally the composite one) against a
		// given render pass. Pipelines are pass-specific, so each target needs its own.
		bool buildPipelineSet(VkRenderPass renderPass, GraphicsPipeline& atlas,
		                      GraphicsPipeline& image, GraphicsPipeline* composite);
		VkDescriptorSet imageSetFor(const Texture* texture);
		// This window's buffers for that frame index, created on first use so nothing has
		// to be told in advance which windows will exist. Null if the index is out of range.
		DynamicBuffers* buffersFor(const Window* owner, uint32_t frameIndex);

		FontAtlas             mFont;
		GraphicsPipeline      mAtlasPipeline; // font/coverage: samples R8 as alpha
		GraphicsPipeline      mImagePipeline; // image/color: samples RGBA directly
		// Composite: the cached GUI layer was built by blending into a transparent
		// target, so its colour is already multiplied by alpha and must be composited
		// with ONE / ONE_MINUS_SRC_ALPHA rather than the usual SRC_ALPHA.
		GraphicsPipeline      mCompositePipeline;
		// The same two pipelines, built against the cached GUI layer's render pass.
		GraphicsPipeline      mLayerAtlasPipeline;
		GraphicsPipeline      mLayerImagePipeline;
		bool                  mLayerPipelinesReady = false;
		VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
		DescriptorAllocator   mPool;
		VkDescriptorSet       mAtlasSet = VK_NULL_HANDLE;

		// Geometry buffers, per window and then per frame in flight.
		//
		// Indexing by frame alone is not enough: several windows draw through this one
		// GuiRenderer, each advancing its own frame counter, and they are drawn one after
		// another in the same loop iteration. Two windows sitting on frame 0 would then
		// share a buffer — the second window's upload landing on geometry the first has
		// already recorded and submitted, gated only by the first window's own fence. A
		// window whose geometry outgrew its buffer made it worse than wrong output:
		// MemoryBuffer::create() destroys before it allocates, so the grow freed a
		// VkBuffer an in-flight command buffer was still reading.
		uint32_t mFramesInFlight = 0;
		std::unordered_map<const Window*, std::vector<DynamicBuffers>> mWindowFrames;

		// One pool, not one per frame in flight. A per-frame pool has to be reset at the
		// start of a frame, and with several windows drawing from the same GuiRenderer
		// that reset would free sets another window's in-flight frame is still using.
		VkDescriptorPool mImagePool = VK_NULL_HANDLE;

		// A cached set, together with the identity of the image it was written against.
		//
		// The pointer alone is not that identity: a Texture belonging to a render target
		// keeps its address when the target is rebuilt at a new size, while the view and
		// sampler inside it are destroyed and replaced. Texture::revision() is the value
		// that does change, so it is what makes a stale entry detectable rather than a
		// silent use-after-free. Comparing the view and sampler instead would very nearly
		// work — but Vulkan may hand a recycled handle back at the same value, and then
		// the stale entry looks fresh.
		struct ImageSet {
			VkDescriptorSet set = VK_NULL_HANDLE;
			uint64_t        revision = 0;  // Texture::revision() when `set` was written
		};
		std::unordered_map<const Texture*, ImageSet> mImageSets;
	};
}
