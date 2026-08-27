#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/GraphicsPipeline.h>

namespace RDA {
	class Scene;

	// The lit pass: every scene item drawn once, through one pipeline, into whatever
	// target it was built against.
	//
	// A technique, sitting above GraphicsPipeline rather than inside the renderer — the
	// same shape as ShadowPass, and the shape a deferred or batched path would be written
	// in beside them. What the renderer keeps is the frame loop and the targets; what a
	// pass owns is its pipeline, its shaders and the walk over the scene.
	//
	// The flat draw loop inside is exactly what batching and instancing will later
	// replace, and replacing it is now a change to this file alone.
	class ForwardPass {
	public:
		// `target` is the render pass this will be used with — a pipeline is only valid
		// against the one it was built for, so a change of target means a rebuild.
		bool init(VkRenderPass target, VkDescriptorSetLayout sceneSetLayout,
		          VkDescriptorSetLayout materialSetLayout);
		void destroy();
		bool isValid() const { return mPipeline.isValid(); }

		// Materials bind against this, so they need it by address and it must outlive them.
		GraphicsPipeline& pipeline() { return mPipeline; }

		// Draws the scene with `sceneSet` bound at set 0. Assumes the caller has already
		// begun a render pass compatible with the one this was built against.
		void record(VkCommandBuffer cmd, VkDescriptorSet sceneSet, const Scene& scene);

		// What the last record() actually did. Useful for confirming a scene is being
		// culled rather than assuming it.
		struct CullStats {
			uint32_t submitted = 0; // items the application added to the scene
			uint32_t drawn = 0;     // items that survived culling
			uint32_t culled = 0;    // items rejected by the frustum
		};
		const CullStats& cullStats() const { return mCullStats; }

		// Skip draws whose bounds fall outside the camera frustum. On by default: it costs
		// six dot products per item and saves everything downstream of them.
		void setFrustumCulling(bool enabled) { mFrustumCulling = enabled; }
		bool frustumCulling() const { return mFrustumCulling; }

	private:
		GraphicsPipeline mPipeline;
		bool             mFrustumCulling = true;
		CullStats        mCullStats;
	};
}
