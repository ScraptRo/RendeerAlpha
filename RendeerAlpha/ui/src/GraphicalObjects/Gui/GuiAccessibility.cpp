#include <GraphicalObjects/Gui.h>
#include <cstdio>
#include <string>
#include <utility>

// Gui, what a screen reader asked for: the collector queues it, and Gui acts on it, because
// acting means moving the focus and editing a field's state, which are Gui's.
namespace RDA {

	// ---- the accessibility tree ----------------------------------------------------------

	void Gui::applyAccessibleRequests(Widget* widget, Widget* owner, int item, uint32_t id) {
		for (const AccessibleRequest& one : mA11y.takeRequestsFor(id)) {
			if (one.action == AccessibleAction::SetSelection) {
				// Where a screen reader put the caret, or what it selected, in characters --
				// turned into the bytes the field indexes by, and set on its state the way a
				// click sets it.
				auto* field = dynamic_cast<TextField*>(widget);
				int from = 0, to = 0;
				if (field && std::sscanf(one.value.c_str(), "%d,%d", &from, &to) == 2) {
					const std::string& text = field->text();
					// The byte the n-th character begins at, or the end.
					auto bytesAt = [&](int characters) {
						int seen = 0;
						for (size_t b = 0; b < text.size(); ++b) {
							if ((static_cast<unsigned char>(text[b]) & 0xC0) == 0x80) continue;
							if (seen == characters) return static_cast<int>(b);
							++seen;
						}
						return static_cast<int>(text.size());
					};
					TextState& st = mTextStates[id];
					st.caret = bytesAt(to);
					st.selectAnchor = (from == to) ? -1 : bytesAt(from);
					st.boxMode = false;
					st.blink = 0.0f;
				}
			} else if (one.action == AccessibleAction::Focus) {
				// Moved the way Tab moves it, ring and all: somebody following along on
				// screen should see where the reader has gone.
				mFocus.setByKeyboard(id);
			} else if (owner) {
				owner->accessibleItemAction(item, one.action, one.value);
			} else if (widget) {
				widget->accessibleAction(one.action, one.value);
			}
			mAwake = true;
		}
	}

	void Gui::noteAccessible(Widget* widget, const Rect& where) {
		uint32_t id = 0;
		if (mA11y.noteWidget(widget, where, mInteraction, id)) applyAccessibleRequests(widget, nullptr, -1, id);
	}

	void Gui::noteAccessibleItem(Widget* owner, uint32_t id, int item, Widget* row,
	                             const Rect& where, const AccessibleInfo& info,
	                             const std::string& automationId, bool focused) {
		if (mA11y.noteItem(owner, id, item, row, where, info, automationId, focused)) {
			applyAccessibleRequests(nullptr, owner, item, id);
		}
	}

	void Gui::noteAccessibleText(uint32_t id, AccessibleText text) {
		mA11y.noteText(id, std::move(text));
	}
}
