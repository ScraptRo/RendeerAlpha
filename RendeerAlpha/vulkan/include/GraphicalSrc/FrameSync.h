#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// The semaphores and fences that keep the CPU and the GPU out of each other's way while a
// window draws.
//
// There are two sets of them, and they are not the same set. That is why this is two classes
// and not one with a flag:
//
//   FrameSync    one per window, for as long as the window is drawn. A pair per frame in
//                flight: a semaphore the swapchain signals when it has an image ready, and a
//                fence the submit signals when the frame's work is done.
//   PresentSync  one per swapchain, rebuilt whenever the swapchain is. A semaphore per *image*:
//                the submit signals it, the present waits on it.
//
// The second cannot be folded into the first, and the reason is the number it is counted in.
// The presentation engine may go on holding a present semaphore after the frame that signalled
// it has finished and its fence has signalled, so reusing it for the next frame in flight
// would signal a semaphore that is still waiting to be consumed. The only count that is safe
// is the number of images the swapchain has, which is not the number of frames in flight and
// changes whenever the swapchain is rebuilt, while the frames' sync objects must survive it.
//
// What the two share is how they are made and unmade, which is here once, and the wait on a
// fence, which a frame does for its own and an image does for whichever frame last drew into it.
namespace RDA {

	// The frames in flight of one window.
	//
	// Not shared between windows. Two windows drawn in one pass would otherwise wait on the
	// same fence and acquire into the same semaphore, and a semaphore signalled by one acquire
	// and waited on by two submissions fails as intermittent corruption, not as an error.
	//
	// Owns the counter that says whose turn it is, so everything else indexed by frame in flight
	// -- the command buffers, the scene data -- is indexed by index() and by nothing of its own.
	// Raw handles, like the rest of the Vulkan layer: destroy() is explicit and the GPU must be
	// done with every frame when it is called.
	class FrameSync {
	public:
		// What one frame in flight gives to the submit.
		struct Frame {
			// Signalled by vkAcquireNextImageKHR, waited on by the submit.
			VkSemaphore imageAvailable = VK_NULL_HANDLE;
			// Signalled by the submit. Created signalled, so the first frame's wait does not
			// block on work that was never submitted.
			VkFence     fence          = VK_NULL_HANDLE;
		};

		// Makes `framesInFlight` frames. False, with nothing left behind, when any object could
		// not be made.
		bool create(uint32_t framesInFlight);
		// Safe to call twice, and on one that was never created.
		void destroy();

		bool     isValid() const { return !mFrames.empty(); }
		uint32_t count()   const { return static_cast<uint32_t>(mFrames.size()); }

		// Whose turn it is: the frame the next draw uses.
		uint32_t index() const { return mCurrent; }

		// Blocks until the GPU has finished the last submit made with this frame, so whatever
		// the frame owns can be written again, and returns its handles. The only way to get
		// them, so nobody holds a frame's fence without having waited on it. Not what
		// vkAcquireNextImageKHR does -- that is the swapchain's, and happens after this.
		const Frame& acquire();
		// Unsignals the frame's fence, ready for the submit to signal it. After the image has
		// been acquired and not before: the acquire can fail and give the frame up unused, and
		// a fence that was reset and never submitted is waited on forever.
		void resetFence();
		// Gives the next draw the next frame. Once a frame has been presented, and not before.
		void advance();

	private:
		std::vector<Frame> mFrames;
		uint32_t           mCurrent = 0;
	};

	// The presentation side of one swapchain.
	class PresentSync {
	public:
		// Makes a semaphore for each of `imageCount` images. False, with nothing left behind,
		// when one could not be made.
		bool create(uint32_t imageCount);
		// Safe to call twice, and on one that was never created. The swapchain calls it first
		// when it is torn down, once the GPU is idle.
		void destroy();

		// Signalled by the draw submit for `image`, waited on by its present.
		VkSemaphore renderFinished(uint32_t image) const { return mRenderFinished[image]; }

		// Records that the frame with `fence` is now drawing into `image`, after waiting for
		// whichever frame was before it. A swapchain can hand an image back while an earlier
		// frame is still rendering into it, and the frame's own fence does not cover that: it
		// was waited on before this image was acquired, and is a different frame's.
		void claimImage(uint32_t image, VkFence fence);

	private:
		std::vector<VkSemaphore> mRenderFinished;   // one per image (owned)
		std::vector<VkFence>     mImageFence;       // borrowed from FrameSync, not owned
	};
}
