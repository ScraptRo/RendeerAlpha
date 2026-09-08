#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GraphicalObjects/Window.h>
#include <Core/BackendConnector.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
namespace RDA {
	void InitGlfw() {
		glfwInit();
	}

	void endGlfw() {
		glfwTerminate();
	}

	static void framebufferResizeCallback(GLFWwindow* window, int, int) {
		auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		if (self) self->markResized();
	}

	// GLFW hands input to these free functions with the window's user pointer; each one
	// recovers the owning Window (one pointer load) and forwards into its Input. GLFW's
	// action codes line up with InputAction (RELEASE/PRESS/REPEAT = 0/1/2).
	static Window* windowFrom(GLFWwindow* window) {
		return reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
	}

	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		if (Window* self = windowFrom(window)) {
			self->input().onKey(key, scancode, static_cast<InputAction>(action), mods);
		}
	}
	static void charCallback(GLFWwindow* window, unsigned int codepoint) {
		if (Window* self = windowFrom(window)) self->input().onChar(codepoint);
	}
	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		if (Window* self = windowFrom(window)) {
			self->input().onMouseButton(button, static_cast<InputAction>(action), mods);
		}
	}
	static void cursorPosCallback(GLFWwindow* window, double x, double y) {
		if (Window* self = windowFrom(window)) self->input().onCursorPos(x, y);
	}
	static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		if (Window* self = windowFrom(window)) self->input().onScroll(xoffset, yoffset);
	}

	Window::Window(WindowInfo& pInfo) : mInfo(pInfo) {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		mWindow = glfwCreateWindow(mInfo.Width, mInfo.Height, mInfo.name.c_str(), nullptr, nullptr);
		if (!mWindow) {
			RDA_RUNTIME_ERROR("Failed to create GLFW window");
		}
		glfwSetWindowUserPointer(mWindow, this);
		glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);
		glfwSetKeyCallback(mWindow, keyCallback);
		glfwSetCharCallback(mWindow, charCallback);
		glfwSetMouseButtonCallback(mWindow, mouseButtonCallback);
		glfwSetCursorPosCallback(mWindow, cursorPosCallback);
		glfwSetScrollCallback(mWindow, scrollCallback);

		if (!mSurface.Create(appInstance, mWindow)) {
			RDA_RUNTIME_ERROR("Failed to create window surface");
		}

		syncOSState(); // seed the cached size before anything renders
		mSwapchain.setVsync(mInfo.vsync); // FIFO vs. Mailbox; honored on recreate too
		if (mSwapchain.create(mSurface.Get(), cachedExtent())) {
			if (!mFrameBuffer.createSurface(mSwapchain)) {
				RDA_RUNTIME_ERROR("Failed to create the window's surface FrameBuffer");
			}
		} else {
			// Created with no area: a session restoring its windows minimised, or a
			// compositor that has not given this one a size yet. Not fatal, and not
			// something to build around -- the first frame that finds it restored
			// makes the swapchain then.
			RDA_LOG_INFO("Window created with a zero-sized surface; its swapchain is "
			             "built when the window is given a size");
			mFramebufferResized = true;
		}

		windowList.addNode(&mID);
		RDA_LOG_SUCCES("Window Created Succesfully");
	}

	Window::~Window() {
		closeWindow();
	}

	void Window::closeWindow() {
		if (!mWindow) return; // already closed

		VkDevice device = getDevice();
		if (device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(device);
		}
		mFrameBuffer.destroy();
		mSwapchain.destroy();
		mSurface.Destroy();
		glfwDestroyWindow(mWindow);
		mWindow = nullptr;
		mShouldClose = true;
	}

	bool Window::windowIsUp() const {
		return mWindow && !mShouldClose;
	}

	void Window::syncOSState() {
		if (!mWindow) return;
		int width = 0, height = 0;
		glfwGetFramebufferSize(mWindow, &width, &height);
		mCachedWidth = static_cast<uint32_t>(width);
		mCachedHeight = static_cast<uint32_t>(height);
		int winW = 0, winH = 0;
		glfwGetWindowSize(mWindow, &winW, &winH);
		mCachedWinWidth = static_cast<uint32_t>(winW);
		mCachedWinHeight = static_cast<uint32_t>(winH);
		if (glfwWindowShouldClose(mWindow)) {
			mShouldClose = true;
		}
	}

	bool Window::recreateSwapchain() {
		// Minimized: there is nothing to size a swapchain to. Bail out and let the
		// caller try again on a later frame, once the event pump has cached a
		// non-zero size.
		VkExtent2D extent = cachedExtent();
		if (extent.width == 0 || extent.height == 0) {
			return false;
		}

		// And the cache is not enough. It is refreshed by the event pump; a window
		// minimised *after* the last poll and before the present that notices still
		// has a non-zero cache while its surface already reports nothing. Building
		// from that is VUID-VkSwapchainCreateInfoKHR-imageExtent-01689, followed by a
		// 0x0 depth attachment that fails to allocate -- three errors in the log for
		// one minimise. Asked here, before the device is stalled and before anything
		// is destroyed. mFramebufferResized is deliberately left set: the window is
		// coming back, and the frame that finds it restored rebuilds this.
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
		mFramebufferResized = false;
		return true;
	}

	Window* Window::getRefFromNode(stack_element& node) {
		return node.getBody<Window, &RDA::Window::mID>();
	}
}
