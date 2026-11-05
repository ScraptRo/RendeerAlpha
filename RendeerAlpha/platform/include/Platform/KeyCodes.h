#pragma once

// The keys and mouse buttons the engine names, as the window system numbers them.
//
// Written out here so that nothing above the platform layer needs the window system's own
// header to say "Enter" or "the left button": the GUI's chords, the input the runtime
// gathers and the tests all use these. Every one is checked against GLFW where GLFW is
// included -- Platform/WindowSystem.cpp -- so the two cannot quietly disagree.
//
// Like every GLFW keycode, a key names a *position* on a US keyboard: on AZERTY "Ctrl+Z" is
// the key labelled W. That is what GLFW gives and what most toolkits do; keyboard.md says so.
namespace RDA {

	namespace Key {
		inline constexpr int Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46,
		                     Slash = 47, Digit0 = 48, Digit9 = 57, Semicolon = 59, Equal = 61,
		                     A = 65, C = 67, S = 83, V = 86, X = 88, Y = 89, Z = 90,
		                     LeftBracket = 91, Backslash = 92, RightBracket = 93, Grave = 96,
		                     Escape = 256, Enter = 257, Tab = 258, Backspace = 259,
		                     Insert = 260, Delete = 261, Right = 262, Left = 263, Down = 264,
		                     Up = 265, PageUp = 266, PageDown = 267, Home = 268, End = 269,
		                     F1 = 290, F24 = 313, F25 = 314, KeypadEnter = 335,
		                     LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
		                     RightShift = 344, RightControl = 345, RightAlt = 346,
		                     RightSuper = 347;
	}

	namespace MouseButton {
		inline constexpr int Left = 0, Right = 1, Middle = 2;
	}

	// The modifier bits an event carries, which are also the bits a Chord holds.
	namespace KeyMod {
		inline constexpr unsigned Shift = 1u << 0, Ctrl = 1u << 1, Alt = 1u << 2, Super = 1u << 3;
	}
}
