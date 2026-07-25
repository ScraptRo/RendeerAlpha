#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/FontAtlas.h>
#include <GraphicalSrc/GraphicsPipeline.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <GraphicalSrc/Descriptors.h>
#include <GraphicalObjects/Gui.h>
#include <string>
#include <vector>

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

		void record(VkCommandBuffer cmd, const GuiDrawData& data,
		            VkExtent2D targetExtent, uint32_t frameIndex);

		const FontAtlas& fontAtlas() const { return mFont; }

	private:
		struct DynamicBuffers {
			MemoryBuffer vertices;
			MemoryBuffer indices;
			VkDeviceSize vertexCapacity = 0;
			VkDeviceSize indexCapacity = 0;
		};

		bool buildPipeline(VkRenderPass renderPass);

		FontAtlas             mFont;
		GraphicsPipeline      mPipeline;
		VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
		DescriptorAllocator   mPool;
		VkDescriptorSet       mAtlasSet = VK_NULL_HANDLE;
		std::vector<DynamicBuffers> mFrames;
	};
}
