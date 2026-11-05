#pragma once
#include <Core/Datatypes.h>
#include <Core/Rect.h>
#include <cstdint>
#include <vector>

// What the interface hands the renderer each frame, and nothing else: rectangles in layout
// units, and the triangles to draw. The renderer's GUI pass includes this rather than the
// whole GUI -- it draws a list, and has no business knowing about widgets, docking or
// themes to do it.
namespace RDA {
	class Texture;

	struct GuiVertex {
		glm::vec2 pos;
		glm::vec2 uv;
		uint32_t  color; // R8G8B8A8_UNORM, see rgba()
	};

	// A run of indices sharing one clip rect (x0, y0, x1, y1) in pixels and one texture.
	// texture == nullptr means the font atlas (solid quads + text); non-null is an image
	// (e.g. the scene rendered by a Viewport widget), drawn with the image pipeline.
	struct GuiDrawCmd {
		glm::vec4      clip;
		uint32_t       indexOffset;
		uint32_t       indexCount;
		const Texture* texture = nullptr;
	};

	struct GuiDrawData {
		std::vector<GuiVertex>  vertices;
		std::vector<uint16_t>   indices;
		std::vector<GuiDrawCmd> commands;
		void clear() { vertices.clear(); indices.clear(); commands.clear(); }
	};
}
