#pragma once
#include <Core/Datatypes.h>
#include <GraphicalSrc/FontAtlas.h>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/GuiTheme.h>
#include <GraphicalObjects/Syntax.h>
#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/Docking.h>
#include <vendor/RDA_Library/frame_arena.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace RDA {

	// The window's GUI: an immediate-mode core plus a retained widget tree, sharing one
	// draw list and one interaction model. Widgets called directly (button/label/...) are
	// immediate — re-declared every frame. Nodes added to retained() persist until removed
	// and are walked automatically each frame, emitting the same immediate calls. The
	// retained tree is the base; immediate calls layer on top of it (and win input ties),
	// which is what makes an immediate debug overlay coexist with a retained game UI.
	//
	// The only cross-frame state is interaction (hot/active, keyed by a stable id) and the
	// reused draw buffers; that id system underpins both modes.
	class Gui {
	public:
		// Binds the CPU-side font metrics (the atlas texture is the backend's concern).
		void init(const FontAtlas* font) { mFont = font; }

		// Frame boundaries — driven by the engine, around the app's onUpdate. begin() also
		// walks the retained tree, so retained widgets draw beneath this frame's immediate
		// calls.
		void begin(const GuiInput& input);
		void end();

		// The persistent widget tree. Build it once (e.g. in onStart), mutate it at
		// runtime; it is walked every frame automatically.
		Container& retained() { return mRetainedRoot; }

		// The dock space: code-defined containers the user can drag and dock to window
		// edges. Laid out and drawn automatically each frame, above the retained tree.
		DockSpace& docking() { return mDockSpace; }

		// The widget theme: named style variants for this window's GUI. Define variants
		// in code (theme().defineButton(...)) or load them from an XML file
		// (theme().loadFromFile(...) / the GuiConfig::themePath at startup). Widgets
		// select a variant by name; an unknown name renders with the "default" variant.
		Theme& theme() { return mTheme; }
		const Theme& theme() const { return mTheme; }

		// The languages available for syntax highlighting. "python" is built in; more
		// can be registered from code (syntax().define(...)) or loaded from XML
		// (syntax().loadFromFile(...) / the GuiConfig::languagesPath at startup). A text
		// field highlights when its style names one (TextFieldStyle::language).
		SyntaxRegistry& syntax() { return mSyntax; }
		const SyntaxRegistry& syntax() const { return mSyntax; }

		// The input for the current frame (pointer/keyboard), for custom widgets/docking.
		const GuiInput& input() const { return mInput; }

		// Scratch memory for things that live exactly one frame. Rewound in begin(), so
		// anything taken from it is valid until the next frame starts and must not be
		// stored past it. Widget and layout code can take temporary arrays from here
		// instead of allocating: after a few frames the arena has grown to the busiest
		// frame's needs and stops touching the heap entirely.
		RDL::frame_arena& frameArena() { return mFrameArena; }

		// ---- low-level drawing (for custom widgets and the dock chrome) ----
		void drawRect(const Rect& rect, uint32_t color);
		void drawText(const char* text, glm::vec2 topLeft, uint32_t color);
		void image(const Rect& rect, const Texture* texture); // full-texture quad
		void pushClipRect(const Rect& rect);
		void popClipRect();
		Rect currentClipRect() const; // the active clip, e.g. a container body — for fill widgets
		float measureText(const char* text) const;
		float lineHeight() const;

		// Push/pop an id scope: widget ids created between them are namespaced by `id`, so
		// two instances of the same content (e.g. two spawned containers) don't share
		// hot/active/focus/edit state. Panels and dock containers do this automatically.
		void pushId(const char* id);
		void popId();

		// The offscreen scene texture, set by the engine each frame in Widget viewport
		// mode (null in Fullscreen mode). A Viewport widget draws it.
		void setSceneTexture(const Texture* texture) { mSceneTexture = texture; }
		const Texture* sceneTexture() const { return mSceneTexture; }

		// A Viewport widget reports its on-screen rect here (GUI pixels) so the engine can
		// size the offscreen scene target to match — the aspect-correct rendering path.
		void setViewportRect(const Rect& rect) { mViewportRect = rect; }
		Rect viewportRect() const { return mViewportRect; }

		// OS clipboard access for editable widgets (Ctrl+C/X/V). Wired by the engine to
		// the window system; unset means copy/paste do nothing.
		void setClipboardHandlers(std::function<std::string()> get, std::function<void(const char*)> set) {
			mGetClipboard = std::move(get);
			mSetClipboard = std::move(set);
		}

		// ---- widgets (immediate) ----
		// The styled widgets take an optional theme variant name; it resolves against
		// theme() (falling back to "default"), so omitting it keeps the built-in look.
		void beginPanel(const char* id, const Rect& rect, Variant variant = kDefaultVariant);
		void endPanel();
		void label(const char* text, glm::vec2 pos, uint32_t color = rgba(230, 230, 235));
		bool button(const char* id, const char* text, const Rect& rect, Variant variant = kDefaultVariant);
		// Toggles `value` on click; returns true the frame it changed.
		bool checkbox(const char* id, const char* label, bool& value, const Rect& rect, Variant variant = kDefaultVariant);
		// Drags `value` within [minValue, maxValue]; returns true while it changes.
		bool sliderFloat(const char* id, float& value, float minValue, float maxValue, const Rect& rect, Variant variant = kDefaultVariant);
		// An editable text field; edits `text` in place, returns true the frames it
		// changes. The style (Line / Document / Code) governs layout and behavior.
		bool textField(const char* id, std::string& text, const Rect& rect, const TextFieldStyle& style);

		// Whether any text field currently holds keyboard focus.
		bool hasKeyboardFocus() const { return mFocused != 0; }

		const GuiDrawData& drawData() const { return mDraw; }

		// A fingerprint of the geometry produced this frame, updated by end(). Two frames
		// with the same version are pixel-identical, which lets the renderer skip the
		// vertex/index upload and lets an on-demand loop skip the frame entirely. It is
		// derived from the built draw data rather than from change notifications, so no
		// widget mutation can slip past it.
		uint64_t drawVersion() const { return mDrawVersion; }
		// Whether this frame's draw data differs from the previous frame's.
		bool drawChanged() const { return mDrawChanged; }

	private:
		// Per-text-field edit state, kept across frames keyed by the field's stable id.
		struct TextState {
			int   caret = 0;          // character index into the field's text
			int   selectAnchor = -1;  // linear-selection anchor index (-1 = no selection)
			bool  boxMode = false;    // Alt column (box) selection active
			int   boxAnchorLine = 0;  // box selection anchor (line, col)
			int   boxAnchorCol = 0;
			float scrollX = 0.0f;
			float scrollY = 0.0f;
			float blink = 0.0f;   // caret blink accumulator; reset on any edit/move
			float scrollGrab = 0.0f; // pointer offset within the scroll thumb while dragging it

			// Cached syntax highlighting. Re-lexed only when the text or the language
			// changes, so an idle editor costs nothing per frame.
			std::vector<Token> tokens;
			size_t             tokenHash = 0;
			const Language*    tokenLang = nullptr;

			// Scratch for the line-start table, kept across frames so recomputing it
			// (several times per frame while editing) reuses this capacity instead of
			// allocating a fresh vector each time.
			std::vector<int> lineStarts;
		};
		uint32_t hashId(const char* str) const;   // FNV-1a
		uint32_t scopedId(const char* id) const;   // combine with the panel scope

		void addQuad(float x0, float y0, float x1, float y1,
		             float u0, float v0, float u1, float v1, uint32_t color);
		void addRect(const Rect& r, uint32_t color);
		// Rounded fill; falls back to addRect when the radius rounds away to nothing.
		void addRectRounded(const Rect& r, uint32_t color, float radius);
		// A filled rect with an optional border, drawn as a border-colored shape with the
		// fill inset on top — so one path covers square and rounded frames alike.
		void addFrame(const Rect& r, uint32_t fill, uint32_t border, float borderWidth, float radius);
		void addText(float penX, float baselineY, const char* text, uint32_t color);
		// Draws `count` characters starting at `text` and returns the pen X after the
		// run, so colored runs (syntax highlighting) chain without copying substrings.
		float addTextRange(float penX, float baselineY, const char* text, int count, uint32_t color);
		void setTexture(const Texture* texture); // flush + switch the active texture

		void pushClip(const glm::vec4& clip);
		void popClip();
		void flushCmd();

		const FontAtlas* mFont = nullptr;
		GuiInput         mInput;
		GuiDrawData      mDraw;

		uint32_t mHot = 0;     // id under the pointer this frame
		uint32_t mActive = 0;  // id the pointer went down on (persists across frames)

		std::vector<glm::vec4> mClipStack;
		std::vector<uint32_t>  mScopeStack;
		glm::vec4              mCurrentClip{ 0.0f };
		const Texture*         mCurrentTexture = nullptr; // texture of the in-progress command
		const Texture*         mSceneTexture = nullptr;   // offscreen scene, for Viewport widgets
		Rect                   mViewportRect{};           // last Viewport widget's rect this frame
		uint32_t               mCmdStart = 0; // first index of the in-progress command

		uint64_t mDrawVersion = 0;   // fingerprint of this frame's draw data
		bool     mDrawChanged = true; // whether it differs from the previous frame

		uint32_t mFocused = 0;       // id of the keyboard-focused text field, 0 = none
		bool     mFocusClaimed = false; // a focused widget kept/took focus this frame
		std::unordered_map<uint32_t, TextState> mTextStates;
		std::function<std::string()>     mGetClipboard;
		std::function<void(const char*)> mSetClipboard;

		Container      mRetainedRoot{ "__root" }; // invisible root of the persistent tree
		DockSpace      mDockSpace;                // dockable containers, above the tree
		Theme          mTheme;                    // named style variants for this window's GUI
		SyntaxRegistry mSyntax;                   // languages for text-field highlighting
		RDL::frame_arena mFrameArena;             // per-frame scratch, rewound in begin()
	};
}
