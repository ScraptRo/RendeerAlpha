#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <GraphicalSrc/Descriptors.h>
#include <GraphicalSrc/SceneBindings.h>
#include <vector>

namespace RDA {
	class Scene;
	class Texture;

	// Set 0 for one window, for one frame in flight: the scene uniform, the per-object
	// storage buffer it indexes, and the descriptor set binding them together.
	//
	// One object rather than four parallel vectors. It was `cameraUniforms[i]`,
	// `cameraSets[i]`, `objectBuffers[i]` and `objectCapacity[i]` — four containers that
	// had to be resized together, indexed together and destroyed together, with nothing
	// but convention saying so. Grouping them means the buffer, its capacity and the set
	// that points at it cannot be updated out of step, because updating them is one call.
	//
	// Per window as well as per frame: two windows drawing different views need their own,
	// and ensureObjectCapacity() *recreates* the storage buffer when a scene outgrows it,
	// which across windows would free memory another window's in-flight frame is reading.
	class SceneFrame {
	public:
		// Allocates the buffers and the set. `pool` and `layout` are shared by every
		// window; everything this holds is not.
		bool create(DescriptorAllocator& pool, VkDescriptorSetLayout layout);
		void destroy();

		VkDescriptorSet set() const { return mSet; }
		bool isValid() const { return mSet != VK_NULL_HANDLE; }

		// Binding 2: the shadow map. Written once, when the frame is built — the map is
		// shared between windows on purpose, since it is rebuilt at the start of a frame
		// and sampled later in that same frame.
		void bindShadowMap(const Texture& depth);

		// Binding 0. `lightViewProjection` is passed rather than derived so this stays
		// free of any technique: it is set 0's contents, not a decision about shadows.
		void updateCamera(const Scene& scene, const glm::mat4& lightViewProjection);

		// Binding 1. `scratch` is the caller's staging buffer, reused across frames and
		// windows so a steady frame rate does not allocate — it never outlives the call,
		// which is why sharing it is safe when sharing the GPU buffer would not be.
		void updateObjects(const Scene& scene, std::vector<ObjectData>& scratch);

	private:
		// Grows in powers of two, and repoints the set at the new buffer. Only safe once
		// the caller has waited on this frame's fence.
		bool ensureObjectCapacity(uint32_t count);

		MemoryBuffer    mUniform;
		MemoryBuffer    mObjects;
		uint32_t        mObjectCapacity = 0;
		VkDescriptorSet mSet = VK_NULL_HANDLE;
		// Where the set came from, so destroy() can hand it back for the next window
		// instead of leaving it stranded in a pool until the renderer shuts down. Borrowed
		// — the allocator and the layout both outlive every frame that uses them.
		DescriptorAllocator*  mPool = nullptr;
		VkDescriptorSetLayout mLayout = VK_NULL_HANDLE;
	};
}
