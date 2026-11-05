#pragma once

// Waking the loop, from anywhere.
//
// An on-demand loop idles in the event pump while nothing changes. Whatever wants a frame
// -- a handler, a backend on another thread, a screen reader, an input method, a route that
// finished -- says so here, and the pump stops waiting. It used to be three atomics in the
// loop file, reachable only through the engine's public API, so a platform host at the
// bottom of the stack included the top of it to ask for a redraw. This is the one place
// the request lives; the loop reads it, and nothing else has to know where the loop is.
//
// Safe from any thread.
namespace RDA::Wake {

	// Asks for a frame of every window, and ends the idle wait.
	void requestFrame();

	// Ends the idle wait and nothing more: for a request already recorded somewhere more
	// specific, such as on one window.
	void wake();

	// What is shown changed in a way no input or binding reports -- a layout swapped, a
	// route finished -- so every window's interface is walked again, not just presented.
	// The window system installs what that means (see setInterfaceChangedHandler); before
	// it has, this is requestFrame().
	void interfaceChanged();

	// ---- the loop's side -----------------------------------------------------------

	// Whether a frame was asked for. Peeked by the idle wait; taken by the render gate,
	// which is the only place the request is consumed.
	bool frameRequested();
	bool takeFrameRequest();

	// Whether the wait was asked to end since the last look. Taken, because ending the
	// wait is all it means.
	bool takeWake();

	// What actually ends an OS wait -- glfwPostEmptyEvent -- installed once the window
	// system is up and taken away (nullptr) before it ends, so a wake-up from another
	// thread can never reach a window system that is not there.
	void setPoster(void (*post)());

	// What interfaceChanged() does beyond asking for a frame: marking every window's GUI
	// as needing a walk. Installed by whoever owns the windows.
	void setInterfaceChangedHandler(void (*handler)());
}
