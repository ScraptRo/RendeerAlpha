#include <GraphicalObjects/Gui/Shortcuts.h>
#include <Logger/Logger.h>
#include <algorithm>

// Shortcuts: widgets offer theirs while the tree is walked, and the chords of the frame
// are matched against them once it is over.
namespace RDA {

	void Shortcuts::offer(Widget* widget, const GuiInput& input) {
		if (widget && widget->shortcut.valid() && !input.chords.empty()) mOffered.push_back(widget);
	}

	void Shortcuts::offerIn(Widget& subtree, const GuiInput& input) {
		if (!input.chords.empty()) subtree.offerShortcuts(mOffered);
	}

	bool Shortcuts::dispatch(const GuiInput& input, const Focus& focus) {
		bool pressed = false;
		for (const Chord& chord : input.chords) {
			// Whatever has the keyboard hears its own keys first: a field is being typed
			// into, and a button or a list has Space, Enter, the arrows and Escape.
			if (focus.textFocus ? textFieldKeeps(chord) : (focus.id != 0 && focusedWidgetKeeps(chord)))
				continue;
			if (focus.pagesKept()) {
				const bool paging = chord.key == Key::Home || chord.key == Key::End ||
				                    chord.key == Key::PageUp || chord.key == Key::PageDown;
				const bool moving = paging || chord.key == Key::Up || chord.key == Key::Down;
				const uint8_t held = chord.mods;
				if (paging && held == 0) continue;
				if (focus.selectsKept()) {
					if (moving && held != 0 && (held & ~(Chord::Ctrl | Chord::Shift)) == 0) continue;
					if (held == Chord::Ctrl && (chord.key == Key::A || chord.key == Key::Space)) continue;
				}
			}

			// The pointers are this frame's: offered during the walk that just finished,
			// and cleared at the top of the next one.
			Widget* other = nullptr;
			Widget* chosen = pickShortcut(mOffered, chord, &other);
			if (!chosen) continue;

			// Two widgets that could both be pressed with the same keys right now is a
			// layout that has not decided what those keys mean. Said once per chord.
			if (other && std::find(mClashes.begin(), mClashes.end(), chord)
			             == mClashes.end()) {
				mClashes.push_back(chord);
				RDA_LOG_WARNING("shortcut " << chordName(chord) << " belongs to both '"
				                << other->id() << "' and '" << chosen->id() << "'; the one "
				                "drawn last, '" << chosen->id() << "', takes it");
			}
			chosen->pressShortcut();
			// Whatever it did, it was not input the cache can see.
			pressed = true;
		}
		return pressed;
	}
}
