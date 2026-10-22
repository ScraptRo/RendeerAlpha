#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <memory>

struct GLFWwindow;

namespace RDA {

	class Input;

	// An input method -- the way Chinese, Japanese and Korean are typed: letters go in,
	// candidates come up, a choice comes out as text.
	//
	// GLFW passes the finished text on as characters and nothing else: no composition in
	// progress, no say over where the method opens its list, and no way to turn it off over
	// a button so a shortcut's letter is not swallowed by it. Each platform layer here adds
	// those three things for one window, and feeds what it learns into that window's Input.
	//
	// On Windows it is the IMM messages, read in the window procedure. Elsewhere, for now,
	// nothing: X11's input methods work through GLFW for finished text, in the method's own
	// window, and Wayland's do not reach GLFW at all.
	class TextInputHost {
	public:
		static std::unique_ptr<TextInputHost> attach(GLFWwindow* window, Input& input);
		virtual ~TextInputHost() = default;
		// Once a frame, on the loop thread: whether a text field has the keyboard, and where
		// its caret is in physical pixels relative to the window's client area.
		virtual void update(bool wantsText, const Rect& caretPixels) { (void)wantsText; (void)caretPixels; }
	};
}
