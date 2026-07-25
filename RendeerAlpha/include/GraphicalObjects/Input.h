#pragma once
#include <Core/Datatypes.h>
#include <vector>
#include <bitset>
#include <functional>

namespace RDA {

	// Key / mouse-button state is indexed directly by the GLFW keycode, so no lookup
	// table is needed. These bounds mirror GLFW_KEY_LAST + 1 and GLFW_MOUSE_BUTTON_LAST
	// + 1; the Input layer stays GLFW-free by hard-coding them here.
	inline constexpr int MaxKeys = 349;
	inline constexpr int MaxMouseButtons = 8;

	enum class InputAction {
		Release = 0, // matches GLFW_RELEASE
		Press = 1,   // matches GLFW_PRESS
		Repeat = 2,  // matches GLFW_REPEAT
	};

	enum class InputEventType {
		Key,
		Char,
		MouseButton,
		MouseMove,
		Scroll,
	};

	// One discrete input event. Flat POD so a frame's worth lives contiguously and
	// copies cheaply. Which fields are meaningful depends on `type`.
	struct InputEvent {
		InputEventType type;
		int            key = 0;        // Key: keycode | MouseButton: button index
		int            scancode = 0;   // Key
		InputAction    action{};       // Key, MouseButton
		int            mods = 0;       // Key, MouseButton: modifier bitmask
		unsigned int   codepoint = 0;  // Char: UTF-32
		double         x = 0.0;        // MouseMove: position | Scroll: x offset
		double         y = 0.0;        // MouseMove: position | Scroll: y offset
	};

	// Callbacks the application can set (via AppConfig) to react to discrete events.
	// They run on the loop thread, dispatched as each event arrives during the poll.
	struct InputCallbacks {
		std::function<void(int key, int scancode, InputAction action, int mods)> onKey;
		std::function<void(unsigned int codepoint)>                              onChar;
		std::function<void(int button, InputAction action, int mods)>            onMouseButton;
		std::function<void(double x, double y)>                                  onMouseMove;
		std::function<void(double xoffset, double yoffset)>                      onScroll;
	};

	// Per-window input. Two views of the same stream, both filled during the window's
	// event poll:
	//
	//  - Polled state (isKeyDown / keyPressed / mouseDelta / ...): a double-buffered
	//    snapshot, consistent for the whole frame, cheap O(1) queries. Read it from
	//    onUpdate for continuous things like movement.
	//  - Event list (events()): this frame's discrete events in order, for text and UI.
	//
	// Everything is written and read on the loop thread, so no locking is involved.
	class Input {
	public:
		Input();

		// ---- polled state (query from onUpdate) ----
		bool isKeyDown(int key) const;
		bool keyPressed(int key) const;   // went down this frame
		bool keyReleased(int key) const;  // came up this frame

		bool isMouseButtonDown(int button) const;
		bool mouseButtonPressed(int button) const;
		bool mouseButtonReleased(int button) const;

		glm::vec2 mousePosition() const { return mMousePos; }
		glm::vec2 mouseDelta()    const { return mMousePos - mMousePrev; }
		glm::vec2 scroll()        const { return mScroll; } // accumulated this frame

		// ---- discrete events for this frame (in arrival order) ----
		const std::vector<InputEvent>& events() const { return mEvents; }

		// ---- engine-internal: driven by the window and the loop, not the app ----
		void setCallbacks(const InputCallbacks& callbacks) { mCallbacks = callbacks; }

		// Rolls the state forward one frame: snapshots the current key/button state as
		// "previous", rebases the mouse delta, resets the scroll accumulator and clears
		// last frame's events. Called once, before the poll.
		void newFrame();

		// Entry points the window's GLFW trampolines forward into.
		void onKey(int key, int scancode, InputAction action, int mods);
		void onChar(unsigned int codepoint);
		void onMouseButton(int button, InputAction action, int mods);
		void onCursorPos(double x, double y);
		void onScroll(double xoffset, double yoffset);

	private:
		std::bitset<MaxKeys> mKeysCurrent;
		std::bitset<MaxKeys> mKeysPrevious;
		std::bitset<MaxMouseButtons> mButtonsCurrent;
		std::bitset<MaxMouseButtons> mButtonsPrevious;

		glm::vec2 mMousePos{ 0.0f };
		glm::vec2 mMousePrev{ 0.0f };
		glm::vec2 mScroll{ 0.0f };
		bool      mHasMouse = false; // suppress a huge delta on the first cursor event

		std::vector<InputEvent> mEvents;
		InputCallbacks          mCallbacks;
	};
}
