#include <GraphicalObjects/Input.h>

namespace RDA {

	Input::Input() {
		// Discrete events are cleared, not reallocated, each frame; reserve enough that
		// a normal frame never grows the buffer.
		mEvents.reserve(64);
	}

	// ---- polled state -------------------------------------------------------------
	bool Input::isKeyDown(int key) const {
		return (key >= 0 && key < MaxKeys) && mKeysCurrent.test(key);
	}
	bool Input::keyPressed(int key) const {
		return (key >= 0 && key < MaxKeys) && mKeysCurrent.test(key) && !mKeysPrevious.test(key);
	}
	bool Input::keyReleased(int key) const {
		return (key >= 0 && key < MaxKeys) && !mKeysCurrent.test(key) && mKeysPrevious.test(key);
	}

	bool Input::isMouseButtonDown(int button) const {
		return (button >= 0 && button < MaxMouseButtons) && mButtonsCurrent.test(button);
	}
	bool Input::mouseButtonPressed(int button) const {
		return (button >= 0 && button < MaxMouseButtons) && mButtonsCurrent.test(button) && !mButtonsPrevious.test(button);
	}
	bool Input::mouseButtonReleased(int button) const {
		return (button >= 0 && button < MaxMouseButtons) && !mButtonsCurrent.test(button) && mButtonsPrevious.test(button);
	}

	// ---- frame lifecycle ----------------------------------------------------------
	void Input::newFrame() {
		// Snapshot before the poll: everything the poll writes into "current" becomes a
		// rising/falling edge against this. mouseDelta is measured from here, so rebase
		// the previous position to wherever the cursor is now.
		mKeysPrevious = mKeysCurrent;
		mButtonsPrevious = mButtonsCurrent;
		mMousePrev = mMousePos;
		mScroll = glm::vec2(0.0f);
		mEvents.clear();
	}

	// ---- GLFW trampoline entry points ---------------------------------------------
	void Input::onKey(int key, int scancode, InputAction action, int mods) {
		if (key >= 0 && key < MaxKeys) {
			// GLFW_REPEAT keeps the key down; only Release clears it.
			mKeysCurrent.set(key, action != InputAction::Release);
		}
		mEvents.push_back({ InputEventType::Key, key, scancode, action, mods, 0, 0.0, 0.0 });
		if (mCallbacks.onKey) mCallbacks.onKey(key, scancode, action, mods);
	}

	void Input::onChar(unsigned int codepoint) {
		InputEvent event{};
		event.type = InputEventType::Char;
		event.codepoint = codepoint;
		mEvents.push_back(event);
		if (mCallbacks.onChar) mCallbacks.onChar(codepoint);
	}

	void Input::onMouseButton(int button, InputAction action, int mods) {
		if (button >= 0 && button < MaxMouseButtons) {
			mButtonsCurrent.set(button, action != InputAction::Release);
		}
		InputEvent event{};
		event.type = InputEventType::MouseButton;
		event.key = button;
		event.action = action;
		event.mods = mods;
		mEvents.push_back(event);
		if (mCallbacks.onMouseButton) mCallbacks.onMouseButton(button, action, mods);
	}

	void Input::onCursorPos(double x, double y) {
		glm::vec2 position{ static_cast<float>(x), static_cast<float>(y) };
		if (!mHasMouse) {
			// First sample: seed both so this frame's delta is zero rather than a jump
			// from the origin.
			mMousePos = position;
			mMousePrev = position;
			mHasMouse = true;
		} else {
			mMousePos = position;
		}
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.x = x;
		event.y = y;
		mEvents.push_back(event);
		if (mCallbacks.onMouseMove) mCallbacks.onMouseMove(x, y);
	}

	void Input::onScroll(double xoffset, double yoffset) {
		mScroll += glm::vec2(static_cast<float>(xoffset), static_cast<float>(yoffset));
		InputEvent event{};
		event.type = InputEventType::Scroll;
		event.x = xoffset;
		event.y = yoffset;
		mEvents.push_back(event);
		if (mCallbacks.onScroll) mCallbacks.onScroll(xoffset, yoffset);
	}

	void Input::onFilesDropped(const std::vector<std::string>& paths) {
		// No polled state and no InputEvent for this one. A drop is not something a
		// frame is in the middle of -- it happens once, carries data too big for the
		// event struct, and an application that misses the frame it landed on has lost
		// the files. So it goes straight to whoever asked, and to the queue the C ABI
		// drains at its own pace.
		if (mCallbacks.onFilesDropped) mCallbacks.onFilesDropped(paths);
	}
}
