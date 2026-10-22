#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <memory>

struct GLFWwindow;

namespace RDA {

	class Input;

	// The platform's own frame behaviour, for a window that draws its own frame.
	//
	// The engine can move a frameless window by its bar, resize it by its edges and
	// maximise it on a double click, all through GLFW (see Window::followGrip and
	// followEdges). What it cannot do that way is what a platform gives only to a window
	// it believes has a frame: on Windows, snapping when it is dragged to an edge, the bar
	// of snap layouts at the top of the screen while it is dragged, the flyout of layouts
	// over the maximise button, and resizing that snaps too. Each of those is decided by
	// the window's styles and by what it answers when Windows asks what is under the
	// pointer, so a host here gives the window a frame's styles, draws none of it, and
	// answers those questions the way a frame would.
	//
	// On Windows: WS_THICKFRAME and the caption styles with WM_NCCALCSIZE taking the whole
	// window as client area, and WM_NCHITTEST answering the resize edges, HTMAXBUTTON over
	// the layout's maximise button and HTCAPTION over a bare stretch of its title bar -- so
	// a press there is the OS's from the first moment, and moving, snapping, double-click
	// maximise, dragging out of maximised and the system menu are all Windows' own.
	// Elsewhere, nothing: the GLFW paths are what there is, and native() says so.
	class WindowFrameHost {
	public:
		// For a frameless, resizable window only; anything else gets the do-nothing host.
		static std::unique_ptr<WindowFrameHost> attach(GLFWwindow* window, Input& input);
		virtual ~WindowFrameHost() = default;

		// True when the platform is doing the frame's work: the engine's own edge resize
		// and its own maximised-drag restore then stand aside.
		virtual bool native() const { return false; }

		// Once a frame, on the loop thread: where the layout's maximise button is, in
		// physical pixels relative to the client area (empty when there is none); whether
		// the pointer is on a bare stretch of title bar, as of the last walk; and how deep
		// the resize band is, in the same pixels.
		virtual void update(const Rect& maximizeButton, bool captionUnderPointer, double edgeBand) {
			(void)maximizeButton; (void)captionUnderPointer; (void)edgeBand;
		}

		// A press on the title bar reached the interface after all -- the platform asked
		// what was under the pointer before a frame had found out it was the bar. Handed
		// to the platform now, so it is the same caption press it would have been.
		virtual void captionPressed() {}
	};
}
