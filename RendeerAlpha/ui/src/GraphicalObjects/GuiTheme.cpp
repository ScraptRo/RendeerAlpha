#include <GraphicalObjects/GuiTheme.h>
#include <Logger/Logger.h>
#include <cstring>

// Widget theming. A Theme is a set of named style variants, seeded with the engine's
// built-in look and extended from code (defineX) or a compiled theme file. A variant with
// no name targets "default" (re-basing the fallback); fields left out are inherited, so a
// partial override just works. Reading the file is the blueprint layer's job -- see
// Layout/ThemeLoader -- so this registry knows nothing about the format it arrives in.
namespace RDA {


	Theme::Theme() { reset(); }

	// The three presets a text field's mode means, as a style. Here rather than beside the
	// widget that draws one: it builds a style and nothing else, and every other line that
	// builds one is in this file. It also means a theme can be read without linking the
	// immediate-mode widgets, which is what lets the tests cover it.
	TextFieldStyle TextFieldStyle::forMode(TextFieldMode mode) {
		TextFieldStyle style;
		style.mode = mode;
		switch (mode) {
		case TextFieldMode::Line:
			style.multiline = false;
			style.padding = 6.0f;
			break;
		case TextFieldMode::Document:
			style.multiline = true;
			style.padding = 12.0f;
			style.background = rgba(24, 24, 28);
			break;
		case TextFieldMode::Code:
			style.multiline = true;
			style.showLineNumbers = true;
			style.highlightCurrentLine = true;
			style.padding = 6.0f;
			style.background = rgba(16, 18, 24);
			break;
		}
		return style;
	}

	void Theme::forgetResolvedMeshes() {
		// Buttons are the only styles that name a mesh today. When a second one does, it
		// belongs in this loop -- which is the whole reason this is one function rather
		// than two lines at the call site.
		for (auto& one : mButtons) {
			one.second.meshColours.clear();
			one.second.meshResolvedFor = nullptr;
		}
		// The look may have changed, and the revision is what tells the GUI that last
		// frame's geometry cannot simply be shown again.
		++mRevision;
	}

	void Theme::reset() {
		mButtons.clear();
		mCheckboxes.clear();
		mSliders.clear();
		mPanels.clear();
		mFocusRings.clear();
		mBackgrounds.clear();
		mLabels.clear();
		mTextFields.clear();
		mDocks.clear();

		// Seed the always-present "default" variant of every widget type from the
		// struct defaults (the engine's built-in look).
		mButtons[kDefaultVariant]    = ButtonStyle{};
		mCheckboxes[kDefaultVariant] = CheckboxStyle{};
		mSliders[kDefaultVariant]    = SliderStyle{};
		mPanels[kDefaultVariant]     = PanelStyle{};
		mFocusRings[kDefaultVariant] = FocusStyle{};
		mBackgrounds[kDefaultVariant] = BackgroundStyle{};
		mLabels[kDefaultVariant]     = LabelStyle{};
		mTextFields[kDefaultVariant] = TextFieldStyle{};
		mDocks[kDefaultVariant]      = DockStyle{};
		++mRevision;
	}

	namespace {
		Theme::FileReader gReader = nullptr;
	}

	void Theme::setFileReader(FileReader reader) { gReader = reader; }

	bool Theme::replaceWithFile(const std::string& path) {
		if (!gReader) {
			RDA_LOG_WARNING("Theme not changed (" << path << "): no theme reader is installed");
			return false;
		}
		return gReader(*this, path, /*replace*/ true);
	}

	const ButtonStyle&    Theme::button(Variant v)    const { return resolve(mButtons, v); }
	const DockStyle&      Theme::dock(Variant v)      const { return resolve(mDocks, v); }
	const CheckboxStyle&  Theme::checkbox(Variant v)  const { return resolve(mCheckboxes, v); }
	const SliderStyle&    Theme::slider(Variant v)    const { return resolve(mSliders, v); }
	const PanelStyle&     Theme::panel(Variant v)     const { return resolve(mPanels, v); }
	const FocusStyle&     Theme::focus(Variant v)     const { return resolve(mFocusRings, v); }
	const BackgroundStyle& Theme::background(Variant v) const { return resolve(mBackgrounds, v); }
	const LabelStyle&     Theme::label(Variant v)     const { return resolve(mLabels, v); }
	const TextFieldStyle& Theme::textField(Variant v) const { return resolve(mTextFields, v); }

	bool Theme::loadFromFile(const std::string& path) {
		if (!gReader) {
			RDA_LOG_WARNING("Theme not loaded (" << path << "): no theme reader is installed");
			return false;
		}
		return gReader(*this, path, /*replace*/ false);
	}
}
