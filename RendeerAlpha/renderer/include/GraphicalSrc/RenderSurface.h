#pragma once
#include <vulkan/vulkan.h>
#include <GraphicalObjects/DrawList.h>
#include <cstdint>

namespace RDA {
	class Swapchain;
	class FrameBuffer;

	// What the renderer draws into, and what it is asked to draw there.
	//
	// The renderer used to take a Window and read what it needed out of it: its swapchain,
	// framebuffer and size, and its GUI's draw list. That put the renderer above the class
	// that owns the GUI, the OS window and the platform hosts -- the one place the layers
	// still pointed the wrong way. It takes this instead. A window is one, which is the only
	// kind there is today, and the renderer no longer knows what a window is.
	//
	// The names are the ones Window already had, so a window implements this by declaring
	// what it always declared, and nothing that calls them changed.
	class RenderSurface {
	public:
		virtual ~RenderSurface() = default;

		// ---- presentation ----------------------------------------------------------------
		virtual Swapchain&   getSwapchain() = 0;
		// The default render target -- a surface FrameBuffer over the swapchain.
		virtual FrameBuffer& getFrameBuffer() = 0;
		// The last size seen, in pixels.
		virtual VkExtent2D   cachedExtent() const = 0;
		virtual bool         wasResized() const = 0;
		// Rebuilds the swapchain at the current size. False when there is nothing to size it
		// to -- a minimised window.
		virtual bool         recreateSwapchain() = 0;

		// ---- what the interface handed over this frame ---------------------------------
		virtual const GuiDrawData& guiDrawData() const = 0;
		// Bumped whenever the draw list changed, so an unchanged one can be reused.
		virtual uint64_t           guiDrawVersion() const = 0;
		// What the surface starts the frame at, as R8G8B8A8.
		virtual uint32_t           guiBackground() const = 0;
		// Where the viewport that shows the scene sits in the interface, in layout units: the
		// hole a cached interface layer leaves for the live scene to show through.
		virtual Rect               guiViewportRect() const = 0;
	};
}
