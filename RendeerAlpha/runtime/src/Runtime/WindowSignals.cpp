#include "Engine.h"
#include <Core/Signals.h>

namespace RDA::Runtime {

	// What the engine knows about itself, as signals a layout can read: state.rda.width
	// and state.rda.height, in the same pixels every widget is laid out in.
	//
	// Defined here rather than by an application, because they are the engine's answer
	// and not the application's -- and defined during bring-up, before onStart, so the
	// first layout to read one finds it instead of inventing it as an undeclared number.
	//
	// Updated every frame and not on a resize callback: a callback is one more thing to
	// keep in step with minimise, maximise, a monitor change and a compositor that
	// resizes without telling anyone. Signals::set already refuses a write that changes
	// nothing, so a still window costs two comparisons a frame and wakes no bindings.
	static uint32_t gWidthSignal = kNoSignal, gHeightSignal = kNoSignal;
	static uint32_t gWindowSignalWidth = 0, gWindowSignalHeight = 0;

	// The window's own state, as signals that read *and* write.
	//
	// Width and height are the engine reporting; these four are a conversation. The OS
	// changes them -- somebody pressed the maximise button, or alt-tabbed away -- and so
	// does the interface, because a window with no frame of its own has to draw those
	// buttons and they have to do something. Making them state rather than calls is the
	// same answer this engine gives everywhere else: `state.rda.maximized = true` is a
	// window maximising, in a handler that is otherwise just an assignment.
	static uint32_t gMaximizedSignal = kNoSignal, gMinimizedSignal = kNoSignal;
	static uint32_t gFullscreenSignal = kNoSignal, gFocusedSignal = kNoSignal;
	static uint32_t gOpenSignal = kNoSignal;
	// Whether something is being dragged in the interface, and what sort. Read-only, and
	// what a layout reads to show where things may go before they get there.
	static uint32_t gDraggingSignal = kNoSignal, gDragKindSignal = kNoSignal;
	static bool        gMirrorDragging = false;
	static std::string gMirrorDragKind;
	// What was last agreed. A flag that differs from this on the signal side was written
	// by the interface; one that differs on the OS side was done by the reader.
	static bool gMirrorMaximized = false, gMirrorMinimized = false;
	static bool gMirrorFullscreen = false, gMirrorFocused = false;

	// Whichever side moved is the one that meant it. The signal is checked first, so a
	// write made during this frame's handlers is acted on rather than being overwritten
	// by the state the window has not reached yet.
	template <typename Apply>
	static void syncWindowFlag(uint32_t signal, bool& mirror, bool osValue, Apply apply) {
		if (signal == kNoSignal) return;
		const bool wanted = signals().boolean(signal);
		if (wanted != mirror) {
			apply(wanted);
			mirror = wanted;
			return;
		}
		if (osValue != mirror) {
			signals().set(signal, osValue);
			mirror = osValue;
		}
	}

	void publishWindowSignals() {
		// Created once, at their opening values, and written only through set()
		// afterwards. define() carries a value and installs it *without* marking
		// observers dirty -- which is right for declaring state and wrong for changing
		// it, so calling it every frame updated the number while no binding ever heard
		// about it. The window resized, the label did not, and nothing anywhere said so.
		if (gWidthSignal == kNoSignal) {
			gWidthSignal = signals().define("rda.width", 0.0);
			gHeightSignal = signals().define("rda.height", 0.0);
			gMirrorMaximized = mainWindow && mainWindow->isMaximized();
			gMirrorMinimized = mainWindow && mainWindow->isMinimized();
			gMirrorFullscreen = mainWindow && mainWindow->isFullscreen();
			gMirrorFocused = mainWindow && mainWindow->isFocused();
			gMaximizedSignal = signals().define("rda.maximized", gMirrorMaximized);
			gMinimizedSignal = signals().define("rda.minimized", gMirrorMinimized);
			gFullscreenSignal = signals().define("rda.fullscreen", gMirrorFullscreen);
			gFocusedSignal = signals().define("rda.focused", gMirrorFocused);
			gOpenSignal = signals().define("rda.open", true);
			gDraggingSignal = signals().define("rda.dragging", false);
			gDragKindSignal = signals().define("rda.dragKind", std::string_view{});
		}

		// A drag is the main window's GUI's. Set on the change only, so an idle frame
		// marks nothing.
		if (mainWindow) {
			const Gui& gui = mainWindow->gui();
			const bool dragging = gui.dragging();
			if (dragging != gMirrorDragging) {
				gMirrorDragging = dragging;
				signals().set(gDraggingSignal, dragging);
			}
			const std::string& kind = dragging ? gui.dragKind() : std::string();
			if (kind != gMirrorDragKind) {
				gMirrorDragKind = kind;
				signals().set(gDragKindSignal, std::string_view(kind));
			}
		}

		if (mainWindow) {
			syncWindowFlag(gMaximizedSignal, gMirrorMaximized, mainWindow->isMaximized(),
			               [](bool on) { mainWindow->setMaximized(on); });
			syncWindowFlag(gMinimizedSignal, gMirrorMinimized, mainWindow->isMinimized(),
			               [](bool on) { mainWindow->setMinimized(on); });
			syncWindowFlag(gFullscreenSignal, gMirrorFullscreen, mainWindow->isFullscreen(),
			               [](bool on) { mainWindow->setFullscreen(on); });
			// Read-only: nothing an application writes can make the reader look at it.
			const bool focused = mainWindow->isFocused();
			if (focused != gMirrorFocused) {
				gMirrorFocused = focused;
				signals().set(gFocusedSignal, focused);
			}
			// The window being open is state too, and writing false is how a title bar
			// this engine drew closes the window it is sitting on.
			if (gOpenSignal != kNoSignal && !signals().boolean(gOpenSignal)) {
				rendeerStop();
			}
		}

		const uint32_t width = mainWindow ? mainWindow->cachedExtent().width : 0;
		const uint32_t height = mainWindow ? mainWindow->cachedExtent().height : 0;
		if (width == gWindowSignalWidth && height == gWindowSignalHeight) return;
		gWindowSignalWidth = width;
		gWindowSignalHeight = height;
		// In layout units, like every other number a layout reads. A binding that compares
		// `state.rda.width` against a width it wrote itself is comparing two of the same
		// thing; published in pixels it would be comparing against something a quarter
		// larger on a display at 125%, and the mistake would only appear on one machine.
		const float k = unitScale();
		signals().set(gWidthSignal, static_cast<double>(width) / k);
		signals().set(gHeightSignal, static_cast<double>(height) / k);
	}

	// A second run in the same process gets its own signal table, so the ids above do
	// not survive one.
	void forgetWindowSignals() {
		gWidthSignal = kNoSignal;
		gHeightSignal = kNoSignal;
		gMaximizedSignal = gMinimizedSignal = kNoSignal;
		gFullscreenSignal = gFocusedSignal = gOpenSignal = kNoSignal;
		gMirrorMaximized = gMirrorMinimized = gMirrorFullscreen = gMirrorFocused = false;
		gWindowSignalWidth = 0;
		gWindowSignalHeight = 0;
	}
}
