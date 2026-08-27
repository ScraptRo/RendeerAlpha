#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/FrameBuffer.h>
#include <GraphicalSrc/GraphicsPipeline.h>

namespace RDA {
	struct DirectionalLight;
	class Scene;

	// The scene rendered from the sun's point of view, depth only.
	//
	// Its own class rather than three more members on the renderer: it owns a render
	// target, a pipeline built against that target, and a pass that walks the scene — the
	// same shape as any other technique, and the thing a second shadow technique (cascades,
	// a point-light cube map) would be written beside rather than inside.
	//
	// It reads set 0 for the per-object transforms, so it needs the scene set layout to
	// build against and the frame's set to record with. It writes nothing else: the map is
	// rebuilt at the start of a frame and sampled later in that same frame, so it is shared
	// between windows on purpose.
	class ShadowPass {
	public:
		bool init(VkDescriptorSetLayout sceneSetLayout);
		void destroy();
		bool isValid() const { return mTarget.isValid() && mPipeline.isValid(); }

		// The recorded depth, ready to sample. Bound into every window's set 0.
		const Texture& depthTexture() const { return mTarget.depthTexture(); }

		// The sun's view-projection: orthographic, because a directional light's rays are
		// parallel. The light is placed far enough back along its own direction to cover
		// the shadowed area, and looks along it.
		//
		// A pure function of the sun rather than a member computed by one caller and read
		// by another. Both the scene uniform and this pass need it, and deriving it twice
		// costs two matrix multiplies — much less than an ordering rule that says the
		// uniform must be written before the pass is recorded or the shadows lag a frame.
		static glm::mat4 viewProjectionFor(const DirectionalLight& sun);

		// Records the depth pass. `sceneSet` is the frame's set 0, which supplies the
		// object transforms the vertex shader indexes.
		void record(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
		            const glm::mat4& lightViewProjection, const Scene& scene);

		static constexpr uint32_t kSize = 2048;

	private:
		FrameBuffer      mTarget;
		GraphicsPipeline mPipeline;
	};
}
