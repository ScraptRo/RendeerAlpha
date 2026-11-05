#include <GraphicalSrc/FrameSync.h>
#include <GraphicalSrc/DeviceHandler.h>

namespace RDA {

	namespace {
		// What a frame does for its own fence and an image for the fence of the frame drawing
		// into it. The result is not looked at, as it was not where this was written out by
		// hand: a lost device is reported by the submit that follows.
		void waitFor(VkDevice device, VkFence fence) {
			vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		}

		// Built into a local and handed back only on success, null otherwise: a failed create
		// leaves its handle undefined rather than null, and passing that to vkDestroy* would
		// be worse than the leak.
		VkSemaphore makeSemaphore(VkDevice device) {
			VkSemaphoreCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkSemaphore semaphore = VK_NULL_HANDLE;
			if (vkCreateSemaphore(device, &info, nullptr, &semaphore) != VK_SUCCESS) {
				return VK_NULL_HANDLE;
			}
			return semaphore;
		}

		VkFence makeFence(VkDevice device, bool signalled) {
			VkFenceCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			if (signalled) info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VkFence fence = VK_NULL_HANDLE;
			if (vkCreateFence(device, &info, nullptr, &fence) != VK_SUCCESS) return VK_NULL_HANDLE;
			return fence;
		}
	}

	bool FrameSync::create(uint32_t framesInFlight) {
		destroy();
		VkDevice device = getDevice();

		mFrames.assign(framesInFlight, Frame{});
		for (Frame& frame : mFrames) {
			// Each is stored the moment it exists, so a failure partway through leaves
			// every object that was made reachable by destroy() below.
			frame.imageAvailable = makeSemaphore(device);
			// Signalled, so frame 0 doesn't deadlock waiting on a submit that never happened.
			frame.fence = makeFence(device, true);
			if (frame.imageAvailable == VK_NULL_HANDLE || frame.fence == VK_NULL_HANDLE) {
				destroy();
				return false;
			}
		}
		return true;
	}

	void FrameSync::destroy() {
		VkDevice device = getDevice();
		if (device != VK_NULL_HANDLE) {
			for (Frame& frame : mFrames) {
				if (frame.imageAvailable != VK_NULL_HANDLE) {
					vkDestroySemaphore(device, frame.imageAvailable, nullptr);
				}
				if (frame.fence != VK_NULL_HANDLE) vkDestroyFence(device, frame.fence, nullptr);
			}
		}
		mFrames.clear();
		mCurrent = 0;
	}

	const FrameSync::Frame& FrameSync::acquire() {
		const Frame& current = mFrames[mCurrent];
		waitFor(getDevice(), current.fence);
		return current;
	}

	void FrameSync::resetFence() {
		vkResetFences(getDevice(), 1, &mFrames[mCurrent].fence);
	}

	void FrameSync::advance() {
		if (!mFrames.empty()) mCurrent = (mCurrent + 1) % static_cast<uint32_t>(mFrames.size());
	}

	bool PresentSync::create(uint32_t imageCount) {
		destroy();
		VkDevice device = getDevice();

		mRenderFinished.assign(imageCount, VK_NULL_HANDLE);
		mImageFence.assign(imageCount, VK_NULL_HANDLE);
		for (VkSemaphore& semaphore : mRenderFinished) {
			semaphore = makeSemaphore(device);
			if (semaphore == VK_NULL_HANDLE) {
				destroy();
				return false;
			}
		}
		return true;
	}

	void PresentSync::destroy() {
		VkDevice device = getDevice();
		if (device != VK_NULL_HANDLE) {
			for (VkSemaphore semaphore : mRenderFinished) {
				if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, semaphore, nullptr);
			}
		}
		mRenderFinished.clear();
		mImageFence.clear();
	}

	void PresentSync::claimImage(uint32_t image, VkFence fence) {
		VkFence& previous = mImageFence[image];
		if (previous != VK_NULL_HANDLE) waitFor(getDevice(), previous);
		previous = fence;
	}
}
