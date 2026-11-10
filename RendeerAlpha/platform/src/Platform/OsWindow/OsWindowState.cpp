#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Platform/OsWindow.h>
#include <algorithm>
#include <cmath>

// What the OS does with a window once it exists: its title, frame, size limits, place, and
// whether it is maximised, minimised, fullscreen or focused.
namespace RDA {

	void OsWindow::setTitle(const std::string& title) {
		if (!mWindow) return;
		mInfo.name = title;
		glfwSetWindowTitle(mWindow, title.c_str());
	}

	void OsWindow::setDecorated(bool on) {
		if (!mWindow) return;
		mInfo.style.decorated = on;
		glfwSetWindowAttrib(mWindow, GLFW_DECORATED, on ? GLFW_TRUE : GLFW_FALSE);
	}

	void OsWindow::setResizable(bool on) {
		if (!mWindow) return;
		mInfo.style.resizable = on;
		glfwSetWindowAttrib(mWindow, GLFW_RESIZABLE, on ? GLFW_TRUE : GLFW_FALSE);
	}

	void OsWindow::setAlwaysOnTop(bool on) {
		if (!mWindow) return;
		mInfo.style.alwaysOnTop = on;
		glfwSetWindowAttrib(mWindow, GLFW_FLOATING, on ? GLFW_TRUE : GLFW_FALSE);
	}

	void OsWindow::setOpacity(float value) {
		if (!mWindow) return;
		mInfo.style.opacity = std::clamp(value, 0.0f, 1.0f);
		glfwSetWindowOpacity(mWindow, mInfo.style.opacity);
	}

	void OsWindow::setSizeLimits(unsigned minW, unsigned minH, unsigned maxW, unsigned maxH) {
		if (!mWindow) return;
		mInfo.style.minWidth = minW; mInfo.style.minHeight = minH;
		mInfo.style.maxWidth = maxW; mInfo.style.maxHeight = maxH;

		// A bound is in layout units, because it is a statement about the interface: "this
		// does not work narrower than 400" is about what fits, not about pixels. GLFW wants
		// them in the same coordinates the window's size is in, and the size the window was
		// created with was scaled for the display -- so these are too, or a window opened at
		// 1280 units on a 125% display could not be made narrower than 400 *pixels*, which
		// is 320 units and well inside what the layout said it needed.
		const float k = (mCachedContentScale > 0.0f) ? mCachedContentScale : 1.0f;
		const auto scaled = [k](unsigned value) {
			return value ? static_cast<int>(std::lround(value * k)) : GLFW_DONT_CARE;
		};
		// Zero on an axis is no bound on that axis, which is what GLFW_DONT_CARE says.
		glfwSetWindowSizeLimits(mWindow, scaled(minW), scaled(minH), scaled(maxW), scaled(maxH));
	}

	void OsWindow::setPosition(int x, int y) {
		if (!mWindow) return;
		// One axis may be left to the platform, so whichever is unplaced keeps where the
		// window already is rather than snapping to zero.
		int currentX = 0, currentY = 0;
		glfwGetWindowPos(mWindow, &currentX, &currentY);
		mInfo.style.x = (x == kWindowUnplaced) ? currentX : x;
		mInfo.style.y = (y == kWindowUnplaced) ? currentY : y;
		glfwSetWindowPos(mWindow, mInfo.style.x, mInfo.style.y);
	}

	void OsWindow::getPosition(int& x, int& y) const {
		x = y = 0;
		if (mWindow) glfwGetWindowPos(mWindow, &x, &y);
	}

	// Maximising, minimising, restoring and going fullscreen all show a window on one platform or
	// another, so a hidden one only remembers what was asked: everything that reads the answer
	// back still gets it, and nothing appears on anybody's screen.
	void OsWindow::setMaximized(bool on) {
		if (!mWindow) return;
		if (mInfo.style.hidden) {
			mInfo.style.maximized = on;
			if (on) mHiddenMinimized = false;
			return;
		}
		if (on) glfwMaximizeWindow(mWindow);
		else    glfwRestoreWindow(mWindow);
	}

	bool OsWindow::isMaximized() const {
		if (mInfo.style.hidden) return mInfo.style.maximized;
		return mWindow && glfwGetWindowAttrib(mWindow, GLFW_MAXIMIZED) == GLFW_TRUE;
	}

	void OsWindow::setMinimized(bool on) {
		if (!mWindow) return;
		if (mInfo.style.hidden) {
			mHiddenMinimized = on;
			return;
		}
		if (on) glfwIconifyWindow(mWindow);
		else    glfwRestoreWindow(mWindow);
	}

	bool OsWindow::isMinimized() const {
		if (mInfo.style.hidden) return mHiddenMinimized;
		return mWindow && glfwGetWindowAttrib(mWindow, GLFW_ICONIFIED) == GLFW_TRUE;
	}

	bool OsWindow::isFullscreen() const {
		if (mInfo.style.hidden) return mInfo.style.fullscreen;
		return mWindow && glfwGetWindowMonitor(mWindow) != nullptr;
	}

	bool OsWindow::isFocused() const {
		return mWindow && glfwGetWindowAttrib(mWindow, GLFW_FOCUSED) == GLFW_TRUE;
	}

	void OsWindow::setFullscreen(bool on) {
		if (!mWindow || on == isFullscreen()) return;
		if (mInfo.style.hidden) {
			mInfo.style.fullscreen = on;
			return;
		}
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

	void OsWindow::hide() {
		if (mWindow) glfwHideWindow(mWindow);
	}

	std::string OsWindow::clipboardText() const {
		const char* text = mWindow ? glfwGetClipboardString(mWindow) : nullptr;
		return text ? std::string(text) : std::string();
	}

	void OsWindow::setClipboardText(const std::string& text) {
		if (mWindow) glfwSetClipboardString(mWindow, text.c_str());
	}
}
