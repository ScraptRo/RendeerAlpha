#pragma once
#include <Core/Framework.h>
#include <vendor/RDA_Library/stack_list.h>
#include <vendor/RDA_Library/obj_ref.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Vulkan clip space is [0, 1], not [-1, 1]
#include <glm/glm.hpp>
#include <array>

namespace RDA {

	// How the 3D scene is presented.
	enum class ViewportMode {
		Fullscreen, // scene renders straight to the window surface; the GUI overlays it
		Widget,     // scene renders to an offscreen target shown by a Viewport widget
	};

	// When the engine produces a frame.
	enum class RedrawMode {
		// Render and present every loop iteration. What a game wants: the scene is
		// assumed to change constantly.
		Continuous,

		// Render only when something actually changed — the GUI's geometry differs from
		// the last frame, the window was resized, or the application asked for a frame
		// with rendeerRequestRedraw(). Otherwise the loop skips rendering entirely and
		// the window keeps showing the last presented image, which costs no GPU at all.
		// The event pump also idles instead of spinning, so the CPU drops with it.
		//
		// The GUI is tracked automatically; *scene* animation is not visible to the
		// engine (in Widget mode a spinning cube leaves the GUI's geometry untouched),
		// so an app that animates its scene must call rendeerRequestRedraw() while it
		// is animating.
		OnDemand,
	};

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
		bool vsync = true; // FIFO present (cap to refresh) vs. Mailbox (uncapped, burns GPU)
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
