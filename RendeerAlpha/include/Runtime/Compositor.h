#pragma once
#include <Core/BuildMode.h>
#include <Runtime/Host.h>
#include <GraphicalObjects/GuiTypes.h>
#include <vector>

// Placing client surfaces in the runtime's window and getting their geometry into its
// coordinate space.
//
// Each client draws as though it owned the origin, because it has no idea where it sits.
// The compositor is what turns that into a shared window: it gives every surface a rect,
// translates the surface's vertices into it, and intersects every clip rect with the
// surface bounds so a client cannot paint outside the area it was given. That last part
// is not decoration — without it any client could scribble over its neighbours.
//
// Indices stay per surface rather than being merged into one buffer. They are 16-bit, so
// concatenating several clients' geometry would overflow the index space as soon as the
// total passed 65535 vertices; one recording call per surface avoids that entirely and
// costs a draw call, not a copy.
//
//     compositor.autoPlace(host.surfaces(), viewport);
//     for (const auto& layer : compositor.compose(host.surfaces()))
//         guiRenderer.record(cmd, layer.draw, extent, frame, layer.version);
namespace RDA::Runtime {

	class RuntimeCompositor {
	public:
		// Where one client's surface lives in the host window.
		struct Placement {
			uint32_t clientId = 0;
			uint32_t surfaceId = 0;
			Rect     rect{};
			bool     visible = true;
		};

		// One surface's geometry, already in host-window space and clipped to its rect.
		struct Layer {
			Placement   placement;
			GuiDrawData draw;
			uint64_t    version = 0; // the client's draw version, for the renderer's cache
		};

		void place(uint32_t clientId, uint32_t surfaceId, const Rect& rect);
		void setVisible(uint32_t clientId, uint32_t surfaceId, bool visible);
		void forget(uint32_t clientId, uint32_t surfaceId);
		const Placement* find(uint32_t clientId, uint32_t surfaceId) const;
		const std::vector<Placement>& placements() const { return mPlacements; }

		// Gives a rect to any of the host's surfaces that has not been placed, tiling
		// them left to right, and drops placements whose surface has gone. A real window
		// manager replaces this; it exists so a surface is never invisible by default.
		void autoPlace(const std::vector<RuntimeHost::Surface>& surfaces, glm::vec2 viewport);

		// Translates and clips every placed surface. Back to front, so the last placed
		// draws on top. The result is owned by the compositor and reused between frames,
		// so a steady frame rate does not reallocate.
		const std::vector<Layer>& compose(const std::vector<RuntimeHost::Surface>& surfaces);

		// ---- input routing ----
		// The topmost visible surface containing `point`, or null.
		const Placement* surfaceAt(glm::vec2 point) const;
		// A host-window point in that surface's own coordinates, which is the space the
		// client drew in and therefore the only one its hit-testing understands.
		static glm::vec2 toLocal(const Placement& placement, glm::vec2 point) {
			return { point.x - placement.rect.x, point.y - placement.rect.y };
		}

		// Raises a surface to the top of the draw order.
		void raise(uint32_t clientId, uint32_t surfaceId);

	private:
		Placement* mutableFind(uint32_t clientId, uint32_t surfaceId);

		std::vector<Placement> mPlacements;
		std::vector<Layer> mLayers;
	};
}
