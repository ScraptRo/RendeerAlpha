#pragma once
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/GuiTheme.h>
#include <GraphicalObjects/Texture.h>
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

	// Where a child sits across the stacking axis -- the axis it is *not* stacked along.
	//
	// Stretch is the default because it is what a stacked panel almost always wants, and
	// because it is what this did before there was a choice. A child that asked for its own
	// size across the axis keeps it whatever this says; alignment decides where the leftover
	// room goes, not whether the child gets to choose its size.
	enum class Align : uint8_t {
		Stretch,
		Start,
		Center,
		End,

		// Only meaningful on a child, as hAlignSelf/vAlignSelf: "whatever the container
		// says". A container's own setting never holds this, which is why it is last
		// rather than first -- the default for a container is Stretch, and it stays that.
		Auto,
	};

	// A word, and however many pixels past where the word puts it.
	//
	// The same shape a size has: "center" is where it sits, "center+20" is twenty to the
	// right of that. One rule -- a word may carry an offset -- said once for sizes and
	// once here, rather than a second mechanism for nudging things.
	//
	// It is the only way to move a child inside a stack at all: x and y are for absolute
	// placement, and a stack decides positions itself.
	struct Alignment {
		Align word   = Align::Stretch;
		float offset = 0.0f;
	};

	// Which alignment a child is actually laid out under: its own when it named one, and
	// the container's otherwise. Auto is the child saying nothing, which is the default.
	// The nudge travels with the word, so a child that answers back brings its own.
	inline Alignment resolveAlign(Alignment container, Alignment child) {
		return child.word == Align::Auto ? container : child;
	}

	// The whole of the axis rule, in one place: which of an absolute pair lies across the
	// stacking axis, and which lies along it.
	//
	// Free functions rather than only methods because this is the rule the rename exists
	// for -- `hAlign` has to mean the same thing whichever way a stack runs, including
	// when `arrange` is a binding that flips it while the application is open -- and a
	// rule worth stating is worth being able to test without a device, a font and a
	// window to hang a real Stack from.
	inline Alignment crossAlignOf(bool vertical, Alignment hAlign, Alignment vAlign) {
		return vertical ? hAlign : vAlign;
	}
	inline Alignment mainAlignOf(bool vertical, Alignment hAlign, Alignment vAlign) {
		return vertical ? vAlign : hAlign;
	}

	// Where a child of `size` sits inside `available`, across the stacking axis.
	//
	// Never negative: a child bigger than the room it was given starts at the near edge
	// and runs past the far one, so what overflows is the end rather than the beginning.
	// Stretch and Auto have no offset to compute -- a stretched child is the size of its
	// container, and Auto was resolved before this was asked.
	inline float crossOffsetFor(Alignment align, float available, float size) {
		const float leftover = available - size;
		float offset = 0.0f;
		if (leftover > 0.0f) {
			switch (align.word) {
			case Align::Center: offset = leftover * 0.5f; break;
			case Align::End:    offset = leftover; break;
			default:            break;
			}
		}
		// The nudge applies even when there is no room left over: a child as wide as its
		// line, asked to sit 20 past the start, sits 20 past the start and overflows.
		return offset + align.offset;
	}

	// "center+20" or "start-8" -> the word, with the number read into `offset`. The word
	// alone leaves it at zero. Shared by the loader and by a bound value, because an
	// alignment means the same thing however it was written.
	inline std::string_view splitAlignOffset(std::string_view text, float& offset) {
		offset = 0.0f;
		const size_t sign = text.find_first_of("+-", 1);
		if (sign == std::string_view::npos) return text;
		const std::string tail(text.substr(sign));
		char* end = nullptr;
		const float parsed = std::strtof(tail.c_str(), &end);
		if (end == tail.c_str() || *end != '\0') return text; // "center+" -- not a number
		offset = parsed;
		return text.substr(0, sign);
	}

	// How room left over along the stacking axis is shared out between the children.
	//
	// Distribution only, since `justify` stopped meaning "where do they sit": hAlign and
	// vAlign answer that on whichever axis the reader named, and putting the same answer
	// in two places is how one of them ends up disagreeing with the other. Spreading
	// children apart is a genuinely different operation from moving them as a group, and
	// it is the only one that has no meaning across the axis -- so it is the only one
	// that stays tied to it.
	//
	// Only meaningful when there is room left over: a child sized Fill takes the
	// remainder, so a row containing one has nothing to share and this has no effect.
	enum class Distribute : uint8_t {
		None,         // wherever hAlign/vAlign puts them along the axis
		SpaceBetween, // first and last against the edges, gaps equal
		SpaceAround,  // equal space around each child, so the edges get half a gap
	};

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
		// Added to whatever the mode resolved to: "content-23" is the content's size
		// less 23 pixels, "fill-40" is this child's share of the leftover less 40.
		//
		// It has to live here rather than in the expression that asked for it, because
		// neither "content" nor "fill" is a value a binding could subtract from -- one
		// is a request to measure and the other a claim on room nobody has divided yet,
		// and both are answered during layout, long after every binding has run.
		float offset = 0.0f;
		// An upper bound computed from the space the parent has to give, for limits that
		// are only knowable at layout time ("no more than half the panel"). Wins over
		// `max` when set.
		std::function<float(float available)> maxOf;

		static SizeSpec fixed(float size) { SizeSpec s; s.mode = Mode::Fixed; s.value = size; return s; }
		static SizeSpec fill(float weight = 1.0f) { SizeSpec s; s.mode = Mode::Fill; s.value = weight; return s; }
		static SizeSpec content(float minSize = 0.0f, float maxSize = kUnbounded) {
			SizeSpec s; s.mode = Mode::Content; s.min = minSize; s.max = maxSize; return s;
		}

		// The words a layout may write for a size, and the only place they are read:
		// "content", "fill", "fill:2" for a double share, and either with a "+12" or
		// "-23" after it. Answers false for anything else, so a caller can leave the
		// size it already had alone rather than guessing.
		//
		// Shared because a size means the same thing whether it was written as a
		// constant or returned by a binding, and two parsers agreeing about that by
		// coincidence is one edit away from not.
		// Inline so the test runner reaches it: it links the device-independent sources
		// only, and this one is a string parser that happens to live beside a widget.
		static bool parse(std::string_view word, SizeSpec& out) {
			if (word.empty()) return false;

			// The offset first, so what is left is the plain word. Only a sign that
			// follows something counts: "-20" on its own is a negative fixed size.
			float offset = 0.0f;
			const size_t sign = word.find_first_of("+-", 1);
			if (sign != std::string_view::npos) {
				const std::string tail(word.substr(sign));
				char* end = nullptr;
				const float parsed = std::strtof(tail.c_str(), &end);
				if (end == tail.c_str() || *end != '\0') return false; // "content-" or worse
				offset = parsed;
				word = word.substr(0, sign);
			}

			SizeSpec parsed;
			if (word == "content") {
				parsed = SizeSpec::content();
			} else if (word.rfind("fill", 0) == 0) {
				float weight = 1.0f;
				if (word.size() > 5 && word[4] == ':') {
					const std::string number(word.substr(5));
					weight = std::strtof(number.c_str(), nullptr);
					if (weight <= 0.0f) weight = 1.0f;
				} else if (word.size() != 4) {
					return false; // "filling", or "fill" with something odd after it
				}
				parsed = SizeSpec::fill(weight);
			} else {
				return false;
			}
			parsed.offset = offset;
			// The bounds belong to the properties that set them, not to the word, so a
			// minWidth already read stays where it was.
			parsed.min = out.min;
			parsed.max = out.max;
			parsed.maxOf = out.maxOf;
			out = parsed;
			return true;
		}

		// Applies the offset and both bounds. `available` only matters when maxOf is set.
		//
		// Every path that resolves a size comes through here -- fixed, filled and
		// measured alike -- which is why the offset is applied in one place and not in
		// three.
		float clamp(float size, float available) const {
			size += offset;
			const float upper = maxOf ? maxOf(available) : max;
			if (size > upper) size = upper;
			if (size < min) size = min;
			return size < 0.0f ? 0.0f : size;
		}
	};


	// Where a child's size along the stacking axis comes from, decided before anything is
	// measured. The measuring is left to the caller because it costs -- a text field
	// counts its lines to answer -- and because the rule on its own is worth being able
	// to test without a device, a font and a window.
	enum class SizeSource : uint8_t {
		Declared, // spec.value, as written
		Rect,     // what the widget was last laid out at
		Content,  // measure it
		Fill,     // nothing yet: the leftover pass decides
	};

	// Saying nothing about a size is not asking for none of it. A stack that read it that
	// way gave every child of an unsized row zero width and drew them all at the same x.
	inline SizeSource mainAxisSource(const SizeSpec& spec, float own) {
		switch (spec.mode) {
		case SizeSpec::Mode::Fixed:
			if (spec.value > 0.0f) return SizeSource::Declared;
			if (own > 0.0f)        return SizeSource::Rect;
			return SizeSource::Content;
		case SizeSpec::Mode::Content:
			return SizeSource::Content;
		default:
			return SizeSource::Fill;
		}
	}

	// Across the axis it is the other way round: no opinion means the container stretches
	// the child to the line, so nothing is measured for it.
	inline SizeSource crossAxisSource(const SizeSpec& spec) {
		switch (spec.mode) {
		case SizeSpec::Mode::Fixed:   return SizeSource::Declared;
		case SizeSpec::Mode::Content: return SizeSource::Content;
		default:                      return SizeSource::Fill;
		}
	}

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

		// The shape this travels along when it moves, written as an SVG path, and how fast
		// it travels it. Two curves, and they are independent on purpose: the same arc at
		// a different pace, or the same pace along a different arc, are different motions
		// and neither should require re-authoring the other.
		//
		// The route is fitted to the journey -- its first point on where the move starts,
		// its last on where it ends, turned and scaled -- so it bends relative to the
		// direction of travel and one route serves any two places. Empty is a straight
		// line, which is what everything did before this existed.
		//
		// Needs `animate` to be more than zero: a route is a shape to travel, and
		// something arriving instantly does not travel.
		std::string route;
		Pace        pace;

		// Dragging this moves the whole window.
		//
		// For a window opened with no OS frame, where the layout draws its own title bar
		// and would otherwise have drawn something that cannot be picked up. A property
		// rather than a `<titlebar>` element, because the bar is whatever the designer
		// made it -- a stack, a panel, a label -- and only one of its jobs is being a
		// handle.
		//
		// A button inside it still takes its own press: this is declared before the
		// children draw, and the one under the pointer last is the one that gets it.
		bool dragWindow = false;

		// This child's own answer to the container's hAlign and vAlign. Whichever of the
		// two lies across the stacking axis is the one a container reads; the other is
		// ignored, exactly as the container's own setting for that axis is.
		//
		// Auto -- the default -- means the container decides. These are on the child
		// rather than the container because the one thing a container's single setting
		// cannot say is "all of them like this, except that one".
		Alignment hAlignSelf{ Align::Auto, 0.0f };
		Alignment vAlignSelf{ Align::Auto, 0.0f };

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

		// How long this widget takes to slide to a new place, in milliseconds. Zero is
		// what everything did before: it is simply where the layout put it.
		//
		// Inherited from the nearest ancestor that names one, because the things that
		// move are a stack's children and saying so on each of them would be a chore. Put
		// it on the container whose children rearrange.
		//
		// Only the *drawn* rect lags. What a widget measures as, and what its parent
		// measures it as, is the value the layout computed -- if the smoothed rect fed
		// back into measurement, a stack would size itself to a transient number and the
		// whole layout would wobble on its way to settling.
		float animateMs = 0.0f;

		// Seconds this widget's placement should ease over: its own, or the nearest
		// ancestor's. Zero when nobody asked.
		float motionSeconds() const;

		// How much of the layout this widget is currently taking: 1 while it is there, 0
		// once it has collapsed out, and the fraction in between while it is on its way.
		//
		// This is the one animated value measurement is allowed to read, and it is safe to
		// because of where it comes from: `visible` and a clock. Nothing measured feeds it,
		// so there is no loop -- a stack sized from it is sized from a number that would
		// have been the same whatever the stack turned out to be.
		//
		// Without a duration it is the boolean it has always been.
		float presence(Gui& gui) const;

		// Whether children see this widget's ancestors when they look for a duration.
		// False for anything that moves its children itself -- a list scrolls by putting
		// its pooled rows in new places every frame, and easing that is not a slide, it
		// is a smear.
		virtual bool inheritsMotion() const { return true; }
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
		// Virtual because a container may not keep its children in the usual place: a
		// dock host hands a panel to its dock space, which positions it instead of the
		// parent's layout. Everything that builds a tree goes through here, so one
		// override is enough to cover a front end as well as hand-written code.
		virtual Widget* addChild(std::unique_ptr<Widget> child);
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

		// The second half of painting, for a widget that asked Gui::drawAbove() for it.
		// Called after the whole tree has been walked, with the clip at the viewport, so
		// what it draws is over everything and outside whatever clips its owner.
		virtual void paintAbove(Gui& gui, glm::vec2 origin) { (void)gui; (void)origin; }

	protected:
		void paintChildren(Gui& gui, glm::vec2 origin);

		// The rect to draw into: whatever a layout parent arranged, otherwise the classic
		// `rect` + anchors against the current clip. Clears the arranged flag, so a widget
		// that stops being laid out falls back cleanly on the next frame.
		Rect placement(Gui& gui, glm::vec2 origin);

	public:
		// The pointer arrived, or left. Fired on the change rather than every frame, so a
		// handler is asked a question rather than told a fact sixty times a second.
		//
		// Reported from placement(), which is the one call every widget's paint makes to
		// find out where it ended up -- so this works for a row, a panel, a label, and
		// anything added later, without each of them knowing about it. A widget that is
		// not painted does not report, which is what makes a row scrolled out of view
		// stay quiet; and an inert subtree has the pointer moved far away, so it reports
		// a leave and nothing else.
		std::function<void(bool)> onHover;

	protected:

		std::string mId;
		std::vector<std::unique_ptr<Widget>> mChildren;
		Widget* mParent = nullptr;
		Rect mArranged{};
		bool mArrangedValid = false;
		bool mHovered = false;      // what onHover last reported
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

		// Absolute, not relative to `vertical`. A stack whose direction is a binding --
		// `arrange={() => state.rda.width < 700 ? "vertical" : "horizontal"}` -- flips
		// axes while it runs, and a cross-axis alignment would silently come to mean the
		// other one at that moment. "Centred horizontally" cannot.
		//
		// Whichever of the two lies across the stacking axis decides how wide or tall a
		// child that named no size across it becomes; the other moves the children along
		// the axis as a group, and Stretch there is read as Start because stretching
		// along the axis is what a child's own `fill` is for.
		Alignment  hAlign;
		Alignment  vAlign;
		Distribute spread = Distribute::None;

		// Across the stacking axis and along it, whichever way round this one runs.
		Alignment crossAlign() const { return crossAlignOf(vertical, hAlign, vAlign); }
		Alignment mainAlign()  const { return mainAlignOf(vertical, hAlign, vAlign); }

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
	class Scroll : public Widget {
	public:
		explicit Scroll(std::string id) : Widget(std::move(id)) {}

		float offset = 0.0f;     // how much content is hidden above the top edge
		float offsetX = 0.0f;    // and past the left one, when it scrolls sideways
		// Which way it may scroll. Down by default: a column taller than its view is the
		// ordinary case, and one wider than its view is usually a layout mistake rather
		// than something to let the reader drag around.
		bool  vertical = true;
		bool  horizontal = false;
		float spacing = 0.0f;    // between stacked children
		float padding = 0.0f;
		float barWidth = 10.0f;
		float wheelStep = 52.0f; // pixels per wheel notch
		// Where children sit across the column, the same question a Stack answers.
		// Stretch -- every child as wide as the view -- is what a scrolling page almost
		// always wants; the other three are what makes a narrow column of centred cards
		// possible. Only horizontal: this always stacks downward, so there is no second
		// axis to have an opinion about.
		Alignment hAlign;
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
		bool  mDraggingThumbX = false;
		float mThumbGrabX = 0.0f;
		float mContentWidth = 0.0f;
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

	// A panel that draws over everything, beside the widget it belongs to, and closes when
	// the pointer goes somewhere else.
	//
	// Every dropdown, menu, tooltip, autocomplete list and dialog wants the same three
	// things -- draw above, take the input first, close when it is not wanted -- and
	// without this every application rebuilds them out of a <container>, a transparent
	// full-area <button> and arithmetic against the window's width. That pattern works
	// and is slightly wrong in a different way each time.
	//
	// It is a Container, so what is inside it is an ordinary layout: a <stack> of buttons
	// is a menu, a <list> is a picker, a <label> is a tooltip. This widget is only the
	// surface, where it goes, and when it goes away.
	class Popup : public Container {
	public:
		// Which edge of the anchor to sit against. `Over` covers it, for a picker that
		// replaces the control rather than hanging off it.
		enum class Placement : uint8_t { Below, Above, Right, Left, Over };

		explicit Popup(std::string id) : Container(std::move(id)) {}

		// Showing or not. Bindable, and the application owns it: this never writes the
		// signal, it calls onClose and lets the layout decide -- the same shape every
		// other two-way property here has.
		bool open = false;

		// The full id path of the widget to hang off -- "root/toolbar/model", the path the
		// log, the probe and rda_focus all spell, not the bare id. Empty means the popup
		// is placed by its own x/y/w/h, like anything else in a container.
		//
		// The engine has to remember where that widget was drawn to do this, which nothing
		// else needs; see Gui::wantAnchor for what it costs.
		std::string anchor;
		Placement   placement = Placement::Below;
		float       gap = 4.0f;       // pixels between the anchor and this
		float       padding = 8.0f;   // inset around its children, and what it adds to its own size

		// While it is open, nothing underneath hovers or clicks: the interface behind a
		// menu is behind it. Off for a tooltip, or anything that floats without taking
		// over.
		bool blocking = true;

		// A press outside it, or Escape. The layout closes itself here -- `open` is a
		// binding, so writing false to the signal is what actually shuts it.
		std::function<void()> onClose;

		Variant variant = kDefaultVariant;  // a panel variant, for the surface

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		void paintAbove(Gui& gui, glm::vec2 origin) override;

	private:
		// Where it was drawn last, so the dismissal can ask whether the press was inside
		// it -- which happens in the same call that would otherwise have to guess.
		Rect mShown{};
	};

	// A line of text.
	class Label : public Widget {
	public:
		Label(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string text;
		uint32_t    color = rgba(230, 230, 235); // used when variant == "default"
		Variant     variant = kDefaultVariant;   // a named variant's color wins over `color`
		// Break the text to the width it is given rather than letting it run off the
		// edge. Off by default: a caption that silently became three lines tall would
		// move everything under it, and most labels are one line on purpose.
		bool        wrap = false;
		// Runs of the text drawn in their own colour, as `start:length:colour` triples
		// separated by `;` -- "0:3:#E06C75;4:6:#61AFEF". Offsets are bytes.
		//
		// A string rather than a shape, because that is what makes it bindable: it is a
		// column like any other, so a row can carry its own colouring and a list gets
		// syntax colour, a diff or a search highlight without a new mechanism. Anything
		// no span covers is drawn in the label's own colour.
		//
		// Parsed once per distinct value -- see runs().
		std::string spans;

		// `spans`, parsed, clamped to the text and put in order. Rebuilt only when the
		// string or the text it indexes actually changes, so a label re-bound to the same
		// value every frame parses nothing.
		const std::vector<TextSpan>& runs() const;
		// Where the text sits in the box the layout gave this label, across and down.
		// Named for the axes rather than for the container, which is the same pair a
		// stack answers and means the same thing here.
		//
		// Worth having both because they are not the same question: a label in a column
		// is usually as wide as the column and as tall as one line, so hAlign moves the
		// text within a box that is wider than it, and vAlign matters the moment a row
		// is taller than a line -- which is any row holding a button beside it.
		TextAlign   hAlign = TextAlign::Start;
		TextAlign   vAlign = TextAlign::Start;
		// Past where the word puts it, the same nudge a container's alignment takes.
		float       hAlignOffset = 0.0f;
		float       vAlignOffset = 0.0f;
		// As wide as its text and one line tall -- or, when wrapping, as tall as the
		// lines it breaks into -- so height="content" in a layout means what it looks
		// like it means.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		// What runs() built, and what it built them from. Two keys rather than one: the
		// spans are clamped to the text, so the same string over a shorter text is a
		// different answer -- which is exactly what a list row re-bound to a new row is.
		mutable std::vector<TextSpan> mRuns;
		mutable std::string           mRunsFrom;
		mutable size_t                mRunsTextSize = static_cast<size_t>(-1);
	};

	// A clickable button. onClick fires once on each completed click.
	//
	// It may hold children, and then it is a button around them rather than around a
	// string: an icon beside a label, two lines of text, a row of anything. They are
	// laid out inside its rect exactly as a panel's are, so a stack inside one arranges
	// them and the button itself stays a button rather than growing a second layout
	// engine of its own.
	//
	// The children are drawn, and the button takes the input. A widget inside one that
	// would otherwise be clickable is not: the press belongs to the button, which is
	// the same answer HTML gives and the only one that makes "the whole thing is one
	// control" true.
	class Button : public Widget {
	public:
		Button(std::string id, std::string text) : Widget(std::move(id)), text(std::move(text)) {}
		std::string           text;
		Variant               variant = kDefaultVariant; // theme variant (see Gui::theme())
		// Where the label sits across the button. Centred is what a button looks like; a
		// column of them used as a menu wants Start, so every entry begins in the same
		// place instead of each one beginning somewhere else.
		TextAlign             hAlign = TextAlign::Center;
		float                 hAlignOffset = 0.0f;
		// Inset around the children, when it has any. The room its own text would have
		// had, so a button around an icon is the size a button around a word is.
		float                 padding = 6.0f;
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


	// A picture. `source` is a path, loaded the first time it is painted and owned by
	// this widget -- so a hot reload reloads it, which costs a file read and keeps the
	// lifetime obvious. Two images of the same file are two textures; a UI has a handful,
	// and a cache that outlives the tree is a cache that has to be invalidated.
	class Image : public Widget {
	public:
		enum class Fit : uint8_t {
			Contain, // whole picture inside the box, aspect kept, centred
			Stretch, // fill the box, aspect ignored
		};
		explicit Image(std::string id, std::string source = {})
			: Widget(std::move(id)), source(std::move(source)) {}
		~Image() override;

		// A file beside the running program, or "mem:<name>" for a picture a backend
		// registered -- see Images.h. Bindable either way, so one <image> can show a file
		// now and a model's answer a moment later.
		//
		// A path ending in .svg is drawn rather than loaded: the shape is rasterised at
		// whatever size the layout turned out to give this widget, so it is sharp at that
		// size and at the next one, and on a display at 150% it is sharp at a size nobody
		// exported. See Core/Svg.h.
		std::string source;
		Fit         fit = Fit::Contain;

		// Multiplied into the picture. White leaves it alone, which is the default and is
		// what a photograph wants; a colour is what turns one white-drawn icon into every
		// colour a theme has, without a second file or a second rasterisation.
		//
		// This is why an SVG's `currentColor` -- and a fill it never stated -- come out
		// white rather than the black the specification says: white is the one colour a
		// tint can still turn into any other.
		uint32_t    tint = rgba(255, 255, 255);

		// The picture's own size in pixels, so width="content" means what it looks like.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		void ensureLoaded() const;
		// Hands the texture to the engine to destroy at a safe moment. See Image::release.
		void release() const;
		// The same, for the frames of a loop. Its own function because there are three
		// places a loop is dropped and every one of them has to go through the engine:
		// clearing the vector destroys the textures where they stand, which is a
		// use-after-free whenever the GUI is still describing one. See Image::release.
		void retireFrames() const;

		// Held by pointer so its address survives this widget. The frame's draw list was
		// built before the update that destroys this widget ran, and it still refers to
		// the texture by address when it is recorded afterwards.
		mutable std::unique_ptr<Texture> mTexture;
		// A registered picture is borrowed rather than owned: the registry holds it, two
		// <image>s naming it share one, and redefining the name replaces what both draw.
		// The revision is how a redefine is noticed -- the pointer may well be reused.
		mutable const Texture* mShared = nullptr;
		mutable uint64_t       mSharedRevision = 0;
		mutable std::string mLoaded;  // the source the above were resolved from
		mutable bool        mFailed = false;

		// A drawn picture, kept parsed so that changing size re-draws rather than
		// re-reads, and the size it was last drawn at.
		mutable std::shared_ptr<void> mDrawing;   // Svg::Picture, held without the header
		mutable uint32_t    mDrawnWidth = 0;
		mutable uint32_t    mDrawnHeight = 0;

		// A drawing that moves, rasterised once into the frames of its loop.
		//
		// Not re-drawn per frame: the rasteriser walks every edge of every shape for every
		// sub-scanline, which is nothing once and real work sixty times a second. A loop
		// is short and an icon is small -- thirty frames of a 24px icon is seventy
		// kilobytes -- so playing it is picking a texture, and costs what showing a still
		// one costs.
		mutable std::vector<std::unique_ptr<Texture>> mFrames;
		mutable float mLoopSeconds = 0.0f;
		mutable float mPlayhead = 0.0f;

		// Reads the file, or the drawing, for `source`. Separate from ensureLoaded because
		// a drawing needs a size and a size is only known once the widget is placed.
		void ensureDrawn(uint32_t width, uint32_t height) const;
		bool isDrawing() const;

		// Whichever of the two this image is showing, or null.
		const Texture* active() const { return mShared ? mShared : mTexture.get(); }
	};

	// A picture that keeps arriving: a camera, a decoded video, a model's output as it is
	// produced, the result of a filter that runs every frame.
	//
	// The difference from `<image src="mem:...">` is what happens on the second frame.
	// An image is registered, which builds a texture; a stream is pushed, which writes
	// into the one already there. At thirty frames a second that is the difference
	// between a feed and a stall -- see Streams.h.
	//
	// It also reports that it is being looked at. A producer can ask whether anybody is
	// watching before it decodes the next frame, which is what stops a feed on a screen
	// nobody is showing from costing anything.
	class Stream : public Widget {
	public:
		enum class Fit : uint8_t {
			Contain, // the whole frame inside the box, aspect kept, centred
			Stretch, // fill the box, aspect ignored
		};
		explicit Stream(std::string id, std::string name = {})
			: Widget(std::move(id)), name(std::move(name)) {}

		// Which stream to show. Bindable, so one widget can follow whichever feed is
		// selected.
		std::string name;
		Fit         fit = Fit::Contain;

		// The frame's own size in pixels, so width="content" means what it looks like.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};

	// One page of a Tabs. A container with a title; the title is what the tab bar says.
	class Tab : public Container {
	public:
		Tab(std::string id, std::string title)
			: Container(std::move(id)), title(std::move(title)) {}
		std::string title;
	};

	// A row of titles, and the one page whose title is selected.
	//
	// The other pages are not painted and not measured -- they are still there, holding
	// their state, because a Widget is a thing that persists. Selection is an index into
	// the Tab children, so binding it to a signal is the ordinary two-way arrangement
	// that checkbox and slider already use.
	class Tabs : public Container {
	public:
		explicit Tabs(std::string id) : Container(std::move(id)) {}
		int                      value = 0;
		float                    barHeight = 0.0f; // 0 = a line of text plus padding
		Variant                  variant = kDefaultVariant; // the tab buttons' variant
		std::function<void(int)> onChange;

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;

	private:
		float barSize(Gui& gui) const;
		void  paintPage(Gui& gui, Widget& page, const Rect& box, float bar,
		                float dx, float alpha);

		// The page being left, while it is still on its way out.
		//
		// Both pages exist the whole time -- a Widget persists, which is what lets a tab
		// keep its state -- so a transition needs nothing kept alive specially. It only
		// needs the outgoing one to go on being painted for as long as it takes, and the
		// index of what that is.
		int mLeaving = -1;
		int mShown = -1;   // what was showing last frame, to notice a change
	};

	// One choice in a Select. Carries no appearance of its own: the select draws it.
	class Option : public Widget {
	public:
		Option(std::string id, std::string text, std::string value)
			: Widget(std::move(id)), text(std::move(text)), value(std::move(value)) {}
		std::string text;
		std::string value;
		void paint(Gui&, glm::vec2) override {} // drawn by the Select that owns it
	};

	// A closed box showing the chosen option, and a list when it is open.
	//
	// The list is drawn by paintAbove, so it escapes whatever clips this widget and sits
	// over everything -- that is what Gui::drawAbove exists for. onChange fires with the
	// chosen option's `value`, which is a string, so it binds to a text signal the same
	// way a text field does.
	class Select : public Widget {
	public:
		explicit Select(std::string id) : Widget(std::move(id)) {}
		std::string                      value;      // the chosen option's value
		std::string                      placeholder = "Choose...";
		Variant                          variant = kDefaultVariant;
		std::function<void(std::string)> onChange;

		// A declared table to take the choices from, instead of <option> children.
		//
		// <option> is written in the layout and compiled ahead of time, which is right
		// for a fixed set and impossible for one the machine discovers -- the models
		// installed, a recent-files list, the tables in a database. Naming a table here
		// is the same answer <list> already gives, and there is then no maximum to
		// declare and nothing to hide.
		std::string of;
		std::string textColumn  = "text";   // the column a row shows
		std::string valueColumn = "value";  // the column a row means

		// How many choices there are, and what the i-th one says and means.
		//
		// One pair of accessors rather than five loops, because the choices now come from
		// two places and every one of those loops had to agree about which. The <option>
		// path skips a hidden child, which is what makes "declare a maximum and bind
		// visible" work as well -- it silently did not, and drew blank rows.
		size_t choiceCount() const;
		bool   choiceAt(size_t index, std::string_view& text, std::string_view& value) const;

		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
		void paintAbove(Gui& gui, glm::vec2 origin) override;

	protected:
		// Which row the keyboard is on while the list is open. -1 is "none yet", which is
		// what the pointer leaves it at: a list opened with the mouse highlights nothing
		// until an arrow key says where to start.
		int mHighlight = -1;
		void chooseHighlighted();

	private:
		// The choices are the Option children, read where they are rather than copied
		// into a list of their own -- so a binding on an option's text is an ordinary
		// binding on an ordinary widget, and nothing has to be kept in step.
		bool  mOpen = false;
		// How far open the list is drawn, 0 to 1. Not the same question as `mOpen`, which
		// is the answer it is on its way to: the list goes on being drawn after the press
		// that shut it, or it would have nowhere to close from.
		float mOpenAmount = 0.0f;
		Rect  mClosedRect{};     // where the box was drawn, so the list lands under it
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

	// A hole in the interface for something else to fill.
	//
	// The widget decides where it is and how big, and stops there. What appears inside is
	// the application's: a C++ program records Vulkan into it, every other language sends
	// a list of 2D commands the engine draws, and an application that does neither gets
	// the engine's own 3D scene, which is all this element used to be.
	class Viewport : public Widget {
	public:
		explicit Viewport(std::string id) : Widget(std::move(id)) {}

		// How a backend addresses this one. Two viewports with the same name are the same
		// surface drawn twice, which is occasionally what somebody wants.
		std::string name = "main";

		void paint(Gui& gui, glm::vec2 origin) override;
	};
}

