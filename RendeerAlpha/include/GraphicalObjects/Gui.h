#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalSrc/FontAtlas.h>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/GuiTheme.h>
#include <GraphicalObjects/Motion.h>
#include <GraphicalObjects/Syntax.h>
#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/Docking.h>
#include <vendor/RDA_Library/frame_arena.h>
#include <string>
#include <utility>
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
	// Writes a draw list's shape to the log: the counts, and for every command its index
	// range, clip rect and texture. Geometry that does not appear where it should is
	// almost always a batch whose range or clip is not what its author believed, and
	// reading it is faster than reasoning about it.
	void debugLogDrawData(const char* label, const GuiDrawData& data);

	// The vertices a single command actually covers, for when the counts look right but
	// the shape on screen does not. Logs at most `limit` of them.
	void debugLogCommandVertices(const char* label, const GuiDrawData& data,
	                             size_t commandIndex, size_t limit = 8);

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

		// Ask to be painted again once the rest of the tree is done, with the clip back at
		// the whole viewport.
		//
		// An open dropdown is the reason this exists: it belongs to a widget somewhere
		// down a panel, and it has to draw over everything and outside the panel that
		// clips its owner. Collected during the walk and drawn after it, so a popup wins
		// both the pixels and -- because hover is last-writer-wins -- the pointer.
		//
		// Only valid from inside a paint. One entry per widget per frame.
		void drawAbove(Widget* widget, glm::vec2 origin);

		// Structural edits, applied at the top of the next frame rather than immediately.
		// This is what a widget callback should use: a button that adds or removes a
		// sibling fires while its parent's children are being walked, and editing there
		// pulls the container out from under the walk. Building the tree during setup can
		// still use add()/remove() directly.
		WidgetTree& tree() { return mTree; }

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

		// Values on their way somewhere. A widget asks for what it wants to be and gets
		// what it is this frame; see Motion.h for why that is all there is to it.
		//
		// Keys are scoped ids, so two instances of the same content animate separately.
		// `motionKey` is how a widget makes one per property it animates.
		Motion& motion() { return mMotion; }
		// Whether anything is still on its way. The frame loop asks: an on-demand window
		// earns frames while something is moving, and stops the moment it has arrived.
		bool animating() const { return mMotion.moving(); }
		uint32_t motionKey(const char* id, uint32_t property) const {
			return scopedId(id) ^ (property * 2654435761u); // Knuth's multiplicative hash
		}

		// What a scroll bar looks like this frame: whether it is there at all, how long
		// its handle is, and what colour that handle is on its way to.
		//
		// One description because there are four scroll bars in this engine -- a scroll
		// view's, a two-axis scroll's, a list's and a text field's -- and they had four
		// copies of the same two lines between them. `key` needs three consecutive slots.
		//
		// Only what is *drawn* is animated. Whether a bar is needed, whether its handle
		// is under the pointer, and the handle's true length are all worked out from
		// geometry nothing has eased, and they stay the values input is tested against --
		// the same division `offset` and `drawnOffset` already make.
		//
		// Ask for it on every frame there could be a bar, including the frames where
		// there is nothing to scroll. A value nobody asks for is dropped, and a bar that
		// had been dropped would appear at full strength rather than fading in.
		struct ScrollBarLook {
			float    presence = 1.0f; // 0 with nothing to scroll, 1 with something
			float    thumb = 0.0f;    // the length to draw the handle, on its way to the real one
			uint32_t colour = 0;
		};
		ScrollBarLook scrollBar(uint32_t key, bool needed, bool hot,
		                        float thumbSpan, float trackSpan, const TextFieldStyle& style);

		// Wheel arbitration. Widgets are painted innermost-last, so the first one under
		// the pointer that can actually move claims the wheel for the frame: a text field
		// with its own overflow scrolls its text, and an enclosing Scroll moves only
		// once that field has nothing left to give. Cleared each frame in begin().
		bool scrollConsumed() const { return mScrollConsumed; }
		void consumeScroll() { mScrollConsumed = true; }

		// Scratch memory for things that live exactly one frame. Rewound in begin(), so
		// anything taken from it is valid until the next frame starts and must not be
		// stored past it. Widget and layout code can take temporary arrays from here
		// instead of allocating: after a few frames the arena has grown to the busiest
		// frame's needs and stops touching the heap entirely.
		RDL::frame_arena& frameArena() { return mFrameArena; }

		// ---- low-level drawing (for custom widgets and the dock chrome) ----
		void drawRect(const Rect& rect, uint32_t color);
		// Rounded fill. A radius that rounds away to nothing draws a plain rect, so a
		// caller can pass whatever the theme says without checking it first.
		void drawRectRounded(const Rect& rect, uint32_t color, float radius);
		void drawText(const char* text, glm::vec2 topLeft, uint32_t color,
		              TextStyle font = {});
		void image(const Rect& rect, const Texture* texture); // full-texture quad
		// A straight line of a given thickness, at any angle. The one piece of geometry
		// a 2D drawing needs that a widget never did -- an interface is made of boxes,
		// and a plot is not.
		void drawLine(glm::vec2 from, glm::vec2 to, uint32_t color, float width);
		// Everything drawn until the matching pop is this much more transparent.
		//
		// Nests by multiplying, so a half-faded panel inside a half-faded page is a
		// quarter there -- the same as every other opacity anyone has used.
		//
		// It works by scaling the alpha of the vertex colours, which is the one thing
		// every piece of GUI geometry goes through: a solid fill, a glyph and a picture
		// are all a quad with a colour on it, so one multiply covers a whole widget tree
		// without any widget knowing it is being faded.
		void pushOpacity(float alpha);
		void popOpacity();
		float opacity() const { return mOpacity; }

		// Makes a subtree take no input: hover, presses, the wheel and typing all stop at
		// it. It still draws, and it still animates.
		//
		// Done by taking the input away rather than by giving every widget a flag to
		// consult: a pointer that is nowhere fails every `contains` there is, and no
		// button, no press and nothing typed is a widget with nothing to react to. So a
		// widget written the ordinary way is already inert inside one of these, including
		// widgets written after this existed and knowing nothing about it.
		//
		// The clock survives, because something that takes no input still moves -- a
		// screen fading out has to finish fading.
		void pushInert();
		void popInert();
		bool inert() const { return !mInputStack.empty(); }

		void pushClipRect(const Rect& rect);
		void popClipRect();
		Rect currentClipRect() const; // the active clip, e.g. a container body — for fill widgets
		// Text metrics, in the size the given style asks for. Everything that says
		// nothing gets the interface's base size, which is what these used to be.
		float measureText(const char* text, TextStyle font = {}) const;
		float lineHeight(TextStyle font = {}) const;
		// Where the baseline sits below the top of a line of this size.
		float baseline(TextStyle font = {}) const;
		// Breaks `text` to `width`, at spaces where it can and mid-word where it must.
		// Returns one entry per line: an offset into `text` and a length.
		std::vector<std::pair<size_t, size_t>> wrapText(const std::string& text, float width,
		                                                TextStyle font = {}) const;

		// Push/pop an id scope: widget ids created between them are namespaced by `id`, so
		// two instances of the same content (e.g. two spawned containers) don't share
		// hot/active/focus/edit state. Panels and dock containers do this automatically.
		void pushId(const char* id);
		void popId();

		// The offscreen scene texture, set by the engine each frame in Widget viewport
		// mode (null in Fullscreen mode). A Viewport widget draws it.
		// What the window is cleared to this frame, already eased.
		//
		// Asked for in begin() rather than by whoever clears, because that happens on the
		// render path and a value only moves while something keeps asking for it. One
		// window, so one key.
		uint32_t backgroundColor() const { return mBackgroundColor; }

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
		void label(const char* text, glm::vec2 pos, uint32_t color = rgba(230, 230, 235),
		           TextStyle font = {});
		// `align` is where the label sits across the button. Centred by default, which is
		// what a button looks like; Start is what a row of them used as a menu wants,
		// where centred text makes every entry begin somewhere different.
		bool button(const char* id, const char* text, const Rect& rect,
		            Variant variant = kDefaultVariant, TextAlign align = TextAlign::Center,
		            float alignNudge = 0.0f);
		// Toggles `value` on click; returns true the frame it changed.
		bool checkbox(const char* id, const char* label, bool& value, const Rect& rect, Variant variant = kDefaultVariant);
		// Drags `value` within [minValue, maxValue]; returns true while it changes.
		bool sliderFloat(const char* id, float& value, float minValue, float maxValue, const Rect& rect, Variant variant = kDefaultVariant);
		// A drag bar. Adjusts `size` by how far the pointer moves along the bar's normal,
		// clamped to [minSize, maxSize]; returns true on the frames it changes.
		bool splitter(const char* id, float& size, float minSize, float maxSize,
		              const Rect& rect, bool vertical);
		// An editable text field; edits `text` in place, returns true the frames it
		// changes. The style (Line / Document / Code) governs layout and behavior.
		// `outFocused`, when given, reports whether this field holds keyboard focus —
		// enough for a caller to tell which of several editors a shortcut belongs to.
		// `version` lets a caller that knows when its text last changed say so, and the
		// line index and syntax spans are then kept between frames instead of rebuilt
		// from the whole string on every one of them. Zero means "cannot say", which
		// rebuilds everything each frame -- correct, and what an immediate-mode caller
		// holding a string it does not own has to pass.
		bool textField(const char* id, std::string& text, const Rect& rect, const TextFieldStyle& style,
		               bool* outFocused = nullptr, uint64_t version = 0);

		// Whether anything currently holds keyboard focus.
		bool hasKeyboardFocus() const { return mFocused != 0; }

		// ---- keyboard focus ----------------------------------------------------------
		//
		// A widget that does something when pressed says so as it draws, and gets back
		// whether it is the one the keyboard is pointed at. The order they call this in
		// is the order Tab walks, which is the order they were drawn, which is the order
		// they appear -- so nothing has to declare a tab index and nothing can disagree
		// with what the reader sees.
		//
		// `rect` is remembered so the ring can be drawn after everything else, on top.
		bool focusable(uint32_t wid, const Rect& rect, Variant ringVariant = kDefaultVariant);

		// Whether the focused widget was asked to act this frame: Enter or Space. The
		// widget decides what acting means -- a button clicks, a checkbox toggles.
		bool focusActivated(uint32_t wid) const;

		// An arrow key, for a widget that has somewhere to move: -1 or +1, 0 for neither.
		// Left and Up are -1; Right and Down are +1.
		int focusStep(uint32_t wid) const;

		// Escape, for something that can be dismissed.
		bool focusEscaped(uint32_t wid) const;

		// The id a widget is known by inside the current panel scope. A retained widget
		// needs it to ask the three questions above; an immediate one already has it.
		uint32_t widgetId(const char* id) const { return scopedId(id); }

		// Said by a widget that used Tab for something of its own -- a code field
		// indenting -- so the focus walk leaves it alone this frame.
		void consumeFocusMove() { mFocusMoveConsumed = true; }

		// Puts the keyboard somewhere, or nowhere with 0.
		void setFocus(uint32_t wid) { mFocused = wid; mFocusClaimed = true; }
		uint32_t focused() const { return mFocused; }

		const GuiDrawData& drawData() const { return mDraw; }

		// Tells the GUI that something it cannot see has changed, so the retained tree is
		// walked again next frame instead of reusing last frame's geometry.
		//
		// The cache notices input, resizes and structural edits by itself. What it cannot
		// notice is an application writing to a widget's public fields — `label->text =
		// "..."` from game logic is invisible from here. Call this after doing that.
		void markDirty() { mLayoutDirty = true; }

		// How the retained cache is doing: frames whose widget tree was walked versus
		// frames that reused the previous walk's geometry.
		struct CacheStats { uint64_t walked = 0; uint64_t reused = 0; };
		const CacheStats& cacheStats() const { return mCacheStats; }

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
			// 1 = a plain click drags the caret; 2 or 3 = this press selected a word or a
			// line, and dragging must leave that selection alone.
			int   dragGranularity = 1;

			// Cached syntax highlighting. Re-lexed only when the text or the language
			// changes, so an idle editor costs nothing per frame.
			std::vector<Token> tokens;
			size_t             tokenHash = 0;
			const Language*    tokenLang = nullptr;

			// Where each line begins. Kept across frames and rebuilt only when the text
			// actually changed, which the caller states by handing over a version. The
			// size is a second opinion, so a caller that mutates its string without
			// bumping its version is caught rather than drawing from a stale index.
			std::vector<int> lineStarts;
			uint64_t         indexedVersion = 0;
			size_t           indexedSize = 0;
			// Which version the cached spans were lexed from, for the same reason.
			uint64_t         tokenVersion = 0;
		};
		uint32_t hashId(const char* str) const;   // FNV-1a
		uint32_t scopedId(const char* id) const;   // combine with the panel scope

		void addQuad(float x0, float y0, float x1, float y1,
		             float u0, float v0, float u1, float v1, uint32_t color);
		// The same quad with its corners given rather than derived, for geometry that is
		// not axis-aligned. Wound the same way, so it clips and fades like everything else.
		void addQuadPoints(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
		                   uint32_t color);
		void addRect(const Rect& r, uint32_t color);
		// Rounded fill; falls back to addRect when the radius rounds away to nothing.
		void addRectRounded(const Rect& r, uint32_t color, float radius);
		// A filled rect with an optional border, drawn as a border-colored shape with the
		// fill inset on top — so one path covers square and rounded frames alike.
		void addFrame(const Rect& r, uint32_t fill, uint32_t border, float borderWidth, float radius);
		void addText(float penX, float baselineY, const char* text, uint32_t color,
		             TextStyle font = {});
		// Draws `count` characters starting at `text` and returns the pen X after the
		// run, so colored runs (syntax highlighting) chain without copying substrings.
		float addTextRange(float penX, float baselineY, const char* text, int count,
		                   uint32_t color, TextStyle font = {});
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
		uint64_t               mLastViewportRevision = 0; // drawings as of the last walk
		uint64_t               mLastThemeRevision = 0;    // the look as of the last walk
		uint32_t               mBackgroundColor = 0;      // eased in begin()
		uint32_t               mCmdStart = 0; // first index of the in-progress command

		uint64_t mDrawVersion = 0;   // fingerprint of this frame's draw data
		bool     mDrawChanged = true; // whether it differs from the previous frame

		// ---- retained-tree cache ----
		// The draw list is built retained-first, then the application's immediate calls
		// are appended. When nothing the tree depends on has changed, the retained part
		// of last frame's list is still correct: the vectors are truncated back to these
		// counts and the walk is skipped entirely. Nothing is copied — the geometry is
		// already sitting in the buffers.
		bool     mLayoutDirty = true;
		bool     mCacheValid = false;
		size_t   mRetainedVertices = 0;
		size_t   mRetainedIndices = 0;
		size_t   mRetainedCommands = 0;
		glm::vec2 mLastPointer{ 0.0f };
		glm::vec2 mLastViewport{ 0.0f };
		const Texture* mLastSceneTexture = nullptr;
		uint64_t   mLastDockRevision = 0;
		CacheStats mCacheStats;

		// Whether last frame's retained geometry can stand in for this frame's.
		bool canReuseRetained() const;

		uint32_t mFocused = 0;       // id of the keyboard-focused widget, 0 = none
		bool     mFocusClaimed = false; // a focused widget kept/took focus this frame
		// Everything that offered to take the keyboard this frame, in draw order, and
		// where each of them was. Cleared every frame: what is focusable is whatever is
		// on screen, so a widget that scrolled out of view leaves the order with it.
		struct Focusable { uint32_t id; Rect rect; Variant ring; };
		std::vector<Focusable> mFocusables;
		// Set when Tab is seen and cleared when it is spent. Resolved at the end of the
		// frame rather than where it is noticed, because the widget after this one has
		// not drawn yet and the order is the whole answer.
		int      mFocusMove = 0;
		bool     mFocusMoveConsumed = false;
		std::unordered_map<uint32_t, TextState> mTextStates;
		std::function<std::string()>     mGetClipboard;
		std::function<void(const char*)> mSetClipboard;

		WidgetTree     mTree;                     // structural edits pending for next frame
		Motion         mMotion;                   // values still on their way
		float              mOpacity = 1.0f;       // what the alpha of new geometry is scaled by
		std::vector<float> mOpacityStack;
		// The real input, held while a subtree is being shown something else -- nothing
		// at all, or a pointer somewhere else. See pushInert() and pushPointerOffset().
		std::vector<GuiInput> mInputStack;
		// Widgets that asked to be painted last, and where they were when they asked.
		std::vector<std::pair<Widget*, glm::vec2>> mOverlays;
		Container      mRetainedRoot{ "__root" }; // invisible root of the persistent tree
		bool           mScrollConsumed = false;   // wheel already claimed this frame
		DockSpace      mDockSpace;                // dockable containers, above the tree
		Theme          mTheme;                    // named style variants for this window's GUI
		SyntaxRegistry mSyntax;                   // languages for text-field highlighting
		RDL::frame_arena mFrameArena;             // per-frame scratch, rewound in begin()
	};
}
