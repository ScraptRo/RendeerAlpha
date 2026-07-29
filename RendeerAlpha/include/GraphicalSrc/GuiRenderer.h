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

	// The GUI backend: turns a Gui's draw list into Vulkan. Owns the font atlas (its
	// GPU texture), the UI pipeline (orthographic, alpha-blended, depth off), and a
	// dynamic vertex/index buffer per frame-in-flight. It records into whatever render
	// pass it was built against — for the overlay case, the window surface's, drawn
	// after the scene in the same pass.
	class GuiRenderer {
	public:
		bool init(VkRenderPass targetRenderPass, const std::string& fontPath,
		          float fontHeight, uint32_t framesInFlight);
		void destroy();

		// Recycles this frame's transient image descriptor sets. Call once per frame,
		// before any record*() call, since a frame may record more than once (the cached
		// GUI layer and then the composite).
		void beginFrame(uint32_t frameIndex);

		// `drawVersion` is Gui::drawVersion(): the upload is skipped when this frame's
		// buffers already hold that exact geometry (see DynamicBuffers::uploadedVersion).
		// Commands drawn with `skipTexture` are omitted — used when baking the static GUI
		// layer, so the live scene quad is left out and its area stays transparent.
		// `intoLayer` selects the pipelines built against the cached-layer render pass
		// rather than the surface one — a pipeline may only be used with the pass it was
		// built against.
		void record(VkCommandBuffer cmd, const GuiDrawData& data,
		            VkExtent2D targetExtent, uint32_t frameIndex, uint64_t drawVersion,
		            const Texture* skipTexture = nullptr, bool intoLayer = false);

		// Builds the GUI pipelines for the cached-layer render pass. Called by the
		// renderer once that target exists; until then only the surface set is available.
		bool buildLayerPipelines(VkRenderPass layerPass);

		// Draws the live scene filling `sceneRect`, then a pre-rendered GUI layer over the
		// whole target. This is what replaces re-rasterising the entire GUI every frame:
		// two quads instead of every panel, glyph and border.
		void recordComposite(VkCommandBuffer cmd, VkExtent2D targetExtent, uint32_t frameIndex,
		                     const Texture* sceneTexture, const Rect& sceneRect,
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
		VkDescriptorSet imageSetFor(const Texture* texture, uint32_t frameIndex);

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
		std::vector<DynamicBuffers> mFrames;

		// Per-frame-in-flight pools for one-off image textures (Viewport scene texture,
		// etc.), reset each frame so nothing leaks; a per-frame cache avoids re-allocating
		// for a texture used by several commands.
		std::vector<VkDescriptorPool> mImagePools;
		std::unordered_map<const Texture*, VkDescriptorSet> mImageSets;
	};
}
