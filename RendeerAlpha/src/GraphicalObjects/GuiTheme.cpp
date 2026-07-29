#include <GraphicalObjects/GuiTheme.h>
#include <Logger/Logger.h>
#include <vendor/tinyxml2/tinyxml2.h>
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

	using tinyxml2::XMLDocument;
	using tinyxml2::XMLElement;

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

		// Read a color / scalar child element into `out` if present and valid; otherwise
		// leave `out` as-is, which is what makes partial overrides inherit the base.
		void readColor(const XMLElement* e, const char* tag, uint32_t& out) {
			const XMLElement* c = e->FirstChildElement(tag);
			if (!c) return;
			uint32_t parsed;
			if (parseColor(c->GetText(), parsed)) out = parsed;
		}
		void readFloat(const XMLElement* e, const char* tag, float& out) {
			const XMLElement* c = e->FirstChildElement(tag);
			if (c) c->QueryFloatText(&out); // unchanged on convert error
		}
		void readBool(const XMLElement* e, const char* tag, bool& out) {
			const XMLElement* c = e->FirstChildElement(tag);
			if (c) c->QueryBoolText(&out);
		}
		void readString(const XMLElement* e, const char* tag, std::string& out) {
			const XMLElement* c = e->FirstChildElement(tag);
			if (c && c->GetText()) out = c->GetText();
		}

		const char* variantName(const XMLElement* e) {
			const char* name = e->Attribute("name");
			name = (name && name[0]) ? name : "default";
			// Keep the text alongside the id, so a warning or a tool can print the name
			// back from an id. Comparing ids never needs this.
			RDL::intern(name);
			return name;
		}
		// Which variant a partial entry inherits from: base="other" when given,
		// otherwise the same-named variant (which itself falls back to "default").
		const char* inheritFrom(const XMLElement* e, const char* name) {
			const char* base = e->Attribute("base");
			return (base && base[0]) ? base : name;
		}

		// Each applier bases the new variant on the theme's current same-named variant
		// (which falls back to "default"), overrides the listed fields, then re-registers
		// it. Going through the public define*/accessor API keeps Theme's maps private.
		void applyButton(Theme& t, const XMLElement* e) {
			const char* name = variantName(e);
			ButtonStyle s = t.button(inheritFrom(e, name));
			readColor(e, "normal", s.normal);
			readColor(e, "hovered", s.hovered);
			readColor(e, "pressed", s.pressed);
			readColor(e, "text", s.text);
			readColor(e, "border", s.border);
			readFloat(e, "borderWidth", s.borderWidth);
			readFloat(e, "radius", s.radius);
			t.defineButton(name, s);
		}
		void applyCheckbox(Theme& t, const XMLElement* e) {
			const char* name = variantName(e);
			CheckboxStyle s = t.checkbox(inheritFrom(e, name));
			readColor(e, "box", s.box);
			readColor(e, "boxHover", s.boxHover);
			readColor(e, "check", s.check);
			readColor(e, "label", s.label);
			readColor(e, "border", s.border);
			readFloat(e, "borderWidth", s.borderWidth);
			readFloat(e, "radius", s.radius);
			readFloat(e, "checkInset", s.checkInset);
			t.defineCheckbox(name, s);
		}
		void applySlider(Theme& t, const XMLElement* e) {
			const char* name = variantName(e);
			SliderStyle s = t.slider(inheritFrom(e, name));
			readColor(e, "track", s.track);
			readColor(e, "fill", s.fill);
			readColor(e, "knob", s.knob);
			readColor(e, "knobActive", s.knobActive);
			readFloat(e, "knobWidth", s.knobWidth);
			readFloat(e, "radius", s.radius);
			t.defineSlider(name, s);
		}
		void applyPanel(Theme& t, const XMLElement* e) {
			const char* name = variantName(e);
			PanelStyle s = t.panel(inheritFrom(e, name));
			readColor(e, "body", s.body);
			readColor(e, "accent", s.accent);
			readFloat(e, "accentHeight", s.accentHeight);
			readColor(e, "border", s.border);
			readFloat(e, "borderWidth", s.borderWidth);
			readFloat(e, "radius", s.radius);
			t.definePanel(name, s);
		}
		void applyLabel(Theme& t, const XMLElement* e) {
			const char* name = variantName(e);
			LabelStyle s = t.label(inheritFrom(e, name));
			readColor(e, "color", s.color);
			t.defineLabel(name, s);
		}

		// <syntax> child of a <textfield>: one color per token kind.
		void readSyntax(const XMLElement* parent, SyntaxStyle& syntax) {
			const XMLElement* e = parent->FirstChildElement("syntax");
			if (!e) return;
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
				readColor(e, k.tag, syntax.colors[static_cast<size_t>(k.kind)]);
			}
		}

		void applyTextField(Theme& t, const XMLElement* e) {
			const char* name = variantName(e);
			// mode= starts from that mode's built-in defaults; otherwise inherit as usual.
			TextFieldStyle s;
			const char* mode = e->Attribute("mode");
			if (mode && std::strcmp(mode, "line") == 0)          s = TextFieldStyle::forMode(TextFieldMode::Line);
			else if (mode && std::strcmp(mode, "document") == 0) s = TextFieldStyle::forMode(TextFieldMode::Document);
			else if (mode && std::strcmp(mode, "code") == 0)     s = TextFieldStyle::forMode(TextFieldMode::Code);
			else {
				if (mode) {
					RDA_LOG_WARNING("Unknown textfield mode '" << mode << "' on variant '" << name << "'");
				}
				s = t.textField(inheritFrom(e, name));
			}

			readColor(e, "background", s.background);
			readColor(e, "text", s.text);
			readColor(e, "caret", s.caret);
			readColor(e, "selection", s.selection);
			readColor(e, "gutter", s.gutter);
			readColor(e, "lineNumber", s.lineNumber);
			readColor(e, "currentLine", s.currentLine);
			readColor(e, "border", s.border);
			readColor(e, "scrollTrack", s.scrollTrack);
			readColor(e, "scrollThumb", s.scrollThumb);
			readColor(e, "scrollThumbHover", s.scrollThumbHover);
			readFloat(e, "padding", s.padding);
			readFloat(e, "borderWidth", s.borderWidth);
			readFloat(e, "radius", s.radius);
			readFloat(e, "caretWidth", s.caretWidth);
			readBool(e, "readOnly", s.readOnly);
			readBool(e, "multiline", s.multiline);
			readBool(e, "showLineNumbers", s.showLineNumbers);
			readBool(e, "highlightCurrentLine", s.highlightCurrentLine);
			readString(e, "language", s.language);
			readSyntax(e, s.syntax);
			t.defineTextField(name, s);
		}

		bool applyDocument(Theme& theme, const XMLDocument& doc) {
			const XMLElement* root = doc.RootElement();
			if (!root) {
				RDA_LOG_ERROR("Theme XML has no root element");
				return false;
			}
			for (const XMLElement* e = root->FirstChildElement(); e; e = e->NextSiblingElement()) {
				const char* tag = e->Name();
				if      (std::strcmp(tag, "button")    == 0) applyButton(theme, e);
				else if (std::strcmp(tag, "checkbox")  == 0) applyCheckbox(theme, e);
				else if (std::strcmp(tag, "slider")    == 0) applySlider(theme, e);
				else if (std::strcmp(tag, "panel")     == 0) applyPanel(theme, e);
				else if (std::strcmp(tag, "label")     == 0) applyLabel(theme, e);
				else if (std::strcmp(tag, "textfield") == 0) applyTextField(theme, e);
				// unknown tags are ignored on purpose (forward compatibility)
			}
			return true;
		}
	}

	Theme::Theme() {
		// Seed the always-present "default" variant of every widget type from the
		// struct defaults (the engine's built-in look).
		mButtons[kDefaultVariant]    = ButtonStyle{};
		mCheckboxes[kDefaultVariant] = CheckboxStyle{};
		mSliders[kDefaultVariant]    = SliderStyle{};
		mPanels[kDefaultVariant]     = PanelStyle{};
		mLabels[kDefaultVariant]     = LabelStyle{};
		mTextFields[kDefaultVariant] = TextFieldStyle{};
	}

	const ButtonStyle&    Theme::button(Variant v)    const { return resolve(mButtons, v); }
	const CheckboxStyle&  Theme::checkbox(Variant v)  const { return resolve(mCheckboxes, v); }
	const SliderStyle&    Theme::slider(Variant v)    const { return resolve(mSliders, v); }
	const PanelStyle&     Theme::panel(Variant v)     const { return resolve(mPanels, v); }
	const LabelStyle&     Theme::label(Variant v)     const { return resolve(mLabels, v); }
	const TextFieldStyle& Theme::textField(Variant v) const { return resolve(mTextFields, v); }

	bool Theme::loadFromString(const char* xml) {
		XMLDocument doc;
		if (doc.Parse(xml) != tinyxml2::XML_SUCCESS) {
			RDA_LOG_ERROR("Failed to parse theme XML: " << doc.ErrorStr());
			return false;
		}
		return applyDocument(*this, doc);
	}

	bool Theme::loadFromFile(const std::string& path) {
		XMLDocument doc;
		if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
			RDA_LOG_WARNING("Theme file not loaded (" << path << "): " << doc.ErrorStr());
			return false;
		}
		bool ok = applyDocument(*this, doc);
		if (ok) {
			RDA_LOG_INFO("Loaded GUI theme: " << path);
		}
		return ok;
	}
}
