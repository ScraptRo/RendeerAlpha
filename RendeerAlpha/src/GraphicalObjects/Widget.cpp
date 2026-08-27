#include <GraphicalObjects/Widget.h>
#include <cmath>
#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>
#include <algorithm>

namespace RDA {

	Rect Widget::resolveRect(glm::vec2 origin, const Rect& parent) const {
		// Start from the classic behaviour — a fixed rect at a fixed offset — and let the
		// anchors override each axis. With Anchor::TopLeft this is exactly what the
		// widget layer did before anchoring existed.
		Rect out{ origin.x + rect.x, origin.y + rect.y, rect.w, rect.h };

		const bool left = hasAnchor(anchor, Anchor::Left);
		const bool right = hasAnchor(anchor, Anchor::Right);
		const bool top = hasAnchor(anchor, Anchor::Top);
		const bool bottom = hasAnchor(anchor, Anchor::Bottom);

		if (parent.w > 0.0f) {
			if (left && right) {
				// Both edges: the width is whatever is left between the two margins.
				out.w = (std::max)(0.0f, parent.w - rect.x - marginRight);
			} else if (right) {
				// Right only: keep the size, follow the right edge.
				out.x = origin.x + parent.w - marginRight - rect.w;
			}
		}
		if (parent.h > 0.0f) {
			if (top && bottom) {
				out.h = (std::max)(0.0f, parent.h - rect.y - marginBottom);
			} else if (bottom) {
				out.y = origin.y + parent.h - marginBottom - rect.h;
			}
		}

		// A widget never given a size on an axis would come out zero-sized here, and
		// simply not appear — which is never what was meant. Fall back to the space the
		// parent has left, so a child placed straight into a container fills it rather
		// than vanishing, and any layout it runs for its own children has a real extent
		// to divide up. An explicit size, or a pair of anchored edges, is left alone.
		if (!(left && right) && rect.w <= 0.0f && parent.w > 0.0f)
			out.w = (std::max)(0.0f, parent.w - rect.x);
		if (!(top && bottom) && rect.h <= 0.0f && parent.h > 0.0f)
			out.h = (std::max)(0.0f, parent.h - rect.y);
		return out;
	}

	Rect Widget::placement(Gui& gui, glm::vec2 origin) {
		if (mArrangedValid) {
			mArrangedValid = false; // consumed; re-set by the parent next frame
			return mArranged;
		}
		return resolveRect(origin, gui.currentClipRect());
	}

	// ---- Stack ---------------------------------------------------------------------
	namespace {
		// The size a child wants along the stacking axis, before any leftover space is
		// handed out. Fill contributes nothing here — it lives on what remains.
		float mainAxisRequest(const Widget& child, Gui& gui, glm::vec2 available, bool vertical) {
			const SizeSpec& spec = vertical ? child.height : child.width;
			const float avail = vertical ? available.y : available.x;
			switch (spec.mode) {
			case SizeSpec::Mode::Fixed:
				return spec.clamp(spec.value > 0.0f ? spec.value : (vertical ? child.rect.h : child.rect.w), avail);
			case SizeSpec::Mode::Content: {
				const glm::vec2 content = child.measureContent(gui, available);
				return spec.clamp(vertical ? content.y : content.x, avail);
			}
			default:
				return 0.0f; // Fill
			}
		}
	}

	glm::vec2 Stack::measureContent(Gui& gui, glm::vec2 available) const {
		float main = padding * 2.0f;
		int counted = 0;
		for (const auto& child : mChildren) {
			if (!child || !child->visible) continue;
			if (counted++) main += spacing;
			main += mainAxisRequest(*child, gui, available, vertical);
		}
		return vertical ? glm::vec2{ available.x, main } : glm::vec2{ main, available.y };
	}

	void Stack::paint(Gui& gui, glm::vec2 origin) {
		const Rect area = placement(gui, origin);
		const glm::vec2 available{ area.w - padding * 2.0f, area.h - padding * 2.0f };

		// Pass one: what everybody asks for, and how much weight wants the remainder.
		float requested = 0.0f;
		float totalWeight = 0.0f;
		int   visibleCount = 0;
		for (const auto& child : mChildren) {
			if (!child || !child->visible) continue;
			++visibleCount;
			requested += mainAxisRequest(*child, gui, available, vertical);
			const SizeSpec& spec = vertical ? child->height : child->width;
			if (spec.mode == SizeSpec::Mode::Fill) totalWeight += (spec.value > 0.0f ? spec.value : 1.0f);
		}
		if (visibleCount > 1) requested += spacing * (visibleCount - 1);

		const float axisLength = vertical ? available.y : available.x;
		const float leftover = (axisLength > requested) ? (axisLength - requested) : 0.0f;

		// Pass two: place each child in sequence, handing the leftover to the Fill ones.
		float cursor = padding;
		for (size_t i = 0; i < mChildren.size(); ++i) {
			Widget* child = mChildren[i].get();
			if (!child || !child->visible) continue;

			const SizeSpec& mainSpec = vertical ? child->height : child->width;
			float main = mainAxisRequest(*child, gui, available, vertical);
			if (mainSpec.mode == SizeSpec::Mode::Fill && totalWeight > 0.0f) {
				const float weight = mainSpec.value > 0.0f ? mainSpec.value : 1.0f;
				main = mainSpec.clamp(leftover * (weight / totalWeight), axisLength);
			}

			// Cross axis: stretch to the container unless the child asked for a size.
			const SizeSpec& crossSpec = vertical ? child->width : child->height;
			const float crossAvail = vertical ? available.x : available.y;
			float cross = crossAvail;
			if (crossSpec.mode == SizeSpec::Mode::Fixed && crossSpec.value > 0.0f) {
				cross = crossSpec.clamp(crossSpec.value, crossAvail);
			} else if (crossSpec.mode == SizeSpec::Mode::Content) {
				const glm::vec2 content = child->measureContent(gui, available);
				cross = crossSpec.clamp(vertical ? content.x : content.y, crossAvail);
			}

			Rect placed = vertical
				? Rect{ area.x + padding, area.y + cursor, cross, main }
				: Rect{ area.x + cursor, area.y + padding, main, cross };
			child->setArranged(placed);
			child->paint(gui, glm::vec2(placed.x, placed.y));

			cursor += main + spacing;
		}
	}

	// ---- ScrollView --------------------------------------------------------------------
	glm::vec2 ScrollView::measureContent(Gui& gui, glm::vec2 available) const {
		// A scroll view takes the space it is offered. Reporting what it contains would
		// defeat the point — the contents are exactly what is allowed to be too big.
		(void)gui;
		return available;
	}

	void ScrollView::paint(Gui& gui, glm::vec2 origin) {
		const Rect view = placement(gui, origin);
		mViewHeight = view.h;
		mViewTop = view.y;

		// The bar's width is always reserved; see the header for why it must not depend
		// on whether the bar is showing.
		const float inner = (std::max)(0.0f, view.w - padding * 2.0f - barWidth);
		const glm::vec2 measureSpace{ inner, view.h };

		// Pass one: how tall the content is. Heights go in the frame arena so the second
		// pass does not have to measure everything again.
		float* heights = mChildren.empty() ? nullptr
			: gui.frameArena().allocate<float>(mChildren.size());
		float total = padding * 2.0f;
		int counted = 0;
		for (size_t i = 0; i < mChildren.size(); ++i) {
			const Widget* child = mChildren[i].get();
			if (!child || !child->visible) continue;
			const float h = mainAxisRequest(*child, gui, measureSpace, true);
			if (heights) heights[i] = h;
			if (counted++) total += spacing;
			total += h;
		}
		mContentHeight = total;

		const float maxOff = maxOffset();
		const GuiInput& in = gui.input();

		// The wheel, if nothing inside the view already claimed it this frame.
		if (maxOff > 0.0f && in.scroll != 0.0f && !gui.scrollConsumed() && view.contains(in.pointer)) {
			offset -= in.scroll * wheelStep;
			gui.consumeScroll();
		}

		const Rect track{ view.x + view.w - barWidth, view.y, barWidth, view.h };
		float thumbH = 0.0f, range = 0.0f;
		if (maxOff > 0.0f) {
			thumbH = (std::max)(28.0f, view.h * (view.h / total));
			range = (std::max)(0.0f, track.h - thumbH);
			if (!mDraggingThumb && in.pressed && track.contains(in.pointer)) {
				// Grabbing the thumb keeps the offset under the cursor; clicking the bare
				// track picks the thumb up by its middle and jumps there.
				const float thumbY = track.y + (offset / maxOff) * range;
				mThumbGrab = (in.pointer.y < thumbY || in.pointer.y > thumbY + thumbH)
					? thumbH * 0.5f : in.pointer.y - thumbY;
				mDraggingThumb = true;
			}
			if (mDraggingThumb && range > 0.0f)
				offset = ((in.pointer.y - mThumbGrab - track.y) / range) * maxOff;
		}
		if (in.released) mDraggingThumb = false;
		offset = std::clamp(offset, 0.0f, maxOff);

		// Pass two: place the children and paint the ones that can be seen.
		gui.pushClipRect(view);
		float cursor = padding - offset;
		for (size_t i = 0; i < mChildren.size(); ++i) {
			Widget* child = mChildren[i].get();
			if (!child || !child->visible) continue;
			const float h = heights ? heights[i] : mainAxisRequest(*child, gui, measureSpace, true);
			const Rect placed{ view.x + padding, view.y + cursor, inner, h };
			cursor += h + spacing;
			// Scrolled out of sight: nothing to draw and nothing that could be clicked,
			// and skipping it is what keeps a long list costing only what is on screen.
			if (placed.y + placed.h < view.y || placed.y > view.y + view.h) continue;
			child->setArranged(placed);
			child->paint(gui, glm::vec2(placed.x, placed.y));
		}
		gui.popClipRect();

		// The bar last, so it sits above the content rather than under it.
		if (maxOff > 0.0f) {
			const TextFieldStyle& style = gui.theme().textField(variant);
			gui.drawRect(track, style.scrollTrack);
			const Rect thumb{ track.x + 2.0f, track.y + (offset / maxOff) * range,
			                  barWidth - 4.0f, thumbH };
			const bool hot = mDraggingThumb || thumb.contains(in.pointer);
			gui.drawRect(thumb, hot ? style.scrollThumbHover : style.scrollThumb);
		}
	}

	void ScrollView::scrollToShow(const Rect& target) {
		// `target` is in screen space, as arranged by the last paint; undo the shift that
		// paint applied to get back to a position within the content.
		const float top = target.y - mViewTop + offset;
		const float bottom = top + target.h;
		if (top < offset) offset = top;
		else if (bottom > offset + mViewHeight) offset = bottom - mViewHeight;
		offset = std::clamp(offset, 0.0f, maxOffset());
	}

	// ---- walk guard ------------------------------------------------------------------
	namespace {
		int gWalkDepth = 0;

		// Warns rather than asserts: a structural edit during the walk is now survivable
		// (paintChildren re-reads its size), so this points at the cause without taking
		// the application down over something that will merely look odd for a frame.
		void warnIfWalking(const char* what) {
			if (gWalkDepth > 0) {
				RDA_LOG_WARNING("Widget tree edited during the paint walk (" << what
					<< "). Queue it with Gui::tree() instead.");
			}
		}
	}

	WidgetWalkGuard::WidgetWalkGuard() { ++gWalkDepth; }
	WidgetWalkGuard::~WidgetWalkGuard() { --gWalkDepth; }
	bool WidgetWalkGuard::active() { return gWalkDepth > 0; }

	// ---- structural edits ------------------------------------------------------------
	Widget* Widget::addChild(std::unique_ptr<Widget> child) {
		warnIfWalking("addChild");
		Widget* raw = child.get();
		if (raw) raw->mParent = this;
		mChildren.push_back(std::move(child));
		return raw;
	}

	Widget* Widget::insertChild(std::unique_ptr<Widget> child, size_t index) {
		warnIfWalking("insertChild");
		Widget* raw = child.get();
		if (raw) raw->mParent = this;
		if (index >= mChildren.size()) mChildren.push_back(std::move(child));
		else mChildren.insert(mChildren.begin() + index, std::move(child));
		return raw;
	}

	std::unique_ptr<Widget> Widget::detachChild(Widget* child) {
		warnIfWalking("detachChild");
		for (auto it = mChildren.begin(); it != mChildren.end(); ++it) {
			if (it->get() != child) continue;
			std::unique_ptr<Widget> held = std::move(*it);
			mChildren.erase(it);
			held->mParent = nullptr;
			return held;
		}
		return nullptr;
	}

	void WidgetTree::remove(Widget* widget) {
		if (!widget) return;
		mCommands.push_back({ Op::Remove, widget->parent(), widget, nullptr, -1 });
	}

	void WidgetTree::move(Widget* child, int index) {
		if (!child) return;
		mCommands.push_back({ Op::Move, child->parent(), child, nullptr, index });
	}

	void WidgetTree::flush() {
		if (mCommands.empty()) return;
		// Taken by value first: applying a command can run application code that queues
		// more, and those belong to the next flush rather than to this loop.
		std::vector<Command> batch;
		batch.swap(mCommands);

		for (Command& command : batch) {
			if (!command.parent) continue;
			switch (command.op) {
			case Op::Insert:
				command.parent->insertChild(std::move(command.node),
					command.index < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(command.index));
				break;
			case Op::Remove:
				// Dropping the returned owner destroys it here, outside any walk.
				command.parent->detachChild(command.target);
				break;
			case Op::Move:
				command.parent->moveChild(command.target,
					command.index < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(command.index));
				break;
			}
		}
	}

	Widget* Widget::find(const std::string& id) {
		if (mId == id) return this;
		for (auto& child : mChildren) {
			if (Widget* found = child->find(id)) return found;
		}
		return nullptr;
	}

	bool Widget::remove(const std::string& id) {
		warnIfWalking("remove");
		for (auto it = mChildren.begin(); it != mChildren.end(); ++it) {
			if ((*it)->id() == id) {
				mChildren.erase(it);
				return true;
			}
			if ((*it)->remove(id)) return true;
		}
		return false;
	}

	bool Widget::moveChild(Widget* child, size_t index) {
		warnIfWalking("moveChild");
		for (size_t i = 0; i < mChildren.size(); ++i) {
			if (mChildren[i].get() != child) continue;
			if (index >= mChildren.size()) index = mChildren.size() - 1;
			std::unique_ptr<Widget> held = std::move(mChildren[i]);
			mChildren.erase(mChildren.begin() + i);
			mChildren.insert(mChildren.begin() + index, std::move(held));
			return true;
		}
		return false;
	}

	void Widget::paintChildren(Gui& gui, glm::vec2 origin) {
		// Indexed, not a range-for, and the size is re-read every step.
		//
		// A widget's callback can legally change the tree: a button whose onClick adds or
		// removes a sibling is an obvious thing to write, and it happens *during* this
		// walk. A range-for would be iterating a vector that push_back had just
		// reallocated, and the next step would read freed memory.
		//
		// This makes that survivable rather than correct: a child added mid-walk may be
		// painted this frame or next, and removing one can shift the rest. Callers that
		// care should defer structural edits until after the walk, the way DockSpace
		// queues its removals.
		for (size_t i = 0; i < mChildren.size(); ++i) {
			Widget* child = mChildren[i].get();
			if (child && child->visible) child->paint(gui, origin);
		}
	}

	// ---- concrete widgets: each is one immediate-mode call ------------------------
	void Container::paint(Gui& gui, glm::vec2 origin) {
		paintChildren(gui, glm::vec2(origin.x + rect.x, origin.y + rect.y));
	}

	void Panel::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		gui.beginPanel(mId.c_str(), abs, variant);
		// Children are positioned relative to the panel's top-left.
		paintChildren(gui, glm::vec2(abs.x, abs.y));
		gui.endPanel();
	}

	void Label::paint(Gui& gui, glm::vec2 origin) {
		// A named variant's color takes precedence; otherwise the per-instance `color`.
		uint32_t c = (variant != kDefaultVariant) ? gui.theme().label(variant).color : color;
		Rect abs = placement(gui, origin);
		gui.label(text.c_str(), glm::vec2(abs.x, abs.y), c);
	}

	void Button::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.button(mId.c_str(), text.c_str(), abs, variant)) {
			if (onClick) onClick();
		}
	}

	void Checkbox::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.checkbox(mId.c_str(), label.c_str(), value, abs, variant)) {
			if (onChange) onChange(value);
		}
	}

	void Slider::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.sliderFloat(mId.c_str(), value, minValue, maxValue, abs, variant)) {
			if (onChange) onChange(value);
		}
	}

	// ---- measuring text widgets ------------------------------------------------------
	//
	// Without these, SizeSpec::Mode::Content falls back to Widget::measureContent, which
	// reports `rect` — zero for anything a layout container is positioning. Stacked in a
	// column that made every label zero pixels tall and drew them on top of each other,
	// so a layout had to name a pixel height for every piece of text in it.
	//
	// The numbers below mirror what the immediate widgets actually draw, so a measured
	// widget and a hand-sized one land in the same place.

	namespace {
		// A button's text is centred in whatever rect it is given; the style carries no
		// padding of its own, so "as big as its content" has to decide what breathing
		// room means. These are that decision, in one place.
		constexpr float kButtonPadX = 12.0f;
		constexpr float kButtonPadY = 6.0f;
		// Gui::checkbox draws a square box of rect.h and starts the label 8px after it.
		constexpr float kCheckboxGap = 8.0f;
	}

	glm::vec2 Label::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		return { gui.measureText(text.c_str()), gui.lineHeight() };
	}

	glm::vec2 Button::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		return { gui.measureText(text.c_str()) + kButtonPadX * 2.0f,
		         gui.lineHeight() + kButtonPadY * 2.0f };
	}

	glm::vec2 Checkbox::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		const float line = gui.lineHeight();
		// The box is square and as tall as the row, so the row's height is the box.
		return { line + kCheckboxGap + gui.measureText(label.c_str()), line };
	}

	glm::vec2 Slider::measureContent(Gui& gui, glm::vec2 available) const {
		// Nothing to measure: it holds no text. A track about two thirds of a line reads
		// as a control rather than a hairline, and the knob overhangs it by 2px either
		// side the way Gui::sliderFloat draws it.
		return { available.x, std::round(gui.lineHeight() * 0.66f) };
	}

	glm::vec2 TextField::measureContent(Gui& gui, glm::vec2 available) const {
		const TextFieldStyle& used = (variant != kDefaultVariant) ? gui.theme().textField(variant) : style;
		size_t lines = 1;
		for (char c : text) {
			if (c == '\n') ++lines;
		}
		// A single-line field is exactly one row regardless of what it holds.
		if (!used.multiline) lines = 1;
		const float height = static_cast<float>(lines) * gui.lineHeight() + used.padding * 2.0f;
		return { available.x, height };
	}

	void TextField::paint(Gui& gui, glm::vec2 origin) {
		// With an explicit size use it; otherwise fill the container body (the clip rect),
		// so the field grows and shrinks as its container is resized.
		// A zero size still means "fill the container" — the shorthand that predates
		// anchoring, and identical to Anchor::Fill with no margins. An explicit anchor
		// wins over it, so a filled-but-inset layout is expressible.
		// A layout parent's decision always wins over both.
		const bool legacyFill = !mArrangedValid && rect.w <= 0.0f && rect.h <= 0.0f &&
		                        anchor == Anchor::TopLeft;
		Rect abs = legacyFill ? gui.currentClipRect() : placement(gui, origin);
		// A named variant supplies the whole style; otherwise the per-instance one.
		const TextFieldStyle& used = (variant != kDefaultVariant) ? gui.theme().textField(variant) : style;
		if (gui.textField(mId.c_str(), text, abs, used, &focused)) {
			if (onChange) onChange(text);
		}
	}

	void Splitter::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.splitter(mId.c_str(), value, minValue, maxValue, abs, vertical)) {
			if (onChange) onChange(value);
		}
	}

	void Viewport::paint(Gui& gui, glm::vec2 origin) {
		// With an explicit size use it; otherwise fill the container body (the clip rect).
		const bool legacyFill = !mArrangedValid && rect.w <= 0.0f && rect.h <= 0.0f &&
		                        anchor == Anchor::TopLeft;
		Rect area = legacyFill ? gui.currentClipRect() : placement(gui, origin);
		gui.setViewportRect(area); // so the engine renders the scene at this resolution
		const Texture* scene = gui.sceneTexture();
		if (scene) gui.image(area, scene);
		else       gui.drawRect(area, rgba(18, 20, 26)); // Fullscreen mode: no scene texture
	}
}
