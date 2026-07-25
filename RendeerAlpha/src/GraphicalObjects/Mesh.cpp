#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalObjects/Mesh.h>

namespace RDA {

	bool Mesh::upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
		if (vertices.empty() || indices.empty()) {
			return false;
		}

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
