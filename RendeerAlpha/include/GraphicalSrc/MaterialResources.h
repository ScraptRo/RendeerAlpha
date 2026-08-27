#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/Descriptors.h>
#include <GraphicalObjects/Material.h>
#include <GraphicalObjects/Texture.h>

namespace RDA {

	// Set 1: what a material binds, and the stand-ins for what it leaves unset.
	//
	// Separated from the renderer because none of it is per frame or per window. The
	// layout, the pool and the two fallback images are built once and live for the run,
	// and a material's descriptor set is baked when the material is created — so this is a
	// factory with three constants attached, not part of the frame loop that surrounded it.
	class MaterialResources {
	public:
		bool init();
		void destroy();

		VkDescriptorSetLayout layout() const { return mLayout; }

		// A set holding these three maps, with the neutral defaults standing in for any
		// that are missing — so the shader never has to branch on "has texture".
		// VK_NULL_HANDLE if the pool cannot give one out.
		VkDescriptorSet allocateSet(const MaterialTextures& textures);

		// Takes a set back for the next material. The caller must have waited for the GPU:
		// a set still bound by a frame in flight is being read, and handing it to the next
		// allocateSet() would rewrite it underneath that frame.
		void releaseSet(VkDescriptorSet set);

	private:
		VkDescriptorSetLayout mLayout = VK_NULL_HANDLE;
		DescriptorAllocator   mPool;

		// 1x1 stand-ins. White is the identity for albedo and for metallic-roughness
		// (both are multiplied); (0.5, 0.5, 1) decodes to a tangent normal of (0, 0, 1),
		// which leaves the interpolated vertex normal untouched.
		Texture mWhite;
		Texture mFlatNormal;
	};
}
