#pragma once
#include <vulkan/vulkan.h>
#include <Core/Datatypes.h>
#include <array>
#include <cstddef>

namespace RDA {

	// The standard interleaved vertex the mesh and pipeline layer use, and how Vulkan reads
	// one. Here rather than in Core's shared types because describing it to a pipeline is
	// Vulkan's business, and Core's types are read by code that never sees the GPU.
	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;

		static VkVertexInputBindingDescription getBindingDescription() {
			VkVertexInputBindingDescription binding{};
			binding.binding = 0;
			binding.stride = sizeof(Vertex);
			binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return binding;
		}

		static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
			std::array<VkVertexInputAttributeDescription, 3> attributes{};

			attributes[0].binding = 0;
			attributes[0].location = 0;
			attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributes[0].offset = offsetof(Vertex, position);

			attributes[1].binding = 0;
			attributes[1].location = 1;
			attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributes[1].offset = offsetof(Vertex, normal);

			attributes[2].binding = 0;
			attributes[2].location = 2;
			attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
			attributes[2].offset = offsetof(Vertex, uv);

			return attributes;
		}
	};
}
