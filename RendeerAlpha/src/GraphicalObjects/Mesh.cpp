#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalObjects/Mesh.h>
#include <algorithm>
#include <cmath>

namespace RDA {

	bool Mesh::upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
		if (vertices.empty() || indices.empty()) {
			return false;
		}

		// Bounding sphere from the axis-aligned extents: the box centre, and the distance
		// to the furthest vertex from it. Taking the true furthest distance rather than
		// half the diagonal keeps the sphere tight for geometry that does not fill its box.
		glm::vec3 minCorner(vertices[0].position);
		glm::vec3 maxCorner(vertices[0].position);
		for (const Vertex& v : vertices) {
			minCorner = glm::min(minCorner, v.position);
			maxCorner = glm::max(maxCorner, v.position);
		}
		mBoundsCenter = (minCorner + maxCorner) * 0.5f;
		float radiusSquared = 0.0f;
		for (const Vertex& v : vertices) {
			const glm::vec3 offset = v.position - mBoundsCenter;
			radiusSquared = (glm::max)(radiusSquared, glm::dot(offset, offset));
		}
		mBoundsRadius = std::sqrt(radiusSquared);

		const VkDeviceSize vertexSize = sizeof(Vertex) * vertices.size();
		const VkDeviceSize indexSize  = sizeof(uint32_t) * indices.size();

		if (!mVertexBuffer.create(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, MemoryResidence::GpuOnly)) {
			return false;
		}
		if (!mIndexBuffer.create(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, MemoryResidence::GpuOnly)) {
			return false;
		}

		if (!mVertexBuffer.upload(vertices.data(), vertexSize)) return false;
		if (!mIndexBuffer.upload(indices.data(), indexSize))   return false;

		mIndexCount = static_cast<uint32_t>(indices.size());
		return true;
	}

	void Mesh::recordDraw(VkCommandBuffer cmd) const {
		if (!isValid()) return;

		VkBuffer vertexBuffers[] = { mVertexBuffer.handle() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, mIndexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, mIndexCount, 1, 0, 0, 0);
	}
}
