#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GraphicalObjects/Window.h>
#include <Core/BackendConnector.h>
#include <Core/Svg.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>
#include <vendor/stb_image/stb_image.h>
#include <algorithm>
#include <cctype>
#include <cstring>
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
	static void dropCallback(GLFWwindow* window, int count, const char** paths) {
		Window* self = windowFrom(window);
		if (!self || count <= 0 || !paths) return;
		// Copied out of GLFW's array, which is only valid for the length of this call.
		std::vector<std::string> dropped;
		dropped.reserve(static_cast<size_t>(count));
		for (int i = 0; i < count; ++i) {
			if (paths[i]) dropped.emplace_back(paths[i]);
		}
		self->input().onFilesDropped(dropped);
	}

	namespace {
		// One window icon, at one size, as the tightly packed RGBA the platform wants.
		struct IconImage {
			int width = 0, height = 0;
			std::vector<unsigned char> pixels;
		};

		// The sizes an OS actually reaches for: the title bar and the tray at the small
		// end, the alt-tab card and the taskbar in the middle, and the large one for a
		// window list that draws thumbnails. Given several, the platform picks; given one,
		// it scales, and a 16px icon scaled to 64 is the usual reason an application looks
		// unfinished in the task switcher.
		constexpr int kIconSizes[] = { 16, 24, 32, 48, 64, 128, 256 };

		// A drawing is rasterised at every one of them, because it can be: the shape is
		// the file, so each size is drawn rather than resampled and the small ones are
		// sharp instead of being a blurred large one. A .png is what it is, and is handed
		// over as the single size it was saved at.
		bool loadIcon(const std::string& path, std::vector<IconImage>& out) {
			out.clear();
			if (path.empty()) return true;   // "no icon" is a valid thing to ask for

			// Spelled out rather than through a case-insensitive compare, because the
			// one that exists is _stricmp on Windows and strcasecmp everywhere else.
			bool drawn = false;
			if (path.size() > 4) {
				std::string tail = path.substr(path.size() - 4);
				for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				drawn = tail == ".svg";
			}
			if (drawn) {
				std::shared_ptr<Svg::Picture> picture = Svg::load(path);
				if (!picture) {
					RDA_LOG_WARNING("Window icon: cannot read " << path);
					return false;
				}
				for (int size : kIconSizes) {
					IconImage image;
					if (!Svg::rasterise(*picture, static_cast<uint32_t>(size),
					                    static_cast<uint32_t>(size), image.pixels)) {
						continue;
					}
					image.width = image.height = size;
					out.push_back(std::move(image));
				}
				if (out.empty()) RDA_LOG_WARNING("Window icon: nothing drawn from " << path);
				return !out.empty();
			}

			int w = 0, h = 0, channels = 0;
			unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
			if (!pixels) {
				RDA_LOG_WARNING("Window icon: cannot read " << path << " ("
				                << (stbi_failure_reason() ? stbi_failure_reason() : "?") << ")");
				return false;
			}
			IconImage image;
			image.width = w;
			image.height = h;
			image.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
			stbi_image_free(pixels);
			out.push_back(std::move(image));
			return true;
		}
	}

	bool Window::setIcon(const std::string& path) {
		if (!mWindow) return false;
		if (path.empty()) {
			glfwSetWindowIcon(mWindow, 0, nullptr);   // back to the platform's own
			mInfo.style.icon.clear();
			return true;
		}
		std::vector<IconImage> images;
		if (!loadIcon(path, images) || images.empty()) return false;

		std::vector<GLFWimage> handed;
		handed.reserve(images.size());
		for (IconImage& image : images) {
			handed.push_back(GLFWimage{ image.width, image.height, image.pixels.data() });
		}
		glfwSetWindowIcon(mWindow, static_cast<int>(handed.size()), handed.data());
		mInfo.style.icon = path;
		return true;
	}

	Window::Window(WindowInfo& pInfo) : mInfo(pInfo) {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		// Everything the platform decides once, at creation. GLFW keeps these until they
		// are changed, so they are all stated rather than only the ones being turned off.
		glfwWindowHint(GLFW_DECORATED, mInfo.style.decorated ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, mInfo.style.resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, mInfo.style.maximized ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_FLOATING, mInfo.style.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, mInfo.style.transparent ? GLFW_TRUE : GLFW_FALSE);
		// Placed after it is up rather than by a hint, so it is one code path with
		// setPosition -- and so a window that says where it goes does not flash at the
		// platform's choice first. GLFW_VISIBLE is what makes that possible.
		const bool placed = mInfo.style.x != kWindowUnplaced || mInfo.style.y != kWindowUnplaced;
		if (placed) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		// Fullscreen is a monitor rather than a hint. Opening straight onto one avoids
		// creating a swapchain at the windowed size and immediately throwing it away.
		GLFWmonitor* monitor = nullptr;
		if (mInfo.style.fullscreen) {
			monitor = glfwGetPrimaryMonitor();
			if (monitor) {
				if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
					mInfo.Width = static_cast<unsigned>(mode->width);
					mInfo.Height = static_cast<unsigned>(mode->height);
				}
			}
		}
		mWindow = glfwCreateWindow(mInfo.Width, mInfo.Height, mInfo.name.c_str(), monitor, nullptr);
		if (!mWindow) {
			RDA_RUNTIME_ERROR("Failed to create GLFW window");
		}
		if (placed) {
			setPosition(mInfo.style.x, mInfo.style.y);
			glfwShowWindow(mWindow);
		}
		setSizeLimits(mInfo.style.minWidth, mInfo.style.minHeight, mInfo.style.maxWidth, mInfo.style.maxHeight);
		if (mInfo.style.opacity < 1.0f) setOpacity(mInfo.style.opacity);
		if (!mInfo.style.icon.empty()) {
			// Kept in mInfo either way: a failed icon is a warning and a default-looking
			// window, not a reason to refuse to open one.
			const std::string wanted = mInfo.style.icon;
			setIcon(wanted);
			mInfo.style.icon = wanted;
		}
		glfwSetWindowUserPointer(mWindow, this);
		glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);
		glfwSetKeyCallback(mWindow, keyCallback);
		glfwSetCharCallback(mWindow, charCallback);
		glfwSetMouseButtonCallback(mWindow, mouseButtonCallback);
		glfwSetCursorPosCallback(mWindow, cursorPosCallback);
		glfwSetScrollCallback(mWindow, scrollCallback);
		glfwSetDropCallback(mWindow, dropCallback);

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

	void Window::setTitle(const std::string& title) {
		if (!mWindow) return;
		mInfo.name = title;
		glfwSetWindowTitle(mWindow, title.c_str());
	}

	void Window::setDecorated(bool on) {
		if (!mWindow) return;
		mInfo.style.decorated = on;
		glfwSetWindowAttrib(mWindow, GLFW_DECORATED, on ? GLFW_TRUE : GLFW_FALSE);
	}

	void Window::setResizable(bool on) {
		if (!mWindow) return;
		mInfo.style.resizable = on;
		glfwSetWindowAttrib(mWindow, GLFW_RESIZABLE, on ? GLFW_TRUE : GLFW_FALSE);
	}

	void Window::setAlwaysOnTop(bool on) {
		if (!mWindow) return;
		mInfo.style.alwaysOnTop = on;
		glfwSetWindowAttrib(mWindow, GLFW_FLOATING, on ? GLFW_TRUE : GLFW_FALSE);
	}

	void Window::setOpacity(float value) {
		if (!mWindow) return;
		mInfo.style.opacity = std::clamp(value, 0.0f, 1.0f);
		glfwSetWindowOpacity(mWindow, mInfo.style.opacity);
	}

	void Window::setSizeLimits(unsigned minW, unsigned minH, unsigned maxW, unsigned maxH) {
		if (!mWindow) return;
		mInfo.style.minWidth = minW; mInfo.style.minHeight = minH;
		mInfo.style.maxWidth = maxW; mInfo.style.maxHeight = maxH;
		// Zero on an axis is no bound on that axis, which is what GLFW_DONT_CARE says.
		glfwSetWindowSizeLimits(mWindow,
			minW ? static_cast<int>(minW) : GLFW_DONT_CARE,
			minH ? static_cast<int>(minH) : GLFW_DONT_CARE,
			maxW ? static_cast<int>(maxW) : GLFW_DONT_CARE,
			maxH ? static_cast<int>(maxH) : GLFW_DONT_CARE);
	}

	void Window::setPosition(int x, int y) {
		if (!mWindow) return;
		// One axis may be left to the platform, so whichever is unplaced keeps where the
		// window already is rather than snapping to zero.
		int currentX = 0, currentY = 0;
		glfwGetWindowPos(mWindow, &currentX, &currentY);
		mInfo.style.x = (x == kWindowUnplaced) ? currentX : x;
		mInfo.style.y = (y == kWindowUnplaced) ? currentY : y;
		glfwSetWindowPos(mWindow, mInfo.style.x, mInfo.style.y);
	}

	void Window::getPosition(int& x, int& y) const {
		x = y = 0;
		if (mWindow) glfwGetWindowPos(mWindow, &x, &y);
	}

	void Window::setMaximized(bool on) {
		if (!mWindow) return;
		if (on) glfwMaximizeWindow(mWindow);
		else    glfwRestoreWindow(mWindow);
	}

	bool Window::isMaximized() const {
		return mWindow && glfwGetWindowAttrib(mWindow, GLFW_MAXIMIZED) == GLFW_TRUE;
	}

	void Window::setMinimized(bool on) {
		if (!mWindow) return;
		if (on) glfwIconifyWindow(mWindow);
		else    glfwRestoreWindow(mWindow);
	}

	bool Window::isMinimized() const {
		return mWindow && glfwGetWindowAttrib(mWindow, GLFW_ICONIFIED) == GLFW_TRUE;
	}

	bool Window::isFullscreen() const {
		return mWindow && glfwGetWindowMonitor(mWindow) != nullptr;
	}

	bool Window::isFocused() const {
		return mWindow && glfwGetWindowAttrib(mWindow, GLFW_FOCUSED) == GLFW_TRUE;
	}

	void Window::setFullscreen(bool on) {
		if (!mWindow || on == isFullscreen()) return;
		if (on) {
			// Remembered here rather than recomputed on the way back: once the window is
			// on a monitor its own rectangle is gone, and a window that returned to the
			// middle of the screen at a default size every time would be worse than not
			// offering fullscreen at all.
			glfwGetWindowPos(mWindow, &mWindowedX, &mWindowedY);
			glfwGetWindowSize(mWindow, &mWindowedW, &mWindowedH);
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
			if (!mode) return;
			glfwSetWindowMonitor(mWindow, monitor, 0, 0, mode->width, mode->height,
			                     mode->refreshRate);
		} else {
			if (mWindowedW <= 0 || mWindowedH <= 0) {
				mWindowedW = static_cast<int>(mInfo.Width);
				mWindowedH = static_cast<int>(mInfo.Height);
			}
			glfwSetWindowMonitor(mWindow, nullptr, mWindowedX, mWindowedY,
			                     mWindowedW, mWindowedH, 0);
		}
		mInfo.style.fullscreen = on;
		mFramebufferResized = true;
	}

	void Window::followGrip(bool held) {
		if (!mWindow || !held || isMaximized() || isFullscreen()) {
			mGripping = false;
			return;
		}
		double cursorX = 0.0, cursorY = 0.0;
		glfwGetCursorPos(mWindow, &cursorX, &cursorY);
		if (!mGripping) {
			// The first frame only takes hold. Moving on it would jump the window by
			// wherever in the bar the pointer happened to land.
			mGripping = true;
			mGripX = cursorX;
			mGripY = cursorY;
			return;
		}
		int windowX = 0, windowY = 0;
		glfwGetWindowPos(mWindow, &windowX, &windowY);
		// Moving the window moves the cursor's window-relative position back to where it
		// took hold, so the offset stays the offset and this does not drift.
		glfwSetWindowPos(mWindow,
		                 windowX + static_cast<int>(cursorX - mGripX),
		                 windowY + static_cast<int>(cursorY - mGripY));
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
