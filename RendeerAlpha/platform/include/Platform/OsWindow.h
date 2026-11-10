#pragma once
#include <Core/Datatypes.h>
#include <Core/Rect.h>
#include <glm/vec2.hpp>
#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;
struct GLFWcursor;

namespace RDA {
	class Input;
	class WindowFrameHost;

	// The window as the operating system has it: the GLFW window, what the OS says about it,
	// and everything done to it -- its title, frame, size limits and place, maximising,
	// fullscreen, its icon, the clipboard, and moving and resizing a window with no frame of its
	// own.
	//
	// This is the platform layer's half of a window. The runtime's Window puts one together
	// with a presentation surface, a GUI, input and the platform hosts, and nothing above this
	// layer touches GLFW. GLFW is main-thread only, so the OS-facing state is read once per
	// event pump by sync() and cached here; everything else reads the cache.
	class OsWindow {
	public:
		// Opens the window `info` describes, placed, limited, faded and with its icon, and
		// reads its first state. `info` is the owner's and outlives this; what the OS is
		// asked to change is written back to it, so it always says what the window is.
		explicit OsWindow(WindowInfo& info);
		~OsWindow();
		OsWindow(const OsWindow&) = delete;
		OsWindow& operator=(const OsWindow&) = delete;

		// Closes the OS window. The platform hosts hooked into it must be gone first.
		void destroy();
		bool isOpen() const { return mWindow != nullptr; }

		// For the platform hosts and the presentation surface, which are created against the
		// window itself. Nothing above the platform layer should need it.
		GLFWwindow* handle() const { return mWindow; }

		// ---- events ------------------------------------------------------------------------
		// Keys, characters, buttons, the pointer, the wheel and dropped files, into `input`.
		void attachInput(Input& input);
		// The platform's own frame, where it has one to lend (Windows): snapping, the layouts
		// flyout, native edges. Attached last of everything that hooks the window, so its
		// window procedure is the outermost -- the owner decides when that is.
		void attachFrame(Input& input);
		void detachFrame();
		// What runs a frame while the OS holds the event loop -- while a window is dragged or
		// resized, glfwPollEvents does not return, and the position, size and paint
		// notifications are the only thing still arriving.
		void setModalPump(void (*pump)());

		// ---- what the OS says, cached --------------------------------------------------------
		// Copies the OS's state into the cache below. Main thread only.
		void sync();
		bool closeRequested() const { return mShouldClose; }
		// Asked of the OS now rather than of the cache: for the idle wait, which has to notice
		// a close the moment it arrives.
		bool closeRequestedNow() const;
		void markResized() { mFramebufferResized = true; }
		void clearResized() { mFramebufferResized = false; }
		bool resized() const { return mFramebufferResized; }
		// The framebuffer's size in pixels, as of the last sync.
		uint32_t framebufferWidth() const { return mCachedWidth; }
		uint32_t framebufferHeight() const { return mCachedHeight; }
		// What the OS says a layout unit is worth here: 1.0 at 100%, 1.5 at 150%.
		float contentScale() const { return mCachedContentScale; }
		// A cursor position, in screen coordinates, as framebuffer pixels -- so hit-testing
		// lines up with what was rendered on a HiDPI display.
		glm::vec2 cursorToFramebuffer(glm::vec2 screenPos) const {
			if (mCachedWinWidth == 0 || mCachedWinHeight == 0) return screenPos;
			return { screenPos.x * static_cast<float>(mCachedWidth) / mCachedWinWidth,
			         screenPos.y * static_cast<float>(mCachedHeight) / mCachedWinHeight };
		}

		// ---- what the OS does with it --------------------------------------------------------
		bool setIcon(const std::string& path);
		void setTitle(const std::string& title);
		void setDecorated(bool on);
		void setResizable(bool on);
		void setAlwaysOnTop(bool on);
		void setOpacity(float value);
		void setSizeLimits(unsigned minW, unsigned minH, unsigned maxW, unsigned maxH);
		void setPosition(int x, int y);
		void getPosition(int& x, int& y) const;
		void setMaximized(bool on);
		bool isMaximized() const;
		void setMinimized(bool on);
		bool isMinimized() const;
		void setFullscreen(bool on);
		bool isFullscreen() const;
		bool isFocused() const;
		void hide();

		// The OS clipboard, as text. Reached through a window because that is how GLFW asks.
		std::string clipboardText() const;
		void setClipboardText(const std::string& text);

		// ---- a window with no frame of its own -----------------------------------------------
		// See Window::followGrip and Window::followEdges, which say what these are for.
		void followGrip(bool held);
		bool followEdges(bool pressed, bool down, double pressX, double pressY);
		// Tells the platform's frame where the layout's maximise button is, in pixels, and
		// whether the pointer is on a bare stretch of title bar.
		void updateNativeFrame(const Rect& maximizeButton, bool captionUnderPointer);

	private:
		void showEdgeCursor(uint8_t edge);

		GLFWwindow* mWindow = nullptr;
		WindowInfo& mInfo;
		Input*      mInput = nullptr;
		void      (*mModalPump)() = nullptr;

		bool     mFramebufferResized = false;
		bool     mShouldClose = false;
		uint32_t mCachedWidth = 0;       // framebuffer size, in pixels
		uint32_t mCachedHeight = 0;
		uint32_t mCachedWinWidth = 0;    // window content size, in screen coords
		uint32_t mCachedWinHeight = 0;
		float    mCachedContentScale = 1.0f; // physical pixels per layout unit, per the OS

		// Where the pointer took hold of the window, in window coordinates, while a
		// `dragWindow` region is held. See followGrip.
		bool   mGripping = false;
		double mGripX = 0.0, mGripY = 0.0;
		// A grip taken on a maximised window: it restores once the pointer has gone
		// somewhere, under the pointer, and then carries on as an ordinary drag.
		bool   mGripFromMaximized = false;
		// The platform's frame, where it has one to lend (Windows): snapping, the layouts
		// flyout, native edges. See Platform/WindowFrame.h.
		std::unique_ptr<WindowFrameHost> mFrame;

		// An edge of a frameless window being dragged (see followEdges), and the window
		// and the pointer, in screen coordinates, as they were when it was taken hold of.
		uint8_t mResizeEdge = 0;
		int     mResizeStartX = 0, mResizeStartY = 0, mResizeStartW = 0, mResizeStartH = 0;
		double  mResizeFromX = 0.0, mResizeFromY = 0.0;
		// The resize cursors, made the first time one is wanted, and which one is showing
		// -- set on the change only. 0xFF is "never set", so the first frame sets one.
		GLFWcursor* mCursorH = nullptr;
		GLFWcursor* mCursorV = nullptr;
		GLFWcursor* mCursorNWSE = nullptr;
		GLFWcursor* mCursorNESW = nullptr;
		uint8_t mCursorEdge = 0xFF;
		// Where a fullscreen window came from, so leaving it puts it back rather than
		// leaving it the size of the monitor with a frame around it.
		int mWindowedX = 0, mWindowedY = 0, mWindowedW = 0, mWindowedH = 0;
		// What a hidden window was last told, since nothing on the screen can say. Maximised and
		// fullscreen are kept in the style, which already has a place for them.
		bool mHiddenMinimized = false;
	};
}
