#pragma once
#include <Core/Framework.h>
#include <vendor/RDA_Library/stack_list.h>
#include <vendor/RDA_Library/obj_ref.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Vulkan clip space is [0, 1], not [-1, 1]
#include <glm/glm.hpp>
#include <array>

namespace RDA {

	struct AppInfo {
		std::string name; // Application Name
		uint32_t appVersion;
		// Window dependent means that the application needs a window that will be used for the entire application lifetime
		// This will initialize a window and set it as the value for getMainWindow()
		bool windowDependent = true;
	};

	struct WindowInfo {
		std::string name;
		unsigned int Width;
		unsigned int Height;
	};

	// Standard interleaved vertex used by the mesh / pipeline layer.
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
