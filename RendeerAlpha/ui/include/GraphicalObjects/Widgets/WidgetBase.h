#pragma once
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <GraphicalObjects/GuiTypes.h>
#include <GraphicalObjects/Accessible.h>
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

	// How a widget wants to be sized on one axis. A stack reads it to share out its room,
	// and a widget nothing arranges reads it through freeExtent(). `rect` is the other way
	// of saying it -- the two are alternatives, not a migration: placing things yourself
	// stays a first-class option.
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

		// Whether this says nothing at all, which is what a widget has when a layout wrote
		// no width and no bounds. Zero is "no opinion" here as it is across a stack's axis.
		bool silent() const {
			return mode == Mode::Fixed && value <= 0.0f && min <= 0.0f && max >= kUnbounded && !maxOf;
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

	// How big a widget nothing arranges is on one axis: a child of a <panel> or a
	// <container>, or of the window. `room` is what the parent has left past the widget's
	// own position, and `content` is what measuring it gave, for a size that asked for that.
	//
	// `w` and `h` come first, so a layout that placed things itself is untouched by a width
	// beside them, and a widget that says nothing stays at zero for resolveRect's fallback.
	// Everything else means what it means in a stack, with the one difference a stack would
	// have: there are no siblings to share with, so a fill is all of the room.
	//
	// width and height used to be read by a stack and nothing else. A button in a panel with
	// height={30} filled the panel, with no message, and looked like the property was broken.
	inline float freeExtent(const SizeSpec& spec, float own, float room, float content) {
		if (own > 0.0f || spec.silent()) return own;
		switch (spec.mode) {
		case SizeSpec::Mode::Fixed:   return spec.clamp(spec.value > 0.0f ? spec.value : room, room);
		case SizeSpec::Mode::Content: return spec.clamp(content, room);
		default:                      return spec.clamp(room, room);
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
		virtual ~Widget();

		// How many widgets have been destroyed since the process started. The GUI reads it
		// before a walk and again when the frame ends: anything the walk noted down by
		// pointer -- a shortcut offered, a node for a screen reader, a drop target -- is
		// only safe to follow if nothing has died in between.
		static uint64_t destroyedCount();

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

		// Size preferences. A stack shares its room out by them, and a widget placed by its
		// own x and y sizes itself by them unless `rect` already says -- see freeExtent().
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

		// The right button went down on this.
		//
		// One widget hears it -- the deepest one under the pointer that asked, which is
		// the one drawn last. A panel and a button on it may both want a menu, and a
		// right-click on the button means the button's.
		//
		// It carries nothing, because the thing that wants the position is the menu, and
		// a <popup anchor="pointer"> already knows it. So a handler is what a handler
		// should be: `onContext={() => state.menu = true}`.
		std::function<void()> onContext;

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
		// This is the window's maximise button. Nothing changes about how it draws or
		// clicks; the platform frame is told where it is, which on Windows 11 is what
		// offers snap layouts when the pointer rests on it.
		bool maximizeButton = false;

		// ---- what a screen reader is told ------------------------------------------------
		//
		// What this is called, for somebody who cannot see it. Most widgets already say:
		// a button by its text, a checkbox by its label. This is for the ones that do not
		// -- a slider, an icon, a button holding only a picture -- and for a container that
		// is worth naming as a group.
		std::string alt;
		// Read out by a screen reader when what this says changes -- or when it appears
		// saying something -- without the reader having to go and look: 1 politely, after
		// whatever is being read, 2 at once. On a label, its text; on anything holding
		// several, all the text inside it, as one sentence.
		uint8_t live = 0;
		// What this is, for the accessibility tree: false when it is not a node of its own,
		// and its children hang off whatever is above it. The default is a group when it has
		// an `alt` and nothing otherwise, which is right for every container.
		virtual bool describe(AccessibleInfo& out) const;
		// Something outside asked this to be pressed, toggled, set. Run during the walk, the
		// way a click is, through the same path a click takes.
		virtual void accessibleAction(AccessibleAction action, const std::string& value) {
			(void)action; (void)value;
		}
		// The same, for one of the items a widget describes on its own behalf -- a tab
		// title, a tree row -- named by whatever `item` it gave when it described it.
		virtual void accessibleItemAction(int item, AccessibleAction action, const std::string& value) {
			(void)item; (void)action; (void)value;
		}

		// ---- dragging and dropping -----------------------------------------------------
		//
		// What this carries when it is picked up and dragged, as a string. Empty is not
		// draggable, which is everything unless a layout says otherwise. A string rather
		// than a widget or a row, because what lands on the other end is a handler, and a
		// handler is handed a value: an id, a path, whatever the backend can look up. In a
		// <list> row it is a binding like any other, so each row carries its own.
		std::string drag;
		// What sort of thing it is, so a target can take some and refuse others: a task
		// cannot be dropped on a tag. Empty is "no sort in particular".
		std::string dragKind;
		// The kinds this takes as a drop target, separated by '|'. Empty takes any kind.
		std::string accepts;
		// Something was let go of over this, and this accepts its kind. The deepest target
		// under the pointer hears it, the way a right-click works.
		std::function<void(const std::string&)> onDrop;
		// A drag this would take arrived over it, or left. Reported on the change, like
		// onHover, so a layout can light the target up its own way.
		std::function<void(bool)> onDragOver;
		// Files dragged in from outside the application and let go over this: their paths,
		// one per line. The deepest element under the drop that asked hears it.
		std::function<void(const std::string&)> onFiles;
		bool acceptsKind(std::string_view kind) const;
		// What a picked-up widget shows of itself while it travels: its text, or for a
		// row of several things, the first text inside it. Empty draws the figure alone.
		virtual std::string dragLabel() const;
		// Told by the GUI, which knows when the pointer arrives and leaves during a drag.
		void setDragOver(bool over) {
			if (over == mDragOver) return;
			mDragOver = over;
			if (onDragOver) onDragOver(over);
		}

		// A key chord that does what pressing this does: Ctrl+S on a Save button runs the
		// same onClick a click runs, the way Space does when it has the focus. Nothing is
		// declared twice -- a menu entry and its shortcut are one widget, so they cannot
		// drift apart.
		//
		// Live while this widget could be reached: drawn, or in a closed <popup> -- a menu
		// entry's shortcut has to work with the menu shut, which is the whole point of
		// having one -- or scrolled out of view. Not when it is hidden, on a tab that is
		// not showing, inside a screen on its way out, or behind a menu that is open.
		// Only the widgets that do something when pressed answer it; see pressShortcut.
		Chord shortcut;
		// Parses `text` into `shortcut`, or clears it when empty. A chord that does not
		// parse is reported, naming this widget, and leaves no shortcut rather than a
		// wrong one.
		void setShortcut(std::string_view text);

		// What the shortcut does. Nothing, unless a widget has something to do: a button
		// clicks, a checkbox toggles, a <shortcut> runs its handler.
		virtual void pressShortcut() {}

		// Adds this widget, if it has a shortcut, and everything below it that could be
		// reached, without painting any of it. For a subtree that is not being drawn this
		// frame but whose shortcuts still count -- a closed popup, a child scrolled out of
		// view -- and for the Probe, which draws nothing at all. Overridden where "below
		// it" is not simply every visible child: a tab strip offers only the page that is
		// showing.
		virtual void offerShortcuts(std::vector<Widget*>& out);

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

		// Whether this widget takes room where it sits.
		//
		// Almost everything does, which is why this is a question rather than a property:
		// the one answer that is not "yes" belongs to the widget that knows why.
		//
		// A <popup> is the one. It draws over the interface from its own anchor, and the
		// container holding it is only where it lives in the tree -- so a stack that gave
		// it a share of the room took that room away from its siblings. A closed menu of
		// three buttons made the dockspace above it 76 units short of the window, open or
		// shut, which is a hole nobody could see the cause of.
		//
		// Out of flow means out of *every* flow: not measured, not given a slot, and not
		// counted in a scroll view's range. It is still painted, by its parent, in its own
		// place -- that part was never the container's business.
		virtual bool inFlow() const { return true; }

		// The rect this widget actually occupies, given where the parent put it and how
		// big the parent's content area is. `origin` is the parent's top-left in screen
		// space; `parent` is that same area including its size. `content` is what
		// measureContent() answered, and is only read on an axis sized "content".
		Rect resolveRect(glm::vec2 origin, const Rect& parent, glm::vec2 content = { 0.0f, 0.0f }) const;

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
		bool mDragOver = false;     // what onDragOver last reported
	};

	// Which of the widgets offering a shortcut takes `chord`: the last one offered, which is
	// the one drawn last and so the one in front. Null when none of them has it.
	// `alsoOffered`, when given, is set to a second widget with the same chord -- two live
	// widgets sharing one is a layout that has not decided what those keys mean.
	Widget* pickShortcut(const std::vector<Widget*>& offered, const Chord& chord,
	                     Widget** alsoOffered = nullptr);

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

	// Edits made inside one of these are known not to disturb the walk, and are not warned
	// about.
	//
	// For one case: a list building a row because its view grew, in the middle of drawing
	// itself. What the guard protects is a loop over some widget's children, and nothing
	// loops over a list's: the list draws from its pool, and the walk above it is iterating
	// the list's parent. The row's own widgets are a subtree nobody is walking yet. Anything
	// else that wants this should be as sure of the same thing.
	class WidgetBuildScope {
	public:
		WidgetBuildScope();
		~WidgetBuildScope();
		WidgetBuildScope(const WidgetBuildScope&) = delete;
		WidgetBuildScope& operator=(const WidgetBuildScope&) = delete;
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
}
