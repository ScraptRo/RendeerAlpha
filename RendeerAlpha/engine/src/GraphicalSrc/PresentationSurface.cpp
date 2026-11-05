#include <GraphicalSrc/PresentationSurface.h>
#include <GraphicalSrc/DeviceHandler.h>

namespace RDA {

	PresentationSurface::Created PresentationSurface::create(VkInstance instance, GLFWwindow* window,
	                                                         VkExtent2D extent, bool vsync) {
		if (!mSurface.Create(instance, window)) return Created::NoSurface;
		mSwapchain.setVsync(vsync); // FIFO vs. Mailbox; honored on recreate too
		if (!mSwapchain.create(mSurface.Get(), extent)) return Created::Deferred;
		if (!mFrameBuffer.createSurface(mSwapchain)) return Created::NoFrameBuffer;
		return Created::Ready;
	}

	bool PresentationSurface::recreate(VkExtent2D extent) {
		// Minimized: there is nothing to size a swapchain to. Bail out and let the
		// caller try again on a later frame, once the event pump has cached a
		// non-zero size.
		if (extent.width == 0 || extent.height == 0) {
			return false;
		}

		// And the cache is not enough. It is refreshed by the event pump; a window
		// minimised *after* the last poll and before the present that notices still
		// has a non-zero cache while its surface already reports nothing. Building
		// from that is VUID-VkSwapchainCreateInfoKHR-imageExtent-01689, followed by a
		// 0x0 depth attachment that fails to allocate -- three errors in the log for
		// one minimise. Asked here, before the device is stalled and before anything
		// is destroyed.
		if (!mSwapchain.surfaceIsDrawable(mSurface.Get(), extent)) {
			return false;
		}

		vkDeviceWaitIdle(getDevice());
		// Checks the extent again itself, and keeps the swapchain it has if the window
		// went away in between -- so this stays correct however it is reached.
		if (!mSwapchain.recreate(mSurface.Get(), extent)) {
			return false;
		}
		mFrameBuffer.recreateSurface(mSwapchain);
		return true;
	}

	void PresentationSurface::destroy() {
		VkDevice device = getDevice();
		if (device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(device);
		}
		mFrameBuffer.destroy();
		mSwapchain.destroy();
		mSurface.Destroy();
	}
}
