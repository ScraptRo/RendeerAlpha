#include "Engine.h"
#include <Core/Utf8.h>
#include <cmath>
#include <unordered_map>

#include <Platform/KeyCodes.h>
#include <Platform/WindowSystem.h>

// One window's input, turned into what its GUI hears this frame: the pointer in layout
// units, buttons with their edges, click runs, keys as chords and as editing keys, text,
// composition and dropped files -- with whatever synthetic input says happened on top.
namespace RDA::Runtime {

	// What is remembered about each window's pointer between frames (loop thread only).
	// Per window: a click in one window must not count towards a double click in another,
	// and each window has its own pointer position anyway.
	struct PointerHistory {
		// The click run, for double and triple clicks.
		double    time = -1.0;
		glm::vec2 pos{ 0.0f };
		int       count = 1;
		// Whether the synthetic button was down last frame, to work out its edges from. It
		// was one function-local flag for every window, so a press reached the first window
		// gathered and every other one saw it as already held.
		bool      syntheticWasDown = false;
	};
	static std::unordered_map<const RDA::Window*, PointerHistory> gPointerHistory;

	// One key going down, or repeating, as the GUI wants to hear it: as a chord for the
	// shortcuts, as an editing key for whatever has the keyboard, and as one of the
	// clipboard and undo flags. `gi.ctrl` and `gi.shift` are already set for the frame.
	static void gatherKey(RDA::GuiInput& gi, int key, int mods) {
		using namespace RDA;
		// Every key but a modifier on its own: Ctrl going down is not a chord, it is the
		// start of one. The keypad's Enter is Enter, because nobody who writes
		// "Ctrl+Enter" means only the one on the main block.
		if (key < Key::LeftShift || key > Key::RightSuper) {
			Chord chord;
			chord.key = (key == Key::KeypadEnter) ? Key::Enter : key;
			chord.mods = static_cast<uint8_t>(mods & Chord::Mask);
			if (chord.key > 0) gi.chords.insert(chord);
		}
		switch (key) {
		case Key::Backspace: gi.editKeys.insert(GuiEditKey::Backspace); break;
		case Key::Delete:    gi.editKeys.insert(GuiEditKey::Delete);    break;
		case Key::Left:      gi.editKeys.insert(GuiEditKey::Left);       break;
		case Key::Right:     gi.editKeys.insert(GuiEditKey::Right);      break;
		case Key::Up:        gi.editKeys.insert(GuiEditKey::Up);         break;
		case Key::Down:      gi.editKeys.insert(GuiEditKey::Down);       break;
		case Key::Home:      gi.editKeys.insert(GuiEditKey::Home);       break;
		case Key::End:       gi.editKeys.insert(GuiEditKey::End);        break;
		case Key::Enter:
		case Key::KeypadEnter:
			if (gi.ctrl) gi.submit = true;
			gi.editKeys.insert(GuiEditKey::Enter);
			break;
		case Key::Tab:       gi.editKeys.insert(GuiEditKey::Tab);        break;
		case Key::Escape:    gi.editKeys.insert(GuiEditKey::Escape);     break;
		// Space reaches a text field as typed text and must go on doing so; it is also how
		// a keyboard presses a button, so it arrives as both and the widget that has the
		// keyboard decides which one it was.
		case Key::Space:     gi.editKeys.insert(GuiEditKey::Space);      break;
		case Key::C: if (gi.ctrl) gi.copy = true;      break;
		case Key::X: if (gi.ctrl) gi.cut = true;       break;
		case Key::V: if (gi.ctrl) gi.paste = true;     break;
		case Key::A: if (gi.ctrl) gi.selectAll = true; break;
		// Both spellings of redo, because both are muscle memory somewhere: Ctrl+Y on
		// Windows, Ctrl+Shift+Z everywhere a Mac keyboard taught it.
		case Key::Z: if (gi.ctrl) { if (gi.shift) gi.redo = true; else gi.undo = true; } break;
		case Key::Y: if (gi.ctrl) gi.redo = true;      break;
		default: break;
		}
	}

	// Gathers one window's input and opens its GUI frame. Every window owns its own Gui
	// and its own Input, so this is per window rather than per application: the runtime
	// gives each client a window of its own, and each has to be driven independently.
	void beginWindowGui(RDA::Window& window, RDA::GuiInput& gi, float dtSeconds) {
		using namespace RDA;
				Input& in = window.input();
		// Reset only what is accumulated below; every other field is assigned.
		gi.typed.clear();
		gi.editKeys.clear();
		gi.chords.clear();
		gi.copy = gi.cut = gi.paste = gi.selectAll = gi.submit = false;
		gi.undo = gi.redo = false;
		// Both in layout units, because that is what the interface is written in. The
		// pointer arrives in screen coordinates and the framebuffer is pixels, so each is
		// converted once, here -- and from here inward nothing knows what a pixel is.
		const float unitScale = Runtime::unitScale();
		gi.pointer = window.cursorToFramebuffer(in.mousePosition()) / unitScale;
		// Already in layout units, because whoever set it was reading a layout's numbers.
		if (gSyntheticPointer) gi.pointer = gSyntheticAt;
		VkExtent2D ext = window.cachedExtent();
		gi.viewport = { static_cast<float>(ext.width) / unitScale,
		                static_cast<float>(ext.height) / unitScale };
		gi.lDown = in.isMouseButtonDown(MouseButton::Left);      // MouseButton::Left
		gi.lPressed = in.mouseButtonPressed(MouseButton::Left);
		gi.lReleased = in.mouseButtonReleased(MouseButton::Left);
		if (gSyntheticPointer) {
			// Held, pressed and released are three different facts and a caller only gives
			// one of them: down stays down, and the edges are worked out from the frame
			// before, which is exactly what a real button does.
			bool& wasDown = gPointerHistory[&window].syntheticWasDown;
			gi.lPressed = gSyntheticDown && !wasDown;
			gi.lReleased = !gSyntheticDown && wasDown;
			gi.lDown = gSyntheticDown;
			wasDown = gSyntheticDown;
		}

		gi.rDown = in.isMouseButtonDown(MouseButton::Right);      // MouseButton::Right
		gi.rPressed = in.mouseButtonPressed(MouseButton::Right);
		gi.rReleased = in.mouseButtonReleased(MouseButton::Right);

		gi.mDown = in.isMouseButtonDown(MouseButton::Middle);      // MouseButton::Middle
		gi.mPressed = in.mouseButtonPressed(MouseButton::Middle);
		gi.mReleased = in.mouseButtonReleased(MouseButton::Middle);
		// Click runs: presses close together in both time and space count up, so a
		// widget can tell a double click from two separate ones. Tracked here
		// rather than in the GUI because the timing is a platform concern.
		if (gi.lPressed) {
			constexpr double kDoubleClickSeconds = 0.4;
			constexpr float  kSlopPixels = 6.0f;
			const double now = windowSystemTime();
			PointerHistory& clicks = gPointerHistory[&window];
			const glm::vec2 delta = gi.pointer - clicks.pos;
			const bool sameSpot = std::abs(delta.x) <= kSlopPixels &&
			                      std::abs(delta.y) <= kSlopPixels;
			if (now - clicks.time <= kDoubleClickSeconds && sameSpot) {
				clicks.count = (clicks.count >= 3) ? 1 : clicks.count + 1;
			} else {
				clicks.count = 1;
			}
			clicks.time = now;
			clicks.pos = gi.pointer;
		}
		gi.clickCount = gPointerHistory[&window].count;
		gi.scroll = in.scroll().y;
		// A wheel nobody turned. Applied every frame it is set, so a large number means
		// "to the end of whatever this is" rather than a precise distance -- which is the
		// thing a capture of a scrolled view actually wants.
		if (gSyntheticScroll != 0.0f) gi.scroll = gSyntheticScroll;
		gi.dt = dtSeconds;
		gi.shift = in.isKeyDown(Key::LeftShift) || in.isKeyDown(Key::RightShift);
		gi.ctrl = in.isKeyDown(Key::LeftControl) || in.isKeyDown(Key::RightControl);
		gi.shift = gi.shift || gSyntheticShift;
		gi.ctrl  = gi.ctrl  || gSyntheticCtrl;
		gi.alt = in.isKeyDown(Key::LeftAlt) || in.isKeyDown(Key::RightAlt);

		// Distill typed text and edit keys from this frame's event queue, which
		// carries key repeats (held backspace/arrows) that the polled edges miss.
		for (const InputEvent& e : in.events()) {
			if (e.type == InputEventType::Char) {
				// GLFW reports a codepoint; GuiInput::typed is UTF-8, which is what a
				// text field stores and what the atlas looks glyphs up by. This used to
				// keep only 32..126, so a keyboard laid out for any language but English
				// dropped half of what it typed before anything could draw it.
				//
				// Control characters are still dropped -- 0x7F included, which is Delete
				// arriving as a character on some layouts, and would otherwise be typed
				// into the field as text.
				if (e.codepoint >= 32 && e.codepoint != 127) {
					Utf8::encode(e.codepoint, gi.typed);
				}
			} else if (e.type == InputEventType::Key &&
			           (e.action == InputAction::Press || e.action == InputAction::Repeat)) {
				gatherKey(gi, e.key, e.mods);
			}
		}
		// Every window hears it; the frame loop forgets it once they all have. Through the
		// same door a real key goes through, with its modifiers held for the frame -- so a
		// synthetic Tab moves the focus and a synthetic Ctrl+Z undoes, rather than only
		// reaching the shortcuts.
		for (const Chord& chord : gSyntheticChords) {
			gi.shift = gi.shift || (chord.mods & Chord::Shift);
			gi.ctrl  = gi.ctrl  || (chord.mods & Chord::Ctrl);
			gi.alt   = gi.alt   || (chord.mods & Chord::Alt);
			gatherKey(gi, chord.key, chord.mods);
		}
		gi.typed += gSyntheticText;
		// What an input method finished, as text, and what it is still composing.
		gi.typed += in.imeCommitted();
		gi.preedit = gSyntheticPreedit.empty() ? in.preedit() : gSyntheticPreedit;
		gi.preeditCaret = gSyntheticPreedit.empty() ? in.preeditCaret() : gSyntheticPreeditCaret;
		gi.droppedFiles = in.droppedFiles();
		gi.droppedFiles.insert(gi.droppedFiles.end(), gSyntheticFiles.begin(), gSyntheticFiles.end());

		// A frameless window's edges are the window's, not the interface's: over them the
		// pointer is a resize handle, and the interface hears nothing of it -- no hover on
		// the button at the very edge, and no press. A synthetic pointer is a picture
		// being taken, and pictures do not resize the window.
		// Where the left button went down: the last position the pointer reported before
		// the press in this frame's events, or, with no movement before it, where the
		// pointer was at the end of the last frame.
		glm::dvec2 pressAt = in.mousePosition() - in.mouseDelta();
		for (const InputEvent& e : in.events()) {
			if (e.type == InputEventType::MouseMove) pressAt = { e.x, e.y };
			else if (e.type == InputEventType::MouseButton && e.key == MouseButton::Left &&
			         e.action == InputAction::Press) break;
		}
		if (!gSyntheticPointer && window.followEdges(gi.lPressed, gi.lDown, pressAt.x, pressAt.y)) {
			gi.pointer = glm::vec2(-1.0e6f);
			gi.lDown = gi.lPressed = gi.lReleased = false;
			gi.rDown = gi.rPressed = gi.rReleased = false;
			gi.mDown = gi.mPressed = gi.mReleased = false;
			gi.scroll = 0.0f;
		}
		window.gui().begin(gi);
	}

	void forgetWindowInput(const Window* window) {
		gPointerHistory.erase(window);
	}
}
