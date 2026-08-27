#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/GuiTheme.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace RDA {
	class Gui;

	// Which of the parent's edges a widget is tied to. Anchored to one edge it keeps a
	// fixed distance from it; anchored to both edges of an axis it stretches to follow
	// them. This is the whole layout model — enough to build a resizable panel, and
	// small enough to reason about without a layout pass.
	enum class Anchor : uint8_t {
		None   = 0,
		Left   = 1 << 0,
		Right  = 1 << 1,
		Top    = 1 << 2,
		Bottom = 1 << 3,

		TopLeft     = Left | Top,          // the default: fixed position and size
		StretchX    = Left | Right | Top,  // width follows the parent, pinned to the top
		StretchY    = Top | Bottom | Left, // height follows the parent, pinned to the left
		BottomLeft  = Left | Bottom,       // moves down as the parent grows
		BottomRight = Right | Bottom,
		Fill        = Left | Right | Top | Bottom,
	};

	constexpr Anchor operator|(Anchor a, Anchor b) {
		return static_cast<Anchor>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}
	constexpr bool hasAnchor(Anchor value, Anchor flag) {
		return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
	}

	// No cap. A Content-sized widget with this maximum grows to whatever its content
	// needs, which is the sane default for something like a log that should simply be as
	// tall as what it holds.
	inline constexpr float kUnbounded = 1.0e9f;

	// How a widget wants to be sized on one axis, used by layout containers. Absolute
	// (Free) layout ignores these and uses `rect` — the two are alternatives, not a
	// migration: placing things yourself stays a first-class option.
	struct SizeSpec {
		enum class Mode : uint8_t {
			Fixed,   // exactly `value`
			Fill,    // share of the leftover space, weighted by `value`
			Content, // as big as the widget says it needs, clamped below
		};

		Mode  mode = Mode::Fixed;
		float value = 0.0f; // Fixed: the size. Fill: the weight.
		float min = 0.0f;
		float max = kUnbounded;
		// An upper bound computed from the space the parent has to give, for limits that
		// are only knowable at layout time ("no more than half the panel"). Wins over
		// `max` when set.
		std::function<float(float available)> maxOf;

		static SizeSpec fixed(float size) { SizeSpec s; s.mode = Mode::Fixed; s.value = size; return s; }
		static SizeSpec fill(float weight = 1.0f) { SizeSpec s; s.mode = Mode::Fill; s.value = weight; return s; }
		static SizeSpec content(float minSize = 0.0f, float maxSize = kUnbounded) {
			SizeSpec s; s.mode = Mode::Content; s.min = minSize; s.max = maxSize; return s;
		}

		// Applies both bounds. `available` only matters when maxOf is set.
		float clamp(float size, float available) const {
			const float upper = maxOf ? maxOf(available) : max;
			if (size > upper) size = upper;
			if (size < min) size = min;
			return size < 0.0f ? 0.0f : size;
		}
	};

	// A node in the retained widget tree. Unlike an immediate call, a Widget persists:
	// you build it once (typically in onStart), keep it, and mutate it at runtime. Each
	// frame the tree is walked and every node emits the equivalent immediate-mode call
	// into the Gui — so the retained layer reuses the immediate core's drawing, clipping
	// and hot/active interaction wholesale, and the two coexist in one draw list.
	//
	// `rect` is relative to the parent's content origin; the walk accumulates it.
	class Widget {
	public:
		explicit Widget(std::string id) : mId(std::move(id)) {}
		virtual ~Widget() = default;

		Widget(const Widget&) = delete;
		Widget& operator=(const Widget&) = delete;

		// Position and size relative to the parent's content area. When an edge is
		// anchored, the matching margin below takes over from rect's size on that axis.
		Rect rect{};
		bool visible = true;

		// Which parent edges this widget follows. The default keeps the old behaviour:
		// a fixed rect at a fixed offset from the top-left.
		Anchor anchor = Anchor::TopLeft;
		// Distance kept from the parent's right and bottom edges, used whenever those
		// edges are anchored. Measured in the same space as `rect`.
		float marginRight = 0.0f;
		float marginBottom = 0.0f;

		// Size preferences, honoured by layout containers (Stack) and ignored by absolute
		// ones (Free), which use `rect` instead.
		SizeSpec width;
		SizeSpec height;

		// What this widget needs for itself, given the space on offer. The default is its
		// `rect` size; widgets whose size follows their contents — a text field, a label —
		// override it. Only consulted for SizeSpec::Mode::Content.
		virtual glm::vec2 measureContent(Gui& gui, glm::vec2 available) const {
			(void)gui; (void)available;
			return { rect.w, rect.h };
		}

		// The rect this widget actually occupies, given where the parent put it and how
		// big the parent's content area is. `origin` is the parent's top-left in screen
		// space; `parent` is that same area including its size.
		Rect resolveRect(glm::vec2 origin, const Rect& parent) const;

		// Where a layout container decided this widget goes. Set by the parent just
		// before painting it, and consumed once — so a widget under a Stack uses the
		// arranged rect while everything else falls back to `rect` plus anchors.
		void setArranged(const Rect& r) { mArranged = r; mArrangedValid = true; }
		// Where the parent last put this widget, in screen space. Only meaningful after a
		// paint has run, and only for widgets a layout container arranges.
		const Rect& arrangedRect() const { return mArranged; }

		const std::string& id() const { return mId; }

		// ---- tree building / runtime mutation ----
		// Construct a child in place and return a stable pointer to it (ownership stays
		// in the tree). This is also the "modify the tree at runtime" entry point.
		template<typename T, typename... Args>
		T* add(Args&&... args) {
			auto node = std::make_unique<T>(std::forward<Args>(args)...);
			T* raw = node.get();
			// Routed through addChild rather than touching mChildren directly, so this
			// path also records the parent link and trips the walk guard. Bypassing it
			// left add() invisible to both.
			addChild(std::move(node));
			return raw;
		}
		Widget* addChild(std::unique_ptr<Widget> child);
		// Inserts at a position among the existing children; `index` past the end appends.
		Widget* insertChild(std::unique_ptr<Widget> child, size_t index);
		// Detaches a direct child and hands back ownership, instead of destroying it.
		std::unique_ptr<Widget> detachChild(Widget* child);
		// The widget this one hangs off, or null for a root.
		Widget* parent() const { return mParent; }
		Widget* find(const std::string& id);  // depth-first, by id
		bool    remove(const std::string& id); // removes the first match anywhere below
		// Moves a direct child to a new position among its siblings, which for a layout
		// container is what reordering means. No-op if it is not a direct child.
		bool    moveChild(Widget* child, size_t index);
		void    clearChildren() { mChildren.clear(); }
		const std::vector<std::unique_ptr<Widget>>& children() const { return mChildren; }

		// Emits this node (and its subtree) into `gui`, offset by the accumulated origin.
		virtual void paint(Gui& gui, glm::vec2 origin) = 0;

	protected:
		void paintChildren(Gui& gui, glm::vec2 origin);

		// The rect to draw into: whatever a layout parent arranged, otherwise the classic
		// `rect` + anchors against the current clip. Clears the arranged flag, so a widget
		// that stops being laid out falls back cleanly on the next frame.
		Rect placement(Gui& gui, glm::vec2 origin);

		std::string mId;
		std::vector<std::unique_ptr<Widget>> mChildren;
		Widget* mParent = nullptr;
		Rect mArranged{};
		bool mArrangedValid = false;
	};

	// Marks the span in which a widget tree is being walked and painted.
	//
	// Structural edits are unsafe inside it: a callback fires while its parent's child
	// list is being iterated, so adding or removing there pulls the container out from
	// under the walk. The guard exists so those edits can be *detected* rather than
	// merely documented — Widget's mutators warn when one happens while it is active,
	// which turns a memory-corruption bug into a log line pointing at the cause.
	class WidgetWalkGuard {
	public:
		WidgetWalkGuard();
		~WidgetWalkGuard();
		WidgetWalkGuard(const WidgetWalkGuard&) = delete;
		WidgetWalkGuard& operator=(const WidgetWalkGuard&) = delete;
		static bool active();
	};

	// Structural edits queued now and applied at a safe point.
	//
	// The widget is *constructed* immediately, so the caller gets a usable pointer to
	// configure straight away; only its attachment to the tree is deferred. That keeps
	// the natural `auto* b = tree.create<Button>(...); b->text = "Run";` shape while
	// making the edit itself safe from inside a callback.
	class WidgetTree {
	public:
		// `index` past the end (or negative, via the default) appends.
		template<typename T, typename... Args>
		T* create(Widget& parent, int index, Args&&... args) {
			auto node = std::make_unique<T>(std::forward<Args>(args)...);
			T* raw = node.get();
			mCommands.push_back({ Op::Insert, &parent, raw, std::move(node), index });
			return raw;
		}
		template<typename T, typename... Args>
		T* append(Widget& parent, Args&&... args) {
			return create<T>(parent, -1, std::forward<Args>(args)...);
		}

		void remove(Widget* widget);
		void move(Widget* child, int index);

		// Applies everything queued, in the order it was requested. Called by the GUI at
		// the top of a frame, before anything walks the tree.
		void flush();
		bool empty() const { return mCommands.empty(); }

	private:
		enum class Op { Insert, Remove, Move };
		struct Command {
			Op                      op;
			Widget*                 parent = nullptr;
			Widget*                 target = nullptr;
			std::unique_ptr<Widget> node;   // Insert only: owned until it is spliced in
			int                     index = -1;
		};
		std::vector<Command> mCommands;
	};

	// Lays its children out in a line, top to bottom or left to right.
	//
	// Children sized Fixed or Content take what they ask for; the space left over is
	// split between the Fill ones by weight. On the cross axis everything stretches to
	// the container's width (or height), which is what a stacked panel almost always
	// wants.
	//
	// This is what makes "insert a cell between two others" a one-line operation: nothing
	// carries a hand-computed position, so inserting shifts the rest automatically.
	class Stack : public Widget {
	public:
		explicit Stack(std::string id) : Widget(std::move(id)) {}
		bool  vertical = true;
		float spacing = 4.0f;
		float padding = 0.0f;

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A clipped window onto content taller than itself.
	//
	// Children are stacked in a column at the height each asks for, shifted up by
	// `offset`, and clipped to the view. A draggable bar appears on the right whenever
	// there is more content than fits.
	//
	// Children are measured against the *visible* height, not the full content height,
	// so a child that sizes itself relative to what is available ("no more than a third
	// of the panel") means the window rather than the scrolling extent. The bar's width
	// is reserved whether or not it is showing: letting it appear and disappear would
	// change the width available to the content, which can change the content's height,
	// which can decide the bar again — a layout that never settles.
	//
	// Anything scrolled entirely out of view is skipped rather than painted, so a long
	// notebook costs roughly what is on screen instead of what exists.
	class ScrollView : public Widget {
	public:
		explicit ScrollView(std::string id) : Widget(std::move(id)) {}

		float offset = 0.0f;     // how much content is hidden above the top edge
		float spacing = 0.0f;    // between stacked children
		float padding = 0.0f;
		float barWidth = 10.0f;
		float wheelStep = 52.0f; // pixels per wheel notch
		// Styling for the bar comes from a text field variant, which is where the scroll
		// colours already live, so a scroll view matches the fields it usually contains.
		Variant variant = kDefaultVariant;

		// Valid after the first paint.
		float contentHeight() const { return mContentHeight; }
		float viewHeight() const { return mViewHeight; }
		float maxOffset() const { return mContentHeight > mViewHeight ? mContentHeight - mViewHeight : 0.0f; }
		bool  atBottom() const { return offset >= maxOffset() - 0.5f; }
		void  scrollToBottom() { offset = maxOffset(); }
		// Brings a descendant's arranged rect into view. Call after a paint, since it
		// works from the geometry that paint computed.
		void  scrollToShow(const Rect& target);

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		float mContentHeight = 0.0f;
		float mViewHeight = 0.0f;
		float mViewTop = 0.0f;   // where the content origin sat last frame, unscrolled
		bool  mDraggingThumb = false;
		float mThumbGrab = 0.0f;
	};

	// An invisible grouping node: paints its children, draws nothing itself. The Gui's
	// retained root is one of these.
	class Container : public Widget {
	public:
		explicit Container(std::string id) : Widget(std::move(id)) {}
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A framed panel with a background; clips and positions its children.
	class Panel : public Widget {
	public:
		explicit Panel(std::string id) : Widget(std::move(id)) {}
		Variant variant = kDefaultVariant; // theme variant (see Gui::theme())
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A line of text.
	class Label : public Widget {
	public:
		Label(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string text;
		uint32_t    color = rgba(230, 230, 235); // used when variant == "default"
		Variant     variant = kDefaultVariant;   // a named variant's color wins over `color`
		// As wide as its text and one line tall, so height="content" in a layout means
		// what it looks like it means.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A clickable button. onClick fires once on each completed click.
	class Button : public Widget {
	public:
		Button(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string           text;
		Variant               variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void()> onClick;
		// Its text plus room to breathe around it.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A labelled boolean toggle. onChange fires with the new value when toggled.
	class Checkbox : public Widget {
	public:
		Checkbox(std::string id, std::string label) : Widget(std::move(id)), label(std::move(label)) {}
		std::string               label;
		bool                      value = false;
		Variant                   variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void(bool)> onChange;
		// The box is square and as tall as a line; the label follows it.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// A draggable float in [minValue, maxValue]. onChange fires while dragging.
	class Slider : public Widget {
	public:
		explicit Slider(std::string id) : Widget(std::move(id)) {}
		float                      value = 0.0f;
		float                      minValue = 0.0f;
		float                      maxValue = 1.0f;
		Variant                    variant = kDefaultVariant; // theme variant (see Gui::theme())
		std::function<void(float)> onChange;
		// A slider has no content to measure, so "content" means a track of ordinary
		// thickness rather than nothing at all.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

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
		std::string                            text;
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
	};

	// A draggable bar that resizes whatever is above it (or left of it).
	//
	// It owns the number rather than pointing at someone else's: `value` is the size the
	// splitter controls, the drag adjusts it, and the layout above reads it back. That
	// keeps the widget tree free of raw pointers into application state, and means a
	// splitter can be serialised with the rest of a layout.
	class Splitter : public Widget {
	public:
		explicit Splitter(std::string id) : Widget(std::move(id)) {}
		float value = 120.0f;   // the size being controlled
		float minValue = 40.0f;
		float maxValue = 4000.0f;
		bool  vertical = false; // false: a horizontal bar dragged up/down
		std::function<void(float)> onChange;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// Displays the engine's offscreen scene texture (ViewportMode::Widget) filling its
	// rect. In Fullscreen mode there is no scene texture, so it draws a placeholder.
	class Viewport : public Widget {
	public:
		explicit Viewport(std::string id) : Widget(std::move(id)) {}
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}

