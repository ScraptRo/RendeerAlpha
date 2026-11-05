#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>
#include <GraphicalObjects/GuiTheme.h>

namespace RDA {

	// An editable text field. `style` selects Line / Document / Code presentation.
	// onChange fires with the new contents on the frames it changes.
	//
	// Styling comes from `style` by default. Naming a theme variant instead replaces it
	// wholesale with the themed one — including its mode, colors and syntax language —
	// so a variant is the one place an editor's whole look is described.
	class TextField : public Widget {
	public:
		TextField(std::string id, TextFieldMode mode = TextFieldMode::Line)
			: Widget(std::move(id)), style(TextFieldStyle::forMode(mode)) {}

		// The contents, behind accessors because every write has to be noticed.
		//
		// Indexing a document -- where its lines begin, how many there are, where its
		// syntax spans fall -- means walking the whole string, and that used to happen
		// several times a frame whether or not anything had changed. A ten megabyte log
		// cost ten megabytes of scanning per frame to draw forty lines. The counter below
		// is what lets all of that be kept between frames instead, so a field is
		// re-indexed when it is edited rather than when it is looked at.
		const std::string& text() const { return mText; }
		void setText(std::string value) {
			// Compared rather than assigned blindly: re-applying the same text is not a
			// change, and treating it as one throws away the very caches this exists for.
			if (value == mText) return;
			mText = std::move(value);
			++mTextVersion;
		}

		const std::string& placeholder() const { return mPlaceholder; }
		void setPlaceholder(std::string value) {
			if (value == mPlaceholder) return;
			mPlaceholder = std::move(value);
		}

		// A completion to offer ahead of the caret. Not part of the value: it is drawn,
		// and it is taken with Tab or dropped with Escape, and until then the text is
		// exactly what was typed.
		void setSuggestion(std::string value) { mSuggestion = std::move(value); }
		const std::string& suggestion() const { return mSuggestion; }

		// Where the caret is, whenever it moves or the text changes. A completion is a
		// function of the prefix and the suffix, so the caret is the other half of what
		// an application needs -- onChange alone hands back the whole contents and no
		// idea where in it the reader is.
		std::function<void(float)> onCaret;
		// Tab took the suggestion; Escape dropped it. Handled inside the field, so the
		// application never sees a keystroke -- it sees that its offer was taken.
		std::function<void()>      onAccept;
		std::function<void()>      onDismiss;
		// The send gesture, whichever one `submitKey` names.
		std::function<void()>      onSubmit;

		// Which keystroke sends, overriding what the mode would choose. This is how a
		// chat composer is multi-line and still sends on Enter, with Shift+Enter for a
		// line -- the one combination the modes could not express. See SubmitKey.
		SubmitKey                  submitKey = SubmitKey::Default;

		// The grammar to colour with, overriding the variant's. A variant owns the
		// palette, which is a theme's business; which language is in the field is the
		// document's, and an application should not need a variant per grammar -- nor
		// be unable to colour one its theme never heard of.
		std::string                language;

		// Never zero, because zero is what a caller passes to say it cannot tell.
		uint64_t textVersion() const { return mTextVersion; }
		TextFieldStyle                         style;
		Variant                                variant = kDefaultVariant;
		// Whether this field held keyboard focus as of the last frame it painted. Set by
		// the engine; useful for deciding which of several editors a shortcut applies to.
		bool                                   focused = false;
		std::function<void(const std::string&)> onChange;
		// One line per newline plus padding. With height = SizeSpec::content() this is
		// what makes a log grow to fit what it holds, and the spec's max is what stops a
		// runaway paste from swallowing the panel.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		bool describe(AccessibleInfo& out) const override;
		void accessibleAction(AccessibleAction action, const std::string& value) override;

	private:
		std::string mText;
		std::string mPlaceholder;
		std::string mSuggestion;
		int         mLastCaret = -1;   // so onCaret fires on a move, not every frame
		uint64_t    mTextVersion = 1;
		// How many lines mText holds, and which version was counted. Height follows line
		// count, so measuring used to count newlines across the whole document every
		// frame a content-sized field was laid out.
		mutable size_t   mLines = 0;
		mutable uint64_t mLinesVersion = 0;
	};
}
