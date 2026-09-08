#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalSrc/MemoryBuffer.h>

namespace RDA{

	// Which path a mesh is rendered through.
	//
	// This is about blending, not movement: both paths transform via the same
	// camera uniform + model push constant, and the G-buffer is rebuilt from
	// scratch every frame, so moving a mesh costs the same either way. What
	// deferred cannot do is blend — the G-buffer only holds one surface per pixel.
	enum class MeshKind {
		Opaque,      // deferred: writes the G-buffer, lit once in the lighting subpass
		Transparent, // forward: lit in place and blended over the lit image
	};

	// A renderable mesh: GPU-resident vertex + index buffers.
	// Designed to be owned through obj_ref<Mesh> (the CPU-side handle layer),
	// so the buffers free themselves once the last reference drops.
	class Mesh
	{
	public:
		Mesh() = default;

		// Uploads geometry into device-local buffers (via staging).
		bool upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

		// Binds the buffers and issues an indexed draw into an already-begun render pass.
		void recordDraw(VkCommandBuffer cmd) const;

		uint32_t indexCount() const { return mIndexCount; }
		bool     isValid()    const { return mVertexBuffer.isValid() && mIndexBuffer.isValid(); }

		// Local-space bounds, measured during upload(). A sphere rather than a box
		// because it survives rotation without being recomputed: culling only has to
		// move the centre and scale the radius, where a box would need its eight corners
		// transformed every frame.
		const glm::vec3& boundsCenter() const { return mBoundsCenter; }
		float            boundsRadius() const { return mBoundsRadius; }

		// Picks the render path. The mesh data itself is identical either way, so
		// this can be changed at any time.
		MeshKind kind() const { return mKind; }
		void     setKind(MeshKind kind) { mKind = kind; }

	private:
		MemoryBuffer mVertexBuffer;
		MemoryBuffer mIndexBuffer;
		uint32_t     mIndexCount = 0;
		MeshKind     mKind = MeshKind::Opaque;
		glm::vec3    mBoundsCenter{ 0.0f };
		float        mBoundsRadius = 0.0f;
	};
}
