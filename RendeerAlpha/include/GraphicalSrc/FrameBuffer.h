#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalObjects/Texture.h>

namespace RDA {
	class Swapchain;

	// The render target the renderer draws into — one abstraction, two flavors that
	// present the exact same interface so the renderer only ever holds a FrameBuffer&.
	//
	//  - Surface: wraps a window's swapchain. One VkFramebuffer per swapchain image and
	//    a color attachment whose final layout is PRESENT_SRC, so the result is ready to
	//    be presented. A window builds one of these as its default target.
	//
	//  - Offscreen: owns a sampleable color texture (+ depth) and a single framebuffer,
	//    with a color final layout of SHADER_READ_ONLY so the result can be sampled —
	//    e.g. mapped onto a 3D plane to show a GUI inside the world. Point the renderer
	//    at one of these, render, then point it back at the window's surface: because
	//    both are the same type, that swap is a single reference change.
	//
	// Both are a single subpass with one color + one depth attachment. Their render
	// passes are compatible whenever the color formats match, so one pipeline can draw
	// into either — which is what makes the swap above free on the pipeline side too.
	//  - Depth: no colour at all, just a sampleable depth image. What a shadow map is:
	//    the scene is rendered from the light's point of view purely to record how far
	//    the nearest surface is in each direction.
	enum class FrameBufferKind { Surface, Offscreen, Depth };

	class FrameBuffer {
	public:
		FrameBuffer() = default;
		~FrameBuffer();

		FrameBuffer(const FrameBuffer&) = delete;
		FrameBuffer& operator=(const FrameBuffer&) = delete;

		// Surface target backed by a swapchain. recreateSurface() handles a resize and
		// keeps the render pass, so pipelines built against it stay valid.
		bool createSurface(Swapchain& swapchain);
		bool recreateSurface(Swapchain& swapchain);

		// Offscreen target of a fixed size, rendered into an owned sampleable texture.
		// colorFormat should match the surface it shares a pipeline with; the default is
		// the format the swapchain normally picks.
		bool createOffscreen(uint32_t width, uint32_t height,
		                     VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB);

		// Depth-only target of a fixed size, sampleable afterwards — a shadow map.
		bool createDepthOnly(uint32_t width, uint32_t height);

		void destroy();

		FrameBufferKind kind()        const { return mKind; }
		bool            isSurface()   const { return mKind == FrameBufferKind::Surface; }
		VkRenderPass    renderPass()  const { return mRenderPass; }
		VkFramebuffer   framebuffer(uint32_t imageIndex = 0) const;
		VkExtent2D      extent()      const { return mExtent; }
		VkFormat        colorFormat() const { return mColorFormat; }
		bool            isValid()     const { return mRenderPass != VK_NULL_HANDLE; }

		// Offscreen only: the rendered color as a sampleable texture. Has no valid image
		// for a Surface target (its color lives in the swapchain, not here).
		const Texture&  colorTexture() const { return mColor; }
		// Depth-only targets: the recorded depth, ready to sample.
		const Texture&  depthTexture() const { return mDepth; }

		static VkFormat findDepthFormat();

	private:
		FrameBufferKind mKind = FrameBufferKind::Surface;

		Texture mColor; // owned only by an Offscreen target
		Texture mDepth; // owned by both

		VkRenderPass               mRenderPass = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> mFramebuffers; // Surface: one per image; Offscreen: one
		VkExtent2D                 mExtent{};
		VkFormat                   mColorFormat = VK_FORMAT_UNDEFINED;
		VkFormat                   mDepthFormat = VK_FORMAT_UNDEFINED;

		bool createRenderPass(VkImageLayout colorFinalLayout, bool sampledAfterwards);
		bool createDepthOnlyRenderPass();
		bool createDepth();
		bool createSurfaceFramebuffers(Swapchain& swapchain);
		bool createOffscreenFramebuffer();
		void destroyFramebuffers();
	};
}
