#include "Engine.h"
#include <Core/Wake.h>

#include <Platform/WindowSystem.h>

// The OS's events, pumped into every window -- and the frames run from inside the loop the
// OS enters while a window is dragged, which is the one time the pump cannot return.
namespace RDA::Runtime {

	// Pumps OS events and mirrors each window's state into its cache. Runs on the
	// loop thread (GLFW is not thread-safe). Returns false once no window is open.
	// `waitSeconds` > 0 blocks until an event arrives or that long passes, instead of
	// returning immediately — how an OnDemand loop idles rather than spinning a core.
	bool pumpEvents(double waitSeconds) {
		// Roll each window's input forward before the poll, so this frame's events form
		// clean rising/falling edges against last frame's state.
		for (auto& node : windowList) {
			Window::getRefFromNode(node)->input().newFrame();
		}

		// Fires the input trampolines, which fill each window's Input.
		if (waitSeconds > 0.0) {
			// Idle: wait out the whole period, but return the moment real input arrives.
			// The OS wakes glfwWaitEventsTimeout for plenty of messages that change
			// nothing on screen, and returning on those would have the loop rebuild the
			// GUI at their rate instead of idling. newFrame() ran before the first wait,
			// so events accumulate across these waits rather than being rolled away.
			const double deadline = windowSystemTime() + waitSeconds;
			for (;;) {
				const double remaining = deadline - windowSystemTime();
				if (remaining <= 0.0) break;
				waitEvents(remaining);
				// A frame request is read, not consumed: the render gate below owns it.
				// A wake is consumed here, because breaking out is all it means.
				if (!gRunning.load() || Wake::frameRequested() || Wake::takeWake()) break;

				bool sawInput = false;
				for (auto& node : windowList) {
					Window* window = Window::getRefFromNode(node);
					if (!window->input().events().empty() ||
					    window->input().mouseDelta() != glm::vec2(0.0f) ||
					    window->closeRequestedNow()) {
						sawInput = true;
						break;
					}
				}
				if (sawInput) break;
			}
		} else {
			pollEvents();
		}

		bool anyAlive = false;
		for (auto& node : windowList) {
			Window* window = Window::getRefFromNode(node);
			window->syncOSState();
			if (window->windowIsUp()) anyAlive = true;
		}
		// A windowless application keeps running with nothing open: that is its normal
		// resting state, not the end of its work. It stops when something calls
		// rendeerStop(), not when the last window it was lent goes away.
		return anyAlive || !gWindowDependent;
	}

	// Runs a frame from inside the message loop Windows enters while a window is being
	// dragged or resized.
	//
	// glfwPollEvents does not return until the user lets go, and everything the engine does
	// lives after that call — the host poll that reads client messages, the GUI, every
	// window's draw. So dragging one window stops all of them. On an ordinary application
	// that is a stutter; on the runtime it is every connected application freezing because
	// somebody moved a neighbour's window.
	//
	// GLFW still delivers position and refresh events from inside that loop, so the frame
	// is driven off them. Two things matter here: this runs *inside* glfwPollEvents, so it
	// must never pump events again, and it must roll each window's input forward afterwards
	// or the same press would be delivered on every callback for as long as the drag lasts.
	static void serviceWhileModal() {
		static bool inside = false;
		// A frame is already being built, so this is not the case this function exists
		// for.
		//
		// The three callbacks that lead here fire from inside glfwPollEvents while a
		// window is being dragged, which is when the loop genuinely cannot run a frame.
		// They *also* fire synchronously out of SetWindowPos -- and SetWindowPos is what
		// changing a window's decoration, position or size calls. So an application that
		// writes `state.rda.maximized` or asks for a frameless window from inside a
		// widget's handler arrives here in the middle of the walk that ran that handler,
		// and a frame started there resets the frame arena and truncates the draw list
		// underneath the walk still using them. Measured: toggling the Sandbox's title
		// bar three times re-entered five frames, and the corruption showed up as
		// anything from nothing at all to the process going away.
		//
		// Dropping the event costs nothing, because the thing it would have reported --
		// a new size or position -- is read again at the top of the next frame. Asking
		// for one is what makes sure there is a next frame in an on-demand window.
		if (gInFrame) { rendeerRequestRedraw(); return; }
		if (inside || !gLoopConfig || !gRunning.load()) return;
		inside = true;

		runFrame(*gLoopConfig, advanceClock());
		for (auto& node : windowList) {
			Window::getRefFromNode(node)->input().newFrame();
		}

		inside = false;
	}

	// Attaches the callbacks that keep the engine running while `window` is dragged or
	// resized. Every window needs them, including the ones the runtime opens for clients.
	void watchForModalLoop(Window* window) {
		if (window) window->setModalPump(&serviceWhileModal);
	}
}
