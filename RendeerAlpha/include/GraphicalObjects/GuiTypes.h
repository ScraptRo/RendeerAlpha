#pragma once
#include <Core/Datatypes.h>
#include <vendor/RDA_Library/inline_vector.h>
#include <vector>
#include <string>
#include <cstdint>

// Shared GUI value types, split out so both the immediate frontend (Gui) and the
// retained widget tree (Widget) can depend on them without a circular include.
namespace RDA {
	class Texture;

	// Pack RGBA (0..255) into the R8G8B8A8_UNORM layout the UI vertex expects. constexpr
	// so it can be a default argument.
	constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
		return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
		       (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
	}

	// The same colour at a fraction of its opacity. Animation's other half: something
	// arriving grows *and* fades in, and doing only one of those looks like a glitch.
	constexpr uint32_t fadeTo(uint32_t colour, float t) {
		const uint32_t alpha = (colour >> 24) & 0xFFu;
		const float scaled = static_cast<float>(alpha) * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
		return (colour & 0x00FFFFFFu) | (static_cast<uint32_t>(scaled + 0.5f) << 24);
	}

	// One key per animated property, mixed into a widget's id. Constants rather than
	// strings so a paint function costs an xor, and named so two properties of one widget
	// cannot collide by accident.
	inline constexpr uint32_t kMotionFill = 0x9E3779B9u; // a fill that follows hover/press
	inline constexpr uint32_t kMotionMark = 0x85EBCA6Bu; // a tick, growing in or out
	// A corner rounding on its way to another. Only a variant change moves it -- the
	// hover and press states share one radius -- and a variant may be a binding, so a
	// panel that becomes "selected" rounds off instead of snapping.
	inline constexpr uint32_t kMotionRadius = 0x7FEB352Du;
	inline constexpr uint32_t kMotionKnob = 0xC2B2AE35u; // a handle sliding to its value
	inline constexpr uint32_t kMotionOpen = 0x27D4EB2Fu; // something opening or closing
	// A widget's own rect, eased when the layout moves it. Four values live at this key
	// and the three after it -- x, y, w, h -- so it is spaced from its neighbours.
	inline constexpr uint32_t kMotionPlace = 0x165667B1u;
	// A scroll view's offset, glided to after a wheel notch. Two values, x then y.
	inline constexpr uint32_t kMotionScroll = 0x2545F491u;
	// How present a widget is: 1 in the layout, 0 collapsed out of it, and the space it
	// is given in between.
	inline constexpr uint32_t kMotionCollapse = 0x9E3779B1u;
	// There is one window, so the background needs one key rather than one per widget.
	inline constexpr uint32_t kMotionBackground = 0x3C6EF372u;

	// A button's text is centred in whatever rect it is given, and the style carries no
	// padding of its own -- so "as big as its content" has to decide what breathing room
	// means. This is that decision, and it lives here because two sides need the same
	// number: Button::measureContent, which asks for the room, and Gui::button, which
	// insets aligned text by it so a left-aligned label does not sit on the border.
	inline constexpr float kButtonPadX = 12.0f;
	inline constexpr float kButtonPadY = 6.0f;

	// Where text sits inside the box it was given, on one axis.
	//
	// Here rather than in Widget.h because both sides need it: the retained tree reads it
	// off a layout, and the immediate calls (Gui::button, Gui::label) are what act on it.
	// One vocabulary -- start / center / end -- for text and for children alike, so
	// `align` means the same thing wherever it is written.
	enum class TextAlign : uint8_t {
		Start,
		Center,
		End,
	};

	// The offset that puts `content` at `align` within `available`. Never negative:
	// content wider than its box starts at the edge and runs past it, which is what it
	// did before alignment existed and the only answer that keeps the start visible.
	inline float alignOffset(TextAlign align, float available, float content, float nudge = 0.0f) {
		float offset = 0.0f;
		switch (align) {
		case TextAlign::Start:  offset = 0.0f; break;
		case TextAlign::Center: offset = (available - content) * 0.5f; break;
		case TextAlign::End:    offset = available - content; break;
		}
		// The word is clamped, the nudge is not: overflow still starts at the near edge,
		// and "center+20" means twenty past the middle whichever side that lands on.
		return (offset > 0.0f ? offset : 0.0f) + nudge;
	}

	struct Rect {
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
		bool contains(glm::vec2 p) const {
			return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h;
		}
		// Touching edges do not count as overlapping, so a widget resting exactly on the
		// boundary of what is visible is treated as outside it.
		bool overlaps(const Rect& other) const {
			return x < other.x + other.w && other.x < x + w &&
			       y < other.y + other.h && other.y < y + h;
		}
	};

	// Navigation / editing keys an editable widget reacts to. Character input arrives
	// separately as text; these are the non-printing actions.
	enum class GuiEditKey : uint8_t {
		Backspace, Delete, Left, Right, Up, Down, Home, End, Enter, Tab,
		// Appended rather than filed in the middle: these travel in a blueprint's
		// compiled handlers nowhere, but they are stored in saved input traces and an
		// enumerator that changed value would change what those replay.
		Escape, Space,
	};

	// The pointer + keyboard state the GUI reacts to, in the target's pixel space. On a
	// window this is the mouse; for an in-world panel it is the raycast hit. The
	// decoupling seam — the GUI never reads the raw window input directly.
	struct GuiInput {
		glm::vec2 pointer{ 0.0f };
		glm::vec2 viewport{ 0.0f }; // target size in GUI pixels (for edge docking, etc.)
		bool  down = false;
		bool  pressed = false;
		bool  released = false;
		// How many presses in quick succession this one is: 1 a plain click, 2 a double
		// click, 3 a triple. Only meaningful on the frame `pressed` is set.
		int   clickCount = 1;
		float scroll = 0.0f;

		// Text entry, consumed by whichever widget holds keyboard focus.
		std::string typed; // printable characters entered this frame
		// Edit/navigation keys this frame (incl. repeats). A frame realistically carries a
		// handful, so these live inside the struct and never reach the heap; the spill
		// path is only there for a stuck key or a very long repeat burst.
		RDL::inline_vector<GuiEditKey, 16> editKeys;
		bool  shift = false;              // extend selection with movement keys
		bool  ctrl = false;              // word-wise movement; enables the actions below
		bool  alt = false;               // column (box) selection while dragging
		// Editing shortcuts, set by the engine when the combo is pressed this frame.
		bool  copy = false;              // Ctrl+C
		bool  cut = false;               // Ctrl+X
		bool  paste = false;             // Ctrl+V
		bool  selectAll = false;         // Ctrl+A
		// Ctrl+Enter. An editable field deliberately ignores it rather than inserting a
		// newline, leaving the application to decide what submitting means.
		bool  submit = false;
		float dt = 0.0f;                 // seconds since last frame (caret blink, animation)
	};

	// Which flavor of text field to present. Same editing core, different presentation
	// and behavior — see TextFieldStyle::forMode for the per-mode defaults.
	enum class TextFieldMode {
		Line,      // single-line classic input; Enter commits
		Document,  // multi-line text area with document padding
		Code,      // multi-line, monospace, line-number gutter + current-line highlight
	};

	// What a run of source text means, for syntax highlighting. A Language (Syntax.h)
	// classifies text into these; SyntaxStyle gives each one a color. Kept here rather
	// than in Syntax.h so TextFieldStyle can carry a palette without pulling in the
	// tokenizer.
	enum class TokenKind : uint8_t {
		Plain,        // anything unclassified
		Keyword,      // if / def / return ...
		Type,         // builtin types and constants (int, True, None ...)
		String,       // quoted literals, including multi-line ones
		Number,       // numeric literals
		Comment,      // line and block comments
		Operator,     // punctuation and operators
		Function,     // identifier used as a call
		Preprocessor, // C's #define, Python's @decorator
		Count
	};

	// Per-token-kind colors, carried by TextFieldStyle so a theme variant defines its
	// own palette. Defaults are a dark editor scheme; Plain doubles as the field's
	// normal text color when a language is active.
	struct SyntaxStyle {
		uint32_t colors[static_cast<size_t>(TokenKind::Count)] = {
			rgba(228, 230, 235), // Plain
			rgba(197, 134, 192), // Keyword
			rgba( 78, 201, 176), // Type
			rgba(206, 145, 120), // String
			rgba(181, 206, 168), // Number
			rgba(106, 153,  85), // Comment
			rgba(212, 212, 212), // Operator
			rgba(220, 220, 170), // Function
			rgba(155, 155, 255), // Preprocessor
		};
		uint32_t color(TokenKind kind) const { return colors[static_cast<size_t>(kind)]; }
	};

	// How a run of text is drawn, beyond its colour.
	//
	// A size of zero means the interface's base size, which is what everything that has no
	// opinion asks for -- so a style that says nothing about type keeps working and costs
	// nothing to resolve. Sizes are baked at startup; one that was not is drawn at the
	// nearest that was, rather than scaled into a blur.
	//
	// Bold is synthesised: the glyph is drawn twice, a pixel apart. That is not a real
	// bold face and does not pretend to be, but it needs no second font file, works with
	// whatever font an application ships, and leaves the metrics alone -- which matters,
	// because this font is monospaced and a bold word has to stay in its column.
	struct TextStyle {
		float size = 0.0f; // 0 = the interface's base size
		bool  bold = false;

		bool operator==(const TextStyle& other) const {
			return size == other.size && bold == other.bold;
		}
	};

	// How a value gets from one end to the other. Not a general curve library: four that
	// cover interface work, named for what they are for rather than for their maths.
	//
	// Declared here rather than beside the animation itself, because a theme names one and
	// a theme must not have to know how anything moves -- only what it should look like.
	enum class Easing : uint8_t {
		Linear,   // constant speed; right for a spinner, wrong for almost everything else
		Out,      // fast then settling. The default, and what a thing responding should do
		In,       // slow then quickening; for something leaving
		InOut,    // eased at both ends; for a longer move that should not start abruptly
	};

	// How a style's colours get from one to another when what a widget is doing changes.
	//
	// Zero is instant, which is what everything did before this existed and what a theme
	// that says nothing still gets. About 150ms is where a hover stops feeling like a
	// light switch and starts feeling like a response; past 300 it feels slow -- which is
	// why this is a theme decision rather than a hard-coded one.
	struct MotionStyle {
		float  seconds = 0.0f;
		Easing curve = Easing::Out;

		bool operator==(const MotionStyle& other) const {
			return seconds == other.seconds && curve == other.curve;
		}
	};

	// What is behind the whole interface: the colour the window starts each frame at,
	// before a single widget is drawn.
	//
	// One field and no variants worth speaking of, because there is one window. It is a
	// theme export rather than a config setting for the same reason every other colour is
	// -- it is a look, it is hot-reloadable, and a backend in any language gets it without
	// a call of its own.
	struct BackgroundStyle {
		uint32_t color = rgba(5, 5, 8);
		// It travels like every other colour here. Without this a theme swap would fade
		// every widget and change the ground under them in one frame, which reads as a
		// glitch rather than as the same event.
		MotionStyle motion = {};
	};

	struct TextFieldStyle {
		TextFieldMode mode = TextFieldMode::Line;
		bool  multiline = false;       // set by forMode
		bool  showLineNumbers = false; // set by forMode (Code)
		bool  highlightCurrentLine = false;
		bool  readOnly = false;
		float padding = 6.0f;

		uint32_t background  = rgba(18, 20, 26);
		uint32_t text        = rgba(228, 230, 235);
		uint32_t placeholder = rgba(125, 125, 125);
		uint32_t caret       = rgba(120, 170, 255);
		uint32_t selection   = rgba(60, 92, 150, 140);
		uint32_t gutter      = rgba(28, 30, 38);
		uint32_t lineNumber  = rgba(110, 120, 140);
		uint32_t currentLine = rgba(255, 255, 255, 14);

		// Frame + caret geometry.
		uint32_t border      = rgba(58, 62, 72);
		float    borderWidth = 0.0f;   // 0 = no border
		float    radius      = 0.0f;   // corner radius, in pixels
		float    caretWidth  = 2.0f;

		// Scroll bar (multiline fields). These are also the colours every *other* scroll
		// bar is drawn with -- a scroll view's, a two-axis scroll's, a list's -- because
		// a scroll bar looks the same wherever it is and there is no sense in theming it
		// four times.
		uint32_t scrollTrack      = rgba(28, 30, 38, 180);
		uint32_t scrollThumb      = rgba(70, 78, 96);
		uint32_t scrollThumbHover = rgba(120, 130, 160);

		// How long its scroll bar takes to follow: the handle to the pointer, and the bar
		// itself to appear or go once there is or is not something to scroll. See
		// MotionStyle: 0 seconds is instant, which is what a theme that says nothing gets.
		MotionStyle motion = {};

		// Syntax highlighting: the name of a language in Gui::syntax() (empty = off, the
		// whole field draws in `text`), plus the palette its tokens are drawn with.
		std::string language;
		SyntaxStyle syntax;

		// A style pre-configured for the given mode (colors/flags/padding).
		static TextFieldStyle forMode(TextFieldMode mode);
	};

	// ---- per-widget visual styles ------------------------------------------------
	// One plain-data struct per widget type. Their defaults are the engine's built-in
	// look (the same values the widgets used to hardcode), so a widget with no theme
	// looks exactly as before. A Theme holds named variants of these; a widget selects
	// one by name. Every field is a color unless noted, so the XML loader can treat
	// most of them uniformly. Keep these dependency-free (just colors + scalars).
	// The chrome around a dockable panel: its tab, its title bar when it floats, the
	// splitters between panes, and the guidance shown while one is being dragged.
	//
	// Every default below is the colour this was drawn with when it was written in
	// literals, so a theme that says nothing about docking looks exactly as it did.
	struct DockStyle {
		// The panel itself, and the strip its tabs sit in.
		uint32_t pane     = rgba(22, 24, 30, 245);
		uint32_t tabStrip = rgba(30, 33, 42);

		// One tab, and the one whose panel is showing.
		uint32_t tab       = rgba(40, 44, 54);
		uint32_t tabActive = rgba(58, 92, 168);
		uint32_t tabText   = rgba(232, 234, 240);

		// A floating panel's title bar. Kept apart from the tab because a window title
		// and a tab are different things wearing similar colours.
		uint32_t titleBar       = rgba(44, 48, 58);
		uint32_t titleBarActive = rgba(58, 92, 168);

		uint32_t close      = rgba(200, 200, 210);
		uint32_t closeHover = rgba(255, 180, 180);
		uint32_t grip       = rgba(120, 128, 145, 200); // the resize corner of a floating panel

		uint32_t splitter      = rgba(70, 76, 90, 120);
		uint32_t splitterHover = rgba(90, 140, 240, 170);

		// Shown only while a panel is being dragged: the four window edges, the pane
		// under the pointer, and the share of it the drop would take.
		uint32_t dropBand    = rgba(60, 68, 90, 90);
		uint32_t dropBandHot = rgba(90, 140, 240, 150);
		uint32_t dropPane    = rgba(90, 140, 240, 30);
		uint32_t dropPreview = rgba(90, 140, 240, 90);

		float tabHeight  = 22.0f;
		float tabPadding = 10.0f; // inset before a tab's text, and after it
		float closeWidth = 18.0f;
		float radius     = 0.0f;  // corner radius for the tab and the panel
		MotionStyle motion = {}; // see MotionStyle: 0 seconds is instant
	};

	struct ButtonStyle {
		uint32_t normal  = rgba(58, 62, 72);
		uint32_t hovered = rgba(80, 86, 100);
		uint32_t pressed = rgba(42, 106, 208);
		uint32_t text    = rgba(235, 236, 240);
		uint32_t border      = rgba(90, 96, 112);
		float    borderWidth = 0.0f; // 0 = no border
		float    radius      = 0.0f; // corner radius, in pixels
		TextStyle font = {}; // see TextStyle: 0 means the base size
		MotionStyle motion = {}; // see MotionStyle: 0 seconds is instant
	};

	struct CheckboxStyle {
		uint32_t box      = rgba(58, 62, 72);
		uint32_t boxHover = rgba(80, 86, 100);
		uint32_t check    = rgba(120, 180, 255);
		uint32_t label    = rgba(228, 230, 235);
		uint32_t border      = rgba(90, 96, 112);
		float    borderWidth = 0.0f;
		float    radius      = 0.0f;
		float    checkInset  = 0.28f; // check size as a fraction of the box
		TextStyle font = {}; // see TextStyle: 0 means the base size
		MotionStyle motion = {}; // see MotionStyle: 0 seconds is instant
	};

	struct SliderStyle {
		uint32_t track      = rgba(40, 44, 52);
		uint32_t fill       = rgba(60, 110, 200);
		uint32_t knob       = rgba(150, 170, 210);
		uint32_t knobActive = rgba(200, 220, 255);
		float    knobWidth  = 8.0f;
		float    radius     = 0.0f; // rounds the track, fill and knob
		MotionStyle motion = {}; // see MotionStyle: 0 seconds is instant
	};

	// The ring around whatever has the keyboard. One style for every widget, because it
	// answers a question about the keyboard rather than about the widget: a reader
	// looking for "where does typing go" should find the same mark each time.
	struct FocusStyle {
		uint32_t color = rgba(110, 156, 240);
		float    width = 2.0f;  // how thick the ring is
		float    inset = -2.0f; // negative sits outside the widget's own edge
	};

	struct PanelStyle {
		uint32_t body         = rgba(28, 30, 36, 235);
		uint32_t accent       = rgba(74, 106, 208);
		float    accentHeight = 3.0f; // height of the top accent strip, in pixels
		uint32_t border       = rgba(58, 62, 72);
		float    borderWidth  = 0.0f;
		float    radius       = 0.0f;
		// A panel reacts to nothing on its own, but its variant may be a binding -- a row
		// that becomes the selected one, a card that becomes the active one -- and then
		// its body and its corners are changing for exactly the reason a button's do.
		MotionStyle motion = {};
	};

	struct LabelStyle {
		uint32_t  color = rgba(230, 230, 235);
		TextStyle font = {}; // see TextStyle: 0 means the base size
	};

	struct GuiVertex {
		glm::vec2 pos;
		glm::vec2 uv;
		uint32_t  color; // R8G8B8A8_UNORM, see rgba()
	};

	// A run of indices sharing one clip rect (x0, y0, x1, y1) in pixels and one texture.
	// texture == nullptr means the font atlas (solid quads + text); non-null is an image
	// (e.g. the scene rendered by a Viewport widget), drawn with the image pipeline.
	struct GuiDrawCmd {
		glm::vec4      clip;
		uint32_t       indexOffset;
		uint32_t       indexCount;
		const Texture* texture = nullptr;
	};

	struct GuiDrawData {
		std::vector<GuiVertex>  vertices;
		std::vector<uint16_t>   indices;
		std::vector<GuiDrawCmd> commands;
		void clear() { vertices.clear(); indices.clear(); commands.clear(); }
	};
}
