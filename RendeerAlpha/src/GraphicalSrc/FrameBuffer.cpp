#define VK_USE_PLATFORM_WIN32_KHR
#include <GraphicalSrc/FrameBuffer.h>
#include <GraphicalSrc/Swapchain.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
#include <array>

namespace RDA {

	FrameBuffer::~FrameBuffer() {
		destroy();
	}

	VkFormat FrameBuffer::findDepthFormat() {
		const VkFormat candidates[] = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT,
		};
		for (VkFormat format : candidates) {
			VkFormatProperties props{};
			vkGetPhysicalDeviceFormatProperties(getPhysicalDevice(), format, &props);
			if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
				return format;
			}
		}
		return VK_FORMAT_D32_SFLOAT;
	}

	VkFramebuffer FrameBuffer::framebuffer(uint32_t imageIndex) const {
		// Offscreen has a single framebuffer; the image index only means anything for a
		// surface, where it selects the swapchain image being drawn this frame.
		if (isSurface()) return mFramebuffers[imageIndex];
		return mFramebuffers.empty() ? VK_NULL_HANDLE : mFramebuffers[0];
	}

	// ---- Surface ------------------------------------------------------------------
	bool FrameBuffer::createSurface(Swapchain& swapchain) {
		destroy();
		mKind = FrameBufferKind::Surface;
		mExtent = swapchain.extent();
		mColorFormat = swapchain.format();
		mDepthFormat = findDepthFormat();

		if (!createRenderPass(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, /*sampledAfterwards*/ false)) return false;
		if (!createDepth()) return false;
		if (!createSurfaceFramebuffers(swapchain)) return false;
		return true;
	}

	bool FrameBuffer::recreateSurface(Swapchain& swapchain) {
		if (mKind != FrameBufferKind::Surface || mRenderPass == VK_NULL_HANDLE) {
			return createSurface(swapchain);
		}
		// The color format is unchanged across a resize, so the render pass survives;
		// only the size-dependent resources are rebuilt. Keeping the render pass keeps
		// every pipeline built against this target valid.
		destroyFramebuffers();
		mDepth.destroy();
		mExtent = swapchain.extent();
		if (!createDepth()) return false;
		if (!createSurfaceFramebuffers(swapchain)) return false;
		return true;
	}

	// ---- Offscreen ----------------------------------------------------------------
	bool FrameBuffer::createOffscreen(uint32_t width, uint32_t height, VkFormat colorFormat) {
		destroy();
		mKind = FrameBufferKind::Offscreen;
		mExtent = { width, height };
		mColorFormat = colorFormat;
		mDepthFormat = findDepthFormat();

		TextureDesc color{};
		color.width = width;
		color.height = height;
		color.format = colorFormat;
		color.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		color.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		color.withSampler = true; // this target is meant to be sampled afterwards
		if (!mColor.create(color)) {
			RDA_LOG_ERROR("Failed to create offscreen color attachment");
			return false;
		}

		if (!createRenderPass(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, /*sampledAfterwards*/ true)) return false;
		if (!createDepth()) return false;
		if (!createOffscreenFramebuffer()) return false;
		return true;
	}

	// ---- Shared building blocks ---------------------------------------------------
	bool FrameBuffer::createRenderPass(VkImageLayout colorFinalLayout, bool sampledAfterwards) {
		std::array<VkAttachmentDescription, 2> attachments{};

		// 0 — color: cleared, drawn into, stored. Its final layout is what makes a target
		// either presentable (PRESENT_SRC) or sampleable (SHADER_READ_ONLY).
		attachments[0].format = mColorFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = colorFinalLayout;

		// 1 — depth: cleared and tested each frame, never read back afterwards.
		attachments[1].format = mDepthFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		std::array<VkSubpassDependency, 2> dependencies{};
		uint32_t dependencyCount = 1;

		// Entry: don't begin writing color/depth until any prior use is finished. For an
		// offscreen target the "prior use" is last frame's sampling of it, so the wait is
		// against the fragment shader; for a surface it's the previous frame's output.
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = (sampledAfterwards ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)
			| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = sampledAfterwards ? VK_ACCESS_SHADER_READ_BIT : 0;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		// Exit (offscreen only): make the color writes visible to whoever samples this
		// target next, in a later render pass.
		if (sampledAfterwards) {
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencyCount = 2;
		}

		VkRenderPassCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		createInfo.pAttachments = attachments.data();
		createInfo.subpassCount = 1;
		createInfo.pSubpasses = &subpass;
		createInfo.dependencyCount = dependencyCount;
		createInfo.pDependencies = dependencies.data();

		if (vkCreateRenderPass(getDevice(), &createInfo, nullptr, &mRenderPass) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create framebuffer render pass");
			mRenderPass = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	bool FrameBuffer::createDepth() {
		TextureDesc depth{};
		depth.width = mExtent.width;
		depth.height = mExtent.height;
		depth.format = mDepthFormat;
		depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		depth.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		depth.withSampler = false;
		if (!mDepth.create(depth)) {
			RDA_LOG_ERROR("Failed to create framebuffer depth attachment");
			return false;
		}
		return true;
	}

	bool FrameBuffer::createSurfaceFramebuffers(Swapchain& swapchain) {
		VkDevice device = getDevice();
		mFramebuffers.resize(swapchain.imageCount());

		for (uint32_t i = 0; i < swapchain.imageCount(); i++) {
			VkImageView views[2] = { swapchain.imageView(i), mDepth.view() };

			VkFramebufferCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			info.renderPass = mRenderPass;
			info.attachmentCount = 2;
			info.pAttachments = views;
			info.width = mExtent.width;
			info.height = mExtent.height;
			info.layers = 1;

			if (vkCreateFramebuffer(device, &info, nullptr, &mFramebuffers[i]) != VK_SUCCESS) {
				RDA_LOG_ERROR("Failed to create surface framebuffer");
				return false;
			}
		}
		return true;
	}

	bool FrameBuffer::createOffscreenFramebuffer() {
		VkImageView views[2] = { mColor.view(), mDepth.view() };

		VkFramebufferCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.renderPass = mRenderPass;
		info.attachmentCount = 2;
		info.pAttachments = views;
		info.width = mExtent.width;
		info.height = mExtent.height;
		info.layers = 1;

		mFramebuffers.resize(1);
		if (vkCreateFramebuffer(getDevice(), &info, nullptr, &mFramebuffers[0]) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create offscreen framebuffer");
			mFramebuffers.clear();
			return false;
		}
		return true;
	}

	void FrameBuffer::destroyFramebuffers() {
		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE) return;
		for (VkFramebuffer framebuffer : mFramebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}
		mFramebuffers.clear();
	}

	void FrameBuffer::destroy() {
		VkDevice device = getDevice();
		if (device == VK_NULL_HANDLE) return;

		destroyFramebuffers();
		if (mRenderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(device, mRenderPass, nullptr);
			mRenderPass = VK_NULL_HANDLE;
		}
		mDepth.destroy();
		mColor.destroy();
	}
}
