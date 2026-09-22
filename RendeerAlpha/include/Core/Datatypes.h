#pragma once
#include <cstdint>
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

	// "wherever the platform would have put it" -- which is not the same as 0,0, and is
	// the only sensible default for a window nobody has placed.
	inline constexpr int kWindowUnplaced = INT32_MIN;

	// How the OS dresses a window: everything about it that is not its size.
	//
	// Its own struct because the same set is stated twice -- once by an application
	// before the engine starts, and once by the window that ends up carrying it -- and
	// two lists that have to agree is one list too many.
	//
	// Most of it is read when the window is created, because that is when the platform
	// decides. `decorated`, `resizable`, `alwaysOnTop`, `opacity`, the bounds, the
	// position and the icon can all be changed afterwards as well; `transparent` cannot,
	// anywhere.
	struct WindowStyle {
		// The OS frame: title bar, border, the three buttons. Off means the layout draws
		// its own -- and something in it needs `dragWindow`, or the window cannot be
		// moved at all.
		bool decorated = true;
		bool resizable = true;
		bool maximized = false;       // opens filling the work area
		bool fullscreen = false;      // opens covering the monitor
		bool alwaysOnTop = false;
		// A framebuffer with a real alpha channel, so a clear colour that is not opaque
		// lets the desktop through. Decided at creation and nowhere else, and ignored
		// where the platform or the compositor will not do it.
		bool transparent = false;
		float opacity = 1.0f;         // the whole window, frame included

		// Bounds the reader cannot drag past. Zero on an axis means no bound there.
		unsigned int minWidth = 0, minHeight = 0, maxWidth = 0, maxHeight = 0;

		// Where its top-left corner opens, in screen coordinates. Left unplaced, the
		// platform chooses -- usually centred on the active monitor.
		int x = kWindowUnplaced, y = kWindowUnplaced;

		// The picture the OS shows for it: the title bar on Windows and Linux, the
		// alt-tab card, the taskbar. A .png, or an .svg -- which is rasterised at every
		// size an OS picks from, so one file covers all of them.
		//
		// Not the executable's icon. That one is a resource inside the binary, put there
		// when the program is built rather than when it runs.
		std::string icon;
	};

	struct WindowInfo {
		std::string name;
		unsigned int Width;
		unsigned int Height;
		bool vsync = true; // FIFO present (cap to refresh) vs. Mailbox (uncapped, burns GPU)

		WindowStyle style;   // the frame, the icon, the bounds -- see WindowStyle
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
