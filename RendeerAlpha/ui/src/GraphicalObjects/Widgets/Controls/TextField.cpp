#include <GraphicalObjects/Widgets/Controls/TextField.h>
#include "../WidgetInternal.h"
#include <GraphicalObjects/Gui.h>

namespace RDA {

	using detail::either;

	glm::vec2 TextField::measureContent(Gui& gui, glm::vec2 available) const {
		const TextFieldStyle& used = (variant != kDefaultVariant) ? gui.theme().textField(variant) : style;
		// Counted once per edit rather than once per measure: a content-sized field is
		// measured at least once a frame, and counting newlines walks the whole document.
		if (mLinesVersion != mTextVersion) {
			mLines = 1;
			for (char c : mText) {
				if (c == '\n') ++mLines;
			}
			mLinesVersion = mTextVersion;
		}
		size_t lines = mLines;
		// A single-line field is exactly one row regardless of what it holds.
		if (!used.multiline) lines = 1;
		const float height = static_cast<float>(lines) * gui.lineHeight() + used.padding * 2.0f;
		return { available.x, height };
	}

	void TextField::paint(Gui& gui, glm::vec2 origin) {
		// With an explicit size use it; otherwise fill the container body (the clip rect),
		// so the field grows and shrinks as its container is resized.
		// A zero size still means "fill the container" — the shorthand that predates
		// anchoring, and identical to Anchor::Fill with no margins. An explicit anchor
		// wins over it, so a filled-but-inset layout is expressible.
		// A layout parent's decision always wins over both.
		const bool legacyFill = !mArrangedValid && rect.w <= 0.0f && rect.h <= 0.0f &&
		                        anchor == Anchor::TopLeft;
		Rect abs = legacyFill ? gui.currentClipRect() : placement(gui, origin);
		// A named variant supplies the whole style; otherwise the per-instance one.
		const TextFieldStyle& used = (variant != kDefaultVariant) ? gui.theme().textField(variant) : style;
		Gui::TextFieldExtras extras;
		int  caretNow = mLastCaret;
		bool accepted = false;
		bool dismissed = false;
		bool submitted = false;
		extras.suggestion = mSuggestion.empty() ? nullptr : mSuggestion.c_str();
		extras.language = language.empty() ? nullptr : language.c_str();
		extras.submit = submitKey;
		extras.caret = &caretNow;
		extras.accepted = &accepted;
		extras.dismissed = &dismissed;
		extras.submitted = &submitted;

		if (gui.textField(mId.c_str(), mText, mPlaceholder, abs, used, &focused,
		                  mTextVersion, &extras)) {
			// The field edited the string in place, so nothing else can have noticed.
			++mTextVersion;
			if (onChange) onChange(mText);
		}

		// Taken or dropped, the offer is spent. Cleared here rather than left to the
		// application, so a suggestion cannot survive the keystroke that answered it and
		// be offered again on the next frame.
		if (accepted) {
			mSuggestion.clear();
			if (onAccept) onAccept();
		} else if (dismissed) {
			mSuggestion.clear();
			if (onDismiss) onDismiss();
		}
		// After onChange, so a handler that reads the text sees what was just typed
		// rather than what was there before the last keystroke.
		if (submitted && onSubmit) onSubmit();

		// Only on a move. This runs every frame the field paints, and a handler that fired
		// sixty times a second while nothing happened would be asked to debounce something
		// the engine already knows the answer to.
		if (caretNow != mLastCaret) {
			mLastCaret = caretNow;
			if (onCaret) onCaret(static_cast<float>(caretNow));
		}
	}

	bool TextField::describe(AccessibleInfo& out) const {
		out.role = (style.mode == TextFieldMode::Line) ? AccessibleRole::Edit : AccessibleRole::Document;
		out.name = either(alt, mPlaceholder);
		out.value = mText;
		out.focusable = true;
		out.readOnly = style.readOnly;
		if (!style.readOnly) out.actions = kCanSetValue;
		return true;
	}
	void TextField::accessibleAction(AccessibleAction action, const std::string& text) {
		if (action != AccessibleAction::SetValue || style.readOnly || text == mText) return;
		setText(text);
		if (onChange) onChange(mText);
	}
}
