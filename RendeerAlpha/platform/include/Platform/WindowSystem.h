#pragma once
#include <vector>

// The window system as a whole, rather than any one window: starting and ending it, the event
// queue, its clock, and what it asks of Vulkan and of the display. GLFW underneath, and nothing
// above the platform layer includes GLFW to do any of this.
//
// The event functions are main-thread only, like everything GLFW does with windows.
namespace RDA {

	// Started before the first window and ended after the last.
	void initWindowSystem();
	void endWindowSystem();

	// Delivers whatever is queued to every window's callbacks and returns.
	void pollEvents();
	// The same, after waiting up to `seconds` for something to arrive.
	void waitEvents(double seconds);
	// Ends a wait early, from any thread: what a request for a frame posts.
	void postEmptyEvent();

	// Seconds on the window system's clock, which is what a double click is timed against.
	double windowSystemTime();

	// The instance extensions a window surface needs, asked of the window system so the Vulkan
	// layer does not have to know there is one.
	std::vector<const char*> requiredInstanceExtensions();

	// What a layout unit is worth on the primary display: 1.0 at 100%, 1.5 at 150%. Asked
	// before there is a window to ask; 1.0 when the display will not say.
	float primaryDisplayScale();
}
