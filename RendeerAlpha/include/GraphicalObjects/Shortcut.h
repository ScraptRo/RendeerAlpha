#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace RDA {

	// The keycodes a chord is made of, as GLFW numbers them. Written out here for the same
	// reason Input.h writes out its bounds: so this header, and the tests that use it, stay
	// GLFW-free. RendeerAlpha.cpp, which has GLFW, checks every one of them against it.
	namespace Key {
		inline constexpr int Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46,
		                     Slash = 47, Digit0 = 48, Digit9 = 57, Semicolon = 59, Equal = 61,
		                     A = 65, C = 67, S = 83, V = 86, X = 88, Y = 89, Z = 90,
		                     LeftBracket = 91, Backslash = 92, RightBracket = 93, Grave = 96,
		                     Escape = 256, Enter = 257, Tab = 258, Backspace = 259,
		                     Insert = 260, Delete = 261, Right = 262, Left = 263, Down = 264,
		                     Up = 265, PageUp = 266, PageDown = 267, Home = 268, End = 269,
		                     F1 = 290, F24 = 313, F25 = 314, KeypadEnter = 335,
		                     LeftShift = 340, RightSuper = 347;
	}

	// A key pressed with some modifiers held: Ctrl+S, Ctrl+Shift+P, F5, Delete.
	//
	// `key` is a GLFW keycode, which is the same number the window's Input already indexes
	// by -- so a chord read off the keyboard and a chord parsed out of a layout compare
	// with ==, and nothing in between has to translate. Like every GLFW keycode it names a
	// *position* on a US keyboard, so on AZERTY "Ctrl+Z" is the key labelled W. That is
	// what GLFW gives and what most toolkits do; it is written down in keyboard.md.
	struct Chord {
		enum : uint8_t {
			Shift = 1 << 0,  // the same bits GLFW_MOD_* uses, so an event's mods mask in
			Ctrl  = 1 << 1,  // directly once Caps Lock and Num Lock are taken off
			Alt   = 1 << 2,
			Super = 1 << 3,
			Mask  = Shift | Ctrl | Alt | Super,
		};

		int     key = 0;   // 0 is no chord at all
		uint8_t mods = 0;

		bool valid() const { return key != 0; }
		bool operator==(const Chord& other) const { return key == other.key && mods == other.mods; }
		bool operator!=(const Chord& other) const { return !(*this == other); }
	};

	// "Ctrl+Shift+P" into a chord. Modifiers in any order and any case, joined by '+';
	// "Mod" is Ctrl, the name a layout can use for "whichever modifier this platform's
	// shortcuts are made with". The key is a letter, a digit, F1..F24, a named key (Enter,
	// Escape, Space, Tab, Backspace, Delete, Insert, Home, End, PageUp, PageDown, Up, Down,
	// Left, Right) or one of the punctuation keys written as itself, with "Plus" for '+'.
	//
	// False, with `error` saying what was wrong, on anything else.
	bool parseChord(std::string_view text, Chord& out, std::string* error = nullptr);

	// Why a chord that parses still cannot be a shortcut, or null when it can. Tab and
	// Shift+Tab alone are how the keyboard moves between widgets and cannot be taken away
	// from that. Separate from parsing because something *pressing* a key -- a test, a
	// headless capture -- needs Tab as much as anything does.
	const char* shortcutRefusal(const Chord& chord);

	// The chord written back out the way parseChord reads it, modifiers in a fixed order:
	// "Ctrl+Shift+P". What a menu shows beside an entry, and what the log names.
	std::string chordName(const Chord& chord);

	// What a widget holding the keyboard keeps for itself, ahead of any shortcut.
	//
	// A text field keeps every key with no modifier but a function key -- those are
	// characters, or Escape, or the arrows -- and the Ctrl chords it edits with: the
	// clipboard, undo, select all, moving and deleting by word, sending. A shortcut that
	// took one of those would make the field stop working in a way nobody would think to
	// blame on a menu.
	bool textFieldKeeps(const Chord& chord);
	// Anything else that holds it -- a button, a slider, an open dropdown -- keeps the few
	// keys it acts on: Space and Enter press it, the arrows move it, Escape lets go.
	bool focusedWidgetKeeps(const Chord& chord);
}
