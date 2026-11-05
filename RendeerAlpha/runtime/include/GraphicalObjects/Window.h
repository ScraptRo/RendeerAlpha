#pragma once
#include <GraphicalSrc/RenderSurface.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <Core/BuildMode.h>
#include <Core/Datatypes.h>
#include <GraphicalSrc/PresentationSurface.h>
#include <Platform/OsWindow.h>
#include <GraphicalObjects/Input.h>
#include <GraphicalObjects/Gui.h>
#include <atomic>
#include <memory>
namespace RDA {
	// Every open window, in the order they were made. A window adds itself when its OS
	// window exists; the event pump and the draw loop walk this. Owned here, beside the
	// windows it lists, rather than declared in a Core header and defined in the loop.
	extern stack_list windowList;

	// A window owns its GLFW handle, the Vulkan surface, the swapchain, and a surface
	// FrameBuffer built over that swapchain — its default render target. The renderer
	// draws into getFrameBuffer() unless it is pointed at another target (e.g. an
	// offscreen FrameBuffer), which is how a GUI can be rendered into the world and
	// then swapped back to the window with a single reference change.
	//
	// GLFW is main-thread only, so the OS-facing state (framebuffer size, close
	// request) is read once per pump by syncOSState() and cached here. Everything
	// else reads the cached values rather than calling GLFW itself.
	class Window : public RenderSurface
	{
		stack_element mID;

		WindowInfo mInfo;
		// The window as the OS has it -- see Platform/OsWindow.h. After mInfo, which it
		// writes back to.
		OsWindow mOs;
		// The surface, the swapchain and the FrameBuffer over it -- see PresentationSurface.
		PresentationSurface mPresent;
		Input mInput;
		// Held by value, so every window carries the whole interface layer. There is no build
		// that leaves it out.
		Gui mGui;
		// What a screen reader is told, and who tells it. The bridge is shared because the
		// platform layer's providers outlive a frame and may outlive the window; the host
		// is detached before the window it hooks into is destroyed.
		std::shared_ptr<AccessibleBridge>  mA11yBridge;
		std::unique_ptr<class AccessibilityHost> mA11yHost;
		AccessibleWindow mA11yWindowSeen;   // what the bridge was last told
		// An input method's say in this window: on over a field, off elsewhere, its
		// candidates at the caret. Attached after the accessibility host, detached before it,
		// because both hook the same window procedure and each puts back what it found.
		std::unique_ptr<class TextInputHost> mTextInput;

		std::atomic<bool> mRedrawRequested{ false }; // see requestRedraw()
	public:
		Window(WindowInfo&);
		~Window();

		void closeWindow();               // destroys the GLFW window
		bool windowIsUp() const;

		// Copies GLFW state into the cached values above. Main thread only.
		void syncOSState();

		// Rebuilds the swapchain at the current cached size.
		// Returns false when the window is minimized (nothing to size a swapchain to).
		bool recreateSwapchain() override;

		void markResized() { mOs.markResized(); }
		bool wasResized() const override { return mOs.resized(); }

		// Ask for one more frame for *this* window. In RedrawMode::OnDemand each window
		// earns its frames separately, so a caller that knows which window changed should
		// say so: the global rendeerRequestRedraw() wakes every window, which on a runtime
		// serving several applications drags the idle ones through a redraw they had no
		// reason to want.
		//
		// Atomic because a client's frame can arrive on a thread that is not the loop's.
		void requestRedraw() { mRedrawRequested.store(true, std::memory_order_relaxed); }
		// Reads and clears in one step, so a request is honoured exactly once.
		bool takeRedrawRequest() { return mRedrawRequested.exchange(false, std::memory_order_relaxed); }

		// Last framebuffer size seen by the event pump, in pixels.
		VkExtent2D cachedExtent() const override { return { mOs.framebufferWidth(), mOs.framebufferHeight() }; }

		// What the OS says a layout unit is worth here: 1.0 at 100%, 1.5 at 150%. Read
		// every event pump, so it follows a window moved to another monitor -- whether the
		// interface follows it is a separate question, and the engine's.
		float contentScale() const { return mOs.contentScale(); }

		// Maps a cursor position (GLFW screen coords) into framebuffer pixels, so GUI
		// hit-testing lines up with what was rendered on a HiDPI display.
		glm::vec2 cursorToFramebuffer(glm::vec2 screenPos) const { return mOs.cursorToFramebuffer(screenPos); }

		// ---- what the OS does with it, after it exists -------------------------------
		//
		// Every one of these is the platform's business, and is the OS window's -- see
		// Platform/OsWindow.h. They are here so the rest of the engine reads this class
		// rather than reaching past it.

		// The picture the OS shows for this window. A .png, or an .svg rasterised at the
		// sizes an OS picks from. Empty puts the platform's default back.
		bool setIcon(const std::string& path) { return mOs.setIcon(path); }

		void setTitle(const std::string& title) { mOs.setTitle(title); }
		void setDecorated(bool on) { mOs.setDecorated(on); }
		void setResizable(bool on) { mOs.setResizable(on); }
		void setAlwaysOnTop(bool on) { mOs.setAlwaysOnTop(on); }
		void setOpacity(float value) { mOs.setOpacity(value); }
		void setSizeLimits(unsigned minW, unsigned minH, unsigned maxW, unsigned maxH) {
			mOs.setSizeLimits(minW, minH, maxW, maxH);
		}
		void setPosition(int x, int y) { mOs.setPosition(x, y); }
		void getPosition(int& x, int& y) const { mOs.getPosition(x, y); }

		void setMaximized(bool on) { mOs.setMaximized(on); }
		bool isMaximized() const { return mOs.isMaximized(); }
		void setMinimized(bool on) { mOs.setMinimized(on); }
		bool isMinimized() const { return mOs.isMinimized(); }
		void setFullscreen(bool on) { mOs.setFullscreen(on); }
		bool isFullscreen() const { return mOs.isFullscreen(); }
		bool isFocused() const { return mOs.isFocused(); }
		// Shown no more, while still being a window: an engine's anchor for its device.
		void hide() { mOs.hide(); }
		// Whether the OS window exists at all, which windowIsUp() also asks.
		bool isOpen() const { return mOs.isOpen(); }
		// Asked of the OS now rather than of the cache -- for an idle wait, which must notice
		// a close the moment it arrives.
		bool closeRequestedNow() const { return mOs.closeRequestedNow(); }
		// What runs a frame while the OS holds the event loop, during a drag or a resize.
		void setModalPump(void (*pump)()) { mOs.setModalPump(pump); }

		// The OS clipboard, as text.
		std::string clipboardText() const { return mOs.clipboardText(); }
		void setClipboardText(const std::string& text) { mOs.setClipboardText(text); }

		// Move the window with the pointer, for a window with no frame of its own.
		//
		// Called every frame with whether a `dragWindow` region is being held. The first
		// held frame only remembers where the pointer took hold; every one after it moves
		// the window by how far the pointer has gone since. A maximised window ignores
		// it -- dragging one somewhere would leave it maximised and in the wrong place.
		void followGrip(bool held) { mOs.followGrip(held); }

		// Resize a frameless window by its edges, the way a framed one is.
		//
		// Called every frame before the interface sees the pointer, with the left button's
		// state and, on the frame it went down, where it went down -- in the window's
		// cursor coordinates, as the pointer arrives. Where it *went down*, rather than
		// where the pointer is by the frame that hears of it: a quick drag has already
		// moved by then, and the edge would trail the hand by that much for the whole
		// resize. A thin band inside the window's edges belongs to the window: over it the
		// pointer shows which way it would resize, and a press there resizes until the
		// button comes up. Returns true while the pointer is the window's -- over the band
		// with nothing held, or resizing -- so the caller keeps it from the interface, and
		// a button drawn at the very edge does not light up or click under a resize.
		//
		// Does nothing for a framed window (the OS does this), one that is not resizable,
		// or one that is maximised or fullscreen, which have no edges to move.
		bool followEdges(bool pressed, bool down, double pressX, double pressY) {
			return mOs.followEdges(pressed, down, pressX, pressY);
		}

		// After the walk: tells the platform frame where the layout's maximise button is
		// (see `maximizeButton`), whether the pointer is on a bare stretch of title bar,
		// and how deep the resize band is. On Windows that is what offers snap layouts
		// over the button, hands the bar to the OS's caption, and resizes natively.
		void frameDone();


		VkSurfaceKHR   getSurface() const { return mPresent.surface(); }
		Swapchain&     getSwapchain() override { return mPresent.swapchain(); }
		// The window's default render target — a surface FrameBuffer over the swapchain.
		FrameBuffer&   getFrameBuffer() override { return mPresent.framebuffer(); }
		// This window's input state and event stream.
		Input&         input() { return mInput; }
		// This window's immediate-mode GUI. The interface layer is part of every build, so
		// this always has something to hand back.
		Gui&           gui() { return mGui; }
		// What the renderer draws this frame, from this window's GUI. See RenderSurface.
		const GuiDrawData& guiDrawData() const override;
		uint64_t           guiDrawVersion() const override;
		uint32_t           guiBackground() const override;
		Rect               guiViewportRect() const override;
		// After the frame: let the screen reader know what changed. A no-op until one asks.
		void           accessibilityFrameDone();
		// After the frame: tell the input method whether a field has the keyboard, and where.
		void           textInputFrameDone();
		// Describe the interface whether or not a screen reader has asked -- for a tool
		// that wants to read the tree itself.
		void           describeForAccessibility() { if (mA11yBridge) mA11yBridge->want(); }
		const WindowInfo& getInfo() const { return mInfo; }

		static Window* getRefFromNode(stack_element&);
	};
}
