#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Platform/WindowSystem.h>
#include <Platform/KeyCodes.h>
#include <algorithm>
#include <cstdint>

namespace RDA {

	// Platform/KeyCodes.h writes GLFW's numbers out so that nothing above this layer needs
	// GLFW. Checked here, where GLFW is, so the two cannot quietly disagree.
	static_assert(KeyMod::Shift == GLFW_MOD_SHIFT && KeyMod::Ctrl == GLFW_MOD_CONTROL
	              && KeyMod::Alt == GLFW_MOD_ALT && KeyMod::Super == GLFW_MOD_SUPER,
	              "a Chord's modifier bits are GLFW's, so an event's mask needs no translation");
	static_assert(MouseButton::Left == GLFW_MOUSE_BUTTON_LEFT
	              && MouseButton::Right == GLFW_MOUSE_BUTTON_RIGHT
	              && MouseButton::Middle == GLFW_MOUSE_BUTTON_MIDDLE,
	              "KeyCodes.h's mouse buttons are GLFW's");
	static_assert(Key::Space == GLFW_KEY_SPACE && Key::Apostrophe == GLFW_KEY_APOSTROPHE
	              && Key::Comma == GLFW_KEY_COMMA && Key::Minus == GLFW_KEY_MINUS
	              && Key::Period == GLFW_KEY_PERIOD && Key::Slash == GLFW_KEY_SLASH
	              && Key::Digit0 == GLFW_KEY_0 && Key::Digit9 == GLFW_KEY_9
	              && Key::Semicolon == GLFW_KEY_SEMICOLON && Key::Equal == GLFW_KEY_EQUAL
	              && Key::A == GLFW_KEY_A && Key::C == GLFW_KEY_C && Key::S == GLFW_KEY_S
	              && Key::V == GLFW_KEY_V && Key::X == GLFW_KEY_X && Key::Y == GLFW_KEY_Y
	              && Key::Z == GLFW_KEY_Z && Key::LeftBracket == GLFW_KEY_LEFT_BRACKET
	              && Key::Backslash == GLFW_KEY_BACKSLASH
	              && Key::RightBracket == GLFW_KEY_RIGHT_BRACKET
	              && Key::Grave == GLFW_KEY_GRAVE_ACCENT && Key::Escape == GLFW_KEY_ESCAPE
	              && Key::Enter == GLFW_KEY_ENTER && Key::Tab == GLFW_KEY_TAB
	              && Key::Backspace == GLFW_KEY_BACKSPACE && Key::Insert == GLFW_KEY_INSERT
	              && Key::Delete == GLFW_KEY_DELETE && Key::Right == GLFW_KEY_RIGHT
	              && Key::Left == GLFW_KEY_LEFT && Key::Down == GLFW_KEY_DOWN
	              && Key::Up == GLFW_KEY_UP && Key::PageUp == GLFW_KEY_PAGE_UP
	              && Key::PageDown == GLFW_KEY_PAGE_DOWN && Key::Home == GLFW_KEY_HOME
	              && Key::End == GLFW_KEY_END && Key::F1 == GLFW_KEY_F1
	              && Key::F24 == GLFW_KEY_F24 && Key::F25 == GLFW_KEY_F25
	              && Key::KeypadEnter == GLFW_KEY_KP_ENTER
	              && Key::LeftShift == GLFW_KEY_LEFT_SHIFT
	              && Key::LeftControl == GLFW_KEY_LEFT_CONTROL && Key::LeftAlt == GLFW_KEY_LEFT_ALT
	              && Key::LeftSuper == GLFW_KEY_LEFT_SUPER && Key::RightShift == GLFW_KEY_RIGHT_SHIFT
	              && Key::RightControl == GLFW_KEY_RIGHT_CONTROL && Key::RightAlt == GLFW_KEY_RIGHT_ALT
	              && Key::RightSuper == GLFW_KEY_RIGHT_SUPER,
	              "KeyCodes.h's keycodes are GLFW's");

	void initWindowSystem() { glfwInit(); }

	void endWindowSystem() { glfwTerminate(); }

	void pollEvents() { glfwPollEvents(); }

	void waitEvents(double seconds) { glfwWaitEventsTimeout(seconds); }

	void postEmptyEvent() { glfwPostEmptyEvent(); }

	double windowSystemTime() { return glfwGetTime(); }

	std::vector<const char*> requiredInstanceExtensions() {
		uint32_t count = 0;
		const char** names = glfwGetRequiredInstanceExtensions(&count);
		if (!names) return {};
		return std::vector<const char*>(names, names + count);
	}

	float primaryDisplayScale() {
		GLFWmonitor* primary = glfwGetPrimaryMonitor();
		if (!primary) return 1.0f;
		float sx = 1.0f, sy = 1.0f;
		glfwGetMonitorContentScale(primary, &sx, &sy);
		// One number: an interface scaled differently on the two axes would need two of
		// everything, and no display anybody uses asks for that.
		return (sx > 0.0f && sy > 0.0f) ? (std::min)(sx, sy) : 1.0f;
	}
}
