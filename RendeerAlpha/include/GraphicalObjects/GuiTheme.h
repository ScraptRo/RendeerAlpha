#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <Layout/Blueprint.h>
#include <vendor/RDA_Library/string_id.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace RDA {

	// Variants are keyed by an interned id rather than by text. A widget's variant is
	// hashed once — at compile time when it comes from a literal — so resolving a style
	// every frame is an integer compare instead of hashing a string and then comparing
	// characters. RDL::string_id converts implicitly from const char*/string_view, so
	// call sites still read as plain names.
	using Variant = RDL::string_id;

	template <typename Style>
	using VariantMap = std::unordered_map<Variant, Style>;

	// The variant every widget falls back to.
	inline constexpr Variant kDefaultVariant = Variant(std::string_view("default", 7));

	// A named palette of widget styles. For each widget type it holds a map of
	// variant-name -> style; a widget picks a variant by name and the immediate core
	// resolves it here at paint time. Every type always has a "default" variant (seeded
	// in the constructor from the built-in look), and any unknown name — a typo, or a
	// widget left unstyled — falls back to that default, so lookup never fails.
	//
	// Variants come from two places: defineX() from code, and loadFrom*() from an XML
	// theme file (see GuiTheme.cpp for the format). Both merge into the same maps, so a
	// file can extend a code-defined base and vice-versa. A variant defined from a
	// partial XML entry starts from "default" and overrides only the fields it lists.
	class Theme {
	public:
		Theme(); // seeds the "default" variant of every widget type

		// ---- variant lookup (used by the immediate widgets) ----
		// Returns the named variant, or the "default" variant when the name is unknown.
		const ButtonStyle&    button(Variant variant) const;
		const CheckboxStyle&  checkbox(Variant variant) const;
		const SliderStyle&    slider(Variant variant) const;
		const PanelStyle&     panel(Variant variant) const;
		// The ring around whatever holds the keyboard. One answer for every widget, so a
		// reader looking for "where does typing go" finds the same mark each time.
		const FocusStyle&     focus(Variant variant = kDefaultVariant) const;
		const BackgroundStyle& background(Variant variant = kDefaultVariant) const;
		const LabelStyle&     label(Variant variant) const;
		const TextFieldStyle& textField(Variant variant) const;
		const DockStyle&      dock(Variant variant) const;

		// ---- defining variants from code ----
		// Registers or overwrites a variant. Passing "default" re-bases the fallback.
		// Registers or overwrites a variant. Passing kDefaultVariant re-bases the fallback.
		void defineButton(Variant variant, const ButtonStyle& s)       { mButtons[variant] = s; ++mRevision; }
		void defineCheckbox(Variant variant, const CheckboxStyle& s)   { mCheckboxes[variant] = s; ++mRevision; }
		void defineSlider(Variant variant, const SliderStyle& s)       { mSliders[variant] = s; ++mRevision; }
		void definePanel(Variant variant, const PanelStyle& s)         { mPanels[variant] = s; ++mRevision; }
		void defineFocus(Variant variant, const FocusStyle& s)         { mFocusRings[variant] = s; ++mRevision; }
		void defineBackground(Variant variant, const BackgroundStyle& s) { mBackgrounds[variant] = s; ++mRevision; }
		void defineLabel(Variant variant, const LabelStyle& s)         { mLabels[variant] = s; ++mRevision; }
		void defineTextField(Variant variant, const TextFieldStyle& s) { mTextFields[variant] = s; ++mRevision; }
		void defineDock(Variant variant, const DockStyle& s)           { mDocks[variant] = s; ++mRevision; }

		// Whether a named variant exists for a given widget type (exact match, no
		// fallback) — handy for validating a name before relying on it.
		bool hasButton(Variant variant) const    { return mButtons.count(variant) != 0; }
		bool hasCheckbox(Variant variant) const  { return mCheckboxes.count(variant) != 0; }
		bool hasSlider(Variant variant) const    { return mSliders.count(variant) != 0; }
		bool hasPanel(Variant variant) const     { return mPanels.count(variant) != 0; }
		bool hasLabel(Variant variant) const     { return mLabels.count(variant) != 0; }
		bool hasTextField(Variant variant) const { return mTextFields.count(variant) != 0; }

		// ---- loading variants from a compiled theme ----
		// A theme is written in TypeScript and compiled to a blueprint by `rda theme`;
		// see Layout/ThemeCompiler.h. Merging leaves already-registered variants intact,
		// and an element this build does not know is ignored, so a theme written for a
		// newer engine still styles everything here.
		// Applies a compiled theme *on top of* what is already here: a variant the file
		// names is replaced, one it does not is left alone. That is what a theme loaded
		// beside code-defined variants should do, and what hot reload wants.
		bool loadFromFile(const std::string& path);
		bool loadFromBlueprint(const Layout::Blueprint& blueprint);

		// Back to the engine's built-in look, forgetting every variant anything defined.
		void reset();

		// What "switch to this theme" means: reset, then load. Without the reset a theme
		// that simply does not mention `button.danger` would inherit the last theme's,
		// and a light theme would come out with a dark button in it.
		bool replaceWithFile(const std::string& path);

		// Changed by every define and every load. The GUI keeps last frame's geometry when
		// nothing it can see has changed, and a theme swap is invisible to all of its other
		// checks -- same tree, same input, same everything except what colour it all is.
		uint64_t revision() const { return mRevision; }

	private:
		// Fallback lookup shared by the typed accessors: exact name, else "default"
		// (guaranteed present because the constructor seeds it). The map is transparent,
		// so `variant` is matched without building a std::string.
		template <typename Style>
		static const Style& resolve(const VariantMap<Style>& map, Variant variant) {
			auto it = map.find(variant);
			return it != map.end() ? it->second : map.find(kDefaultVariant)->second;
		}

		VariantMap<ButtonStyle>    mButtons;
		VariantMap<CheckboxStyle>  mCheckboxes;
		VariantMap<SliderStyle>    mSliders;
		VariantMap<PanelStyle>     mPanels;
		VariantMap<FocusStyle>     mFocusRings;
		VariantMap<BackgroundStyle> mBackgrounds;
		uint64_t mRevision = 0;
		VariantMap<DockStyle>      mDocks;
		VariantMap<LabelStyle>     mLabels;
		VariantMap<TextFieldStyle> mTextFields;
	};
}
