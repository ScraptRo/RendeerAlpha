#include <GraphicalObjects/GuiTheme.h>
#include <Logger/Logger.h>
#include <Layout/Blueprint.h>
#include <cstring>

// Widget theming. A Theme is a set of named style variants, seeded with the engine's
// built-in look and extended from code (defineX) or an XML file. The XML format:
//
//   <theme>
//     <button name="primary">
//       <normal>#3A6AD0</normal>          <!-- #RGB, #RRGGBB, #RRGGBBAA -->
//       <hovered>74,122,224</hovered>     <!-- or r,g,b[,a] decimals    -->
//       <pressed>#2A5AC0</pressed>
//       <text>#FFFFFF</text>
//       <borderWidth>1</borderWidth>      <!-- scalars, not colors      -->
//       <radius>6</radius>
//     </button>
//     <button name="primarySmall" base="primary"><radius>3</radius></button>
//     <panel name="toolbar">
//       <body>#1E2228EB</body>
//       <accentHeight>2</accentHeight>
//     </panel>
//     <label name="muted"><color>#8A94A6</color></label>
//     <textfield name="ide" mode="code">
//       <background>#101218</background>
//       <language>python</language>       <!-- a language in Gui::syntax() -->
//       <syntax><keyword>#C586C0</keyword><string>#CE9178</string></syntax>
//     </textfield>
//   </theme>
//
// A variant with no name= targets "default" (re-basing the fallback). Fields left out
// are inherited, so partial overrides just work; where they are inherited *from* is:
// base="other" if given, else the same-named variant, else "default". A <textfield>
// with mode= instead starts from that mode's built-in defaults. Unknown elements and
// attributes are ignored, so a newer file stays loadable by older code.
namespace RDA {

	using Layout::Blueprint;
	using Layout::BlueprintNode;
	using Layout::BlueprintProp;
	using Layout::PropKind;
	using Layout::kNoParent;
	using Layout::loadBlueprintFile;

	namespace {
		bool hexVal(char c, int& v) {
			if (c >= '0' && c <= '9') { v = c - '0';      return true; }
			if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
			if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
			return false;
		}

		// "#RGB" / "#RRGGBB" / "#RRGGBBAA" or decimal "r,g,b" / "r,g,b,a" (any
		// non-digit separators). Leaves `out` untouched and returns false on garbage.
		bool parseColor(const char* text, uint32_t& out) {
			if (!text) return false;
			while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') ++text;

			if (*text == '#') {
				++text;
				int d[8], n = 0;
				for (; n < 8 && text[n]; ++n) {
					if (!hexVal(text[n], d[n])) break;
				}
				auto byte = [&](int i) { return (d[2 * i] << 4) | d[2 * i + 1]; };
				if (n == 3) { out = rgba(d[0] * 17, d[1] * 17, d[2] * 17); return true; }
				if (n == 6) { out = rgba(byte(0), byte(1), byte(2)); return true; }
				if (n == 8) { out = rgba(byte(0), byte(1), byte(2), byte(3)); return true; }
				return false;
			}

			int c[4] = { 0, 0, 0, 255 }, n = 0;
			for (const char* p = text; *p && n < 4; ) {
				while (*p && (*p < '0' || *p > '9')) ++p;
				if (!*p) break;
				int v = 0;
				while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
				c[n++] = v > 255 ? 255 : v;
			}
			if (n < 3) return false;
			out = rgba(c[0], c[1], c[2], c[3]);
			return true;
		}

		// Read a field into `out` when the node has one; otherwise leave it alone, which
		// is what makes a partial variant inherit the rest from its base.
		void readColor(const Blueprint& bp, const BlueprintNode& node, const char* key, uint32_t& out) {
			const std::string_view text = bp.text(node, key);
			if (text.empty()) return;
			uint32_t parsed = 0;
			if (parseColor(std::string(text).c_str(), parsed)) out = parsed;
		}
		void readFloat(const Blueprint& bp, const BlueprintNode& node, const char* key, float& out) {
			if (const BlueprintProp* p = bp.find(node, key)) {
				if (p->kind != static_cast<uint32_t>(PropKind::String)) out = p->number;
			}
		}
		void readBool(const Blueprint& bp, const BlueprintNode& node, const char* key, bool& out) {
			if (const BlueprintProp* p = bp.find(node, key)) {
				if (p->kind != static_cast<uint32_t>(PropKind::String)) out = p->number != 0.0f;
			}
		}
		// `fontSize` and `weight` on anything that draws text. Weight is a word rather
		// than a boolean because "weight: bold" is what a designer writes, and because a
		// real second face can be added later without the field having lied.
		void readFont(const Blueprint& bp, const BlueprintNode& node, TextStyle& out) {
			readFloat(bp, node, "fontSize", out.size);
			const std::string_view weight = bp.text(node, "weight");
			if (!weight.empty()) out.bold = (weight == "bold");
		}
		// `transitionMs` and `easing` on anything whose colours follow what it is doing.
		// Milliseconds because that is the unit a designer thinks in; seconds is what the
		// animation wants, and converting once here is cheaper than converting per frame.
		void readMotion(const Blueprint& bp, const BlueprintNode& node, MotionStyle& out) {
			float ms = out.seconds * 1000.0f;
			readFloat(bp, node, "transitionMs", ms);
			out.seconds = ms / 1000.0f;

			const std::string_view curve = bp.text(node, "easing");
			if (curve == "linear")     out.curve = Easing::Linear;
			else if (curve == "in")    out.curve = Easing::In;
			else if (curve == "inOut") out.curve = Easing::InOut;
			else if (curve == "out")   out.curve = Easing::Out;
		}
		void readString(const Blueprint& bp, const BlueprintNode& node, const char* key, std::string& out) {
			const std::string_view text = bp.text(node, key);
			if (!text.empty()) out = std::string(text);
		}

		Variant asVariant(const std::string& name) { return Variant(std::string_view(name)); }

		std::string variantName(const Blueprint& bp, const BlueprintNode& node) {
			std::string name(bp.string(node.id));
			if (name.empty()) name = "default";
			// Keep the text alongside the id, so a warning or a tool can print the name back
			// from an id. Comparing ids never needs this.
			RDL::intern(name.c_str());
			return name;
		}
		// Which variant a partial entry inherits from. In TypeScript this is usually done
		// with object spread and never reaches here; `base` remains for a variant that wants
		// to start from one it cannot name at that point in the file.
		std::string inheritFrom(const Blueprint& bp, const BlueprintNode& node,
		                        const std::string& name) {
			const std::string_view base = bp.text(node, "base");
			return base.empty() ? name : std::string(base);
		}

		// Each applier bases the new variant on the theme's current same-named variant
		// (which falls back to "default"), overrides the listed fields, then re-registers
		// it. Going through the public define*/accessor API keeps Theme's maps private.
		void applyDock(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			DockStyle s = t.dock(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "pane", s.pane);
			readColor(bp, node, "tabStrip", s.tabStrip);
			readColor(bp, node, "tab", s.tab);
			readColor(bp, node, "tabActive", s.tabActive);
			readColor(bp, node, "tabText", s.tabText);
			readColor(bp, node, "titleBar", s.titleBar);
			readColor(bp, node, "titleBarActive", s.titleBarActive);
			readColor(bp, node, "close", s.close);
			readColor(bp, node, "closeHover", s.closeHover);
			readColor(bp, node, "grip", s.grip);
			readColor(bp, node, "splitter", s.splitter);
			readColor(bp, node, "splitterHover", s.splitterHover);
			readColor(bp, node, "dropBand", s.dropBand);
			readColor(bp, node, "dropBandHot", s.dropBandHot);
			readColor(bp, node, "dropPane", s.dropPane);
			readColor(bp, node, "dropPreview", s.dropPreview);
			readFloat(bp, node, "tabHeight", s.tabHeight);
			readFloat(bp, node, "tabPadding", s.tabPadding);
			readFloat(bp, node, "closeWidth", s.closeWidth);
			readFloat(bp, node, "radius", s.radius);
			readMotion(bp, node, s.motion);
			t.defineDock(asVariant(name), s);
		}

		void applyButton(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			ButtonStyle s = t.button(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "normal", s.normal);
			readColor(bp, node, "hovered", s.hovered);
			readColor(bp, node, "pressed", s.pressed);
			readColor(bp, node, "text", s.text);
			readColor(bp, node, "border", s.border);
			readFloat(bp, node, "borderWidth", s.borderWidth);
			readFloat(bp, node, "radius", s.radius);
			readFont(bp, node, s.font);
			readMotion(bp, node, s.motion);
			t.defineButton(asVariant(name), s);
		}
		void applyCheckbox(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			CheckboxStyle s = t.checkbox(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "box", s.box);
			readColor(bp, node, "boxHover", s.boxHover);
			readColor(bp, node, "check", s.check);
			readColor(bp, node, "label", s.label);
			readColor(bp, node, "border", s.border);
			readFloat(bp, node, "borderWidth", s.borderWidth);
			readFloat(bp, node, "radius", s.radius);
			readFloat(bp, node, "checkInset", s.checkInset);
			readFont(bp, node, s.font);
			readMotion(bp, node, s.motion);
			t.defineCheckbox(asVariant(name), s);
		}
		void applySlider(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			SliderStyle s = t.slider(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "track", s.track);
			readColor(bp, node, "fill", s.fill);
			readColor(bp, node, "knob", s.knob);
			readColor(bp, node, "knobActive", s.knobActive);
			readFloat(bp, node, "knobWidth", s.knobWidth);
			readFloat(bp, node, "radius", s.radius);
			readMotion(bp, node, s.motion);
			t.defineSlider(asVariant(name), s);
		}
		void applyPanel(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			PanelStyle s = t.panel(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "body", s.body);
			readColor(bp, node, "accent", s.accent);
			readFloat(bp, node, "accentHeight", s.accentHeight);
			readColor(bp, node, "border", s.border);
			readFloat(bp, node, "borderWidth", s.borderWidth);
			readFloat(bp, node, "radius", s.radius);
			readMotion(bp, node, s.motion);
			t.definePanel(asVariant(name), s);
		}
		void applyFocus(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			FocusStyle s = t.focus(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "color", s.color);
			readFloat(bp, node, "width", s.width);
			readFloat(bp, node, "inset", s.inset);
			t.defineFocus(asVariant(name), s);
		}
		void applyBackground(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			BackgroundStyle s = t.background(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "color", s.color);
			readMotion(bp, node, s.motion);
			t.defineBackground(asVariant(name), s);
		}
		void applyLabel(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			LabelStyle s = t.label(asVariant(inheritFrom(bp, node, name)));
			readColor(bp, node, "color", s.color);
			readFont(bp, node, s.font);
			t.defineLabel(asVariant(name), s);
		}

		// A `syntax` object inside a text field variant: one colour per token kind. The
		// compiler turned that nested object into a child node, so this looks for one.
		void readSyntax(const Blueprint& bp, const BlueprintNode& parent, SyntaxStyle& syntax) {
			for (uint32_t i = 0; i < parent.childCount; ++i) {
				const BlueprintNode& child = bp.node(parent.firstChild + i);
				if (bp.string(child.type) != "syntax") continue;

				struct { const char* tag; TokenKind kind; } kinds[] = {
					{ "plain",        TokenKind::Plain },
					{ "keyword",      TokenKind::Keyword },
					{ "type",         TokenKind::Type },
					{ "string",       TokenKind::String },
					{ "number",       TokenKind::Number },
					{ "comment",      TokenKind::Comment },
					{ "operator",     TokenKind::Operator },
					{ "function",     TokenKind::Function },
					{ "preprocessor", TokenKind::Preprocessor },
				};
				for (const auto& k : kinds) {
					readColor(bp, child, k.tag, syntax.colors[static_cast<size_t>(k.kind)]);
				}
			}
		}

		void applyTextField(Theme& t, const Blueprint& bp, const BlueprintNode& node) {
			const std::string name = variantName(bp, node);
			// mode= starts from that mode's built-in defaults; otherwise inherit as usual.
			TextFieldStyle s;
			const std::string_view mode = bp.text(node, "mode");
			if (mode == "line")          s = TextFieldStyle::forMode(TextFieldMode::Line);
			else if (mode == "document") s = TextFieldStyle::forMode(TextFieldMode::Document);
			else if (mode == "code")     s = TextFieldStyle::forMode(TextFieldMode::Code);
			else {
				if (!mode.empty()) {
					RDA_LOG_WARNING("Unknown textfield mode '" << mode << "' on variant '" << name << "'");
				}
				s = t.textField(asVariant(inheritFrom(bp, node, name)));
			}

			readColor(bp, node, "background", s.background);
			readColor(bp, node, "text", s.text);
			readColor(bp, node, "caret", s.caret);
			readColor(bp, node, "selection", s.selection);
			readColor(bp, node, "gutter", s.gutter);
			readColor(bp, node, "lineNumber", s.lineNumber);
			readColor(bp, node, "currentLine", s.currentLine);
			readColor(bp, node, "border", s.border);
			readColor(bp, node, "scrollTrack", s.scrollTrack);
			readColor(bp, node, "scrollThumb", s.scrollThumb);
			readColor(bp, node, "scrollThumbHover", s.scrollThumbHover);
			readFloat(bp, node, "padding", s.padding);
			readFloat(bp, node, "borderWidth", s.borderWidth);
			readFloat(bp, node, "radius", s.radius);
			readFloat(bp, node, "caretWidth", s.caretWidth);
			readBool(bp, node, "readOnly", s.readOnly);
			readBool(bp, node, "multiline", s.multiline);
			readBool(bp, node, "showLineNumbers", s.showLineNumbers);
			readBool(bp, node, "highlightCurrentLine", s.highlightCurrentLine);
			readString(bp, node, "language", s.language);
			readSyntax(bp, node, s.syntax);
			readMotion(bp, node, s.motion);
			t.defineTextField(asVariant(name), s);
		}

		bool applyBlueprint(Theme& theme, const Blueprint& bp) {
			for (size_t i = 0; i < bp.nodeCount(); ++i) {
				const BlueprintNode& node = bp.node(i);
				// Only the top level: a nested object belongs to the variant that owns it.
				if (node.parent != kNoParent) continue;

				const std::string_view element = bp.string(node.type);
				if      (element == "button")    applyButton(theme, bp, node);
				else if (element == "checkbox")  applyCheckbox(theme, bp, node);
				else if (element == "slider")    applySlider(theme, bp, node);
				else if (element == "panel")     applyPanel(theme, bp, node);
				else if (element == "focus")     applyFocus(theme, bp, node);
				else if (element == "background") applyBackground(theme, bp, node);
				else if (element == "label")     applyLabel(theme, bp, node);
				else if (element == "textfield") applyTextField(theme, bp, node);
				else if (element == "dock")      applyDock(theme, bp, node);
				// Anything else is ignored on purpose: a theme written for a newer engine
				// still styles everything this one knows about.
			}
			return true;
		}
	}

	Theme::Theme() { reset(); }

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

	bool Theme::replaceWithFile(const std::string& path) {
		// Read before the reset, so a path that does not load leaves the interface wearing
		// the theme it already had rather than the built-in look.
		Blueprint blueprint;
		std::string error;
		if (!loadBlueprintFile(path, blueprint, error)) {
			RDA_LOG_WARNING("Theme not changed (" << path << "): " << error);
			return false;
		}
		reset();
		const bool ok = applyBlueprint(*this, blueprint);
		if (ok) {
			RDA_LOG_INFO("Changed GUI theme: " << path);
		}
		return ok;
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

	bool Theme::loadFromBlueprint(const Blueprint& blueprint) {
		if (!blueprint.valid()) return false;
		return applyBlueprint(*this, blueprint);
	}

	bool Theme::loadFromFile(const std::string& path) {
		Blueprint blueprint;
		std::string error;
		if (!loadBlueprintFile(path, blueprint, error)) {
			RDA_LOG_WARNING("Theme not loaded (" << path << "): " << error);
			return false;
		}
		const bool ok = applyBlueprint(*this, blueprint);
		// Braced: the logging macros compile to nothing outside a debug build, and an
		// unbraced `if` in front of one becomes an empty statement the compiler warns
		// about -- correctly, since it looks like a mistake.
		if (ok) {
			RDA_LOG_INFO("Loaded GUI theme: " << path);
		}
		return ok;
	}
}
