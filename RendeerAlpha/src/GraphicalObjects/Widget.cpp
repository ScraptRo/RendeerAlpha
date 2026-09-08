#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/Viewports.h>
#include <cmath>
#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>
#include <RendeerAlpha.h>
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

	float Widget::motionSeconds() const {
		for (const Widget* at = this; at; at = at->mParent) {
			// The barrier is about seeing *past* a widget, not about the widget itself: a
			// list still slides when its container says everything does, and only what the
			// list puts inside itself is excluded. Asked before the value, so a list's own
			// duration is its own and is not handed down to its rows either.
			if (at != this && !at->inheritsMotion()) break;
			if (at->animateMs > 0.0f) return at->animateMs / 1000.0f;
		}
		return 0.0f;
	}

	float Widget::presence(Gui& gui) const {
		const float seconds = motionSeconds();
		if (seconds <= 0.0f) return visible ? 1.0f : 0.0f;
		// Asked for every frame, including while it is nothing, so the value stays in the
		// table: evicted, a hidden widget would forget it was hidden and the next time it
		// was shown it would appear at full size instead of growing into place.
		return gui.motion().value(gui.motionKey(mId.c_str(), kMotionCollapse),
		                          visible ? 1.0f : 0.0f, seconds);
	}

	Rect Widget::placement(Gui& gui, glm::vec2 origin) {
		if (mArrangedValid) {
			mArrangedValid = false; // consumed; re-set by the parent next frame
			return mArranged;
		}
		return resolveRect(origin, gui.currentClipRect());
	}

	// Where a child should be *drawn*, given where the layout just decided it goes.
	//
	// The easing belongs here, in the container that moves its children, and not in
	// placement(): a child is painted with its own position as its origin, so by the time
	// it reads its rect there is no parent frame left to be relative to.
	//
	// Relative to `parentOrigin` rather than in absolute coordinates. If the parent is
	// itself sliding, `placed` already carries that; easing the absolute value would ease
	// the same lag twice and a child inside a moving panel would trail it rubberily. Each
	// level smooths only its own change.
	//
	// What is returned is only what is drawn. `placed` is what the layout computed, and it
	// is what every sibling below is positioned from and what everything measures against
	// -- so nothing here can feed back into how big anything is.
	static Rect easeInto(Gui& gui, Widget& child, const Rect& placed, glm::vec2 parentOrigin) {
		const float seconds = child.motionSeconds();
		if (seconds <= 0.0f) return placed;

		Motion& motion = gui.motion();
		const uint32_t key = gui.motionKey(child.id().c_str(), kMotionPlace);
		return Rect{
			parentOrigin.x + motion.value(key + 0u, placed.x - parentOrigin.x, seconds),
			parentOrigin.y + motion.value(key + 1u, placed.y - parentOrigin.y, seconds),
			motion.value(key + 2u, placed.w, seconds),
			motion.value(key + 3u, placed.h, seconds),
		};
	}

	// ---- Stack ---------------------------------------------------------------------
	namespace {
		// The size a child wants along the stacking axis, before any leftover space is
		// handed out. Fill contributes nothing here — it lives on what remains.
		float mainAxisRequest(const Widget& child, Gui& gui, glm::vec2 available, bool vertical) {
			const SizeSpec& spec = vertical ? child.height : child.width;
			const float avail = vertical ? available.y : available.x;
			const float own = vertical ? child.rect.h : child.rect.w;
			switch (mainAxisSource(spec, own)) {
			case SizeSource::Declared:
				return spec.clamp(spec.value, avail);
			case SizeSource::Rect:
				return spec.clamp(own, avail);
			case SizeSource::Content: {
				const glm::vec2 content = child.measureContent(gui, available);
				return spec.clamp(vertical ? content.y : content.x, avail);
			}
			default:
				return 0.0f; // Fill: the leftover pass decides
			}
		}

		// The size a child needs across the stacking axis. A row is as tall as its
		// tallest child, and no taller.
		float crossAxisRequest(const Widget& child, Gui& gui, glm::vec2 available, bool vertical) {
			const SizeSpec& spec = vertical ? child.width : child.height;
			const float avail = vertical ? available.x : available.y;
			const float own = vertical ? child.rect.w : child.rect.h;
			switch (crossAxisSource(spec)) {
			case SizeSource::Declared:
				// Zero here means "no opinion", and the container stretches it; that is
				// what Stack::paint does with `fills`, so nothing is measured.
				return spec.clamp(spec.value > 0.0f ? spec.value : own, avail);
			case SizeSource::Content: {
				const glm::vec2 content = child.measureContent(gui, available);
				return spec.clamp(vertical ? content.x : content.y, avail);
			}
			default:
				// A child that fills the cross axis wants whatever the row turns out to
				// be, so it cannot be what decides how big the row is.
				return 0.0f;
			}
		}
	}

	glm::vec2 Stack::measureContent(Gui& gui, glm::vec2 available) const {
		float main = padding * 2.0f;
		float cross = 0.0f;
		int counted = 0;
		for (const auto& child : mChildren) {
			if (!child) continue;
			// Asked for every child, gone or not, so a hidden one keeps its place in the
			// table and grows back rather than reappearing at full size.
			const float presence = child->presence(gui);
			if (presence <= 0.004f) continue;

			// Its gap collapses with it. A child that kept its spacing while shrinking
			// would leave a hole and then close it in one frame at the end.
			if (counted++) main += spacing * presence;
			main += mainAxisRequest(*child, gui, available, vertical) * presence;

			// Under a non-stretching alignment a child that named no cross size is given
			// the size of its content, so measuring has to ask what placing will ask.
			// Otherwise a content-sized row would measure one height and fill another.
			float request = crossAxisRequest(*child, gui, available, vertical);
			const SizeSpec& crossSpec = vertical ? child->width : child->height;
			if (request <= 0.0f && crossAlign().word != Align::Stretch
			    && crossSpec.mode != SizeSpec::Mode::Fill) {
				const glm::vec2 content = child->measureContent(gui, available);
				cross = (std::max)(cross, crossSpec.clamp(vertical ? content.x : content.y,
				                                          vertical ? available.x : available.y));
			} else {
				cross = (std::max)(cross, request);
			}
		}
		cross += padding * 2.0f;

		// Both axes are measured. Reporting `available` across the stacking axis -- which
		// this used to do -- made height="content" on a row mean "as tall as the space
		// there is", which is the opposite of what Content means and left a row of
		// buttons filling the window.
		//
		// A stack whose children all fill the cross axis has nothing to measure there, so
		// it falls back to the space on offer rather than collapsing to nothing.
		const float measured = (cross > padding * 2.0f) ? cross
		                                                : (vertical ? available.x : available.y);
		return vertical ? glm::vec2{ measured, main } : glm::vec2{ main, measured };
	}

	void Stack::paint(Gui& gui, glm::vec2 origin) {
		const Rect area = placement(gui, origin);
		const glm::vec2 available{ area.w - padding * 2.0f, area.h - padding * 2.0f };

		// Pass one: what everybody asks for, and how much weight wants the remainder.
		float requested = 0.0f;
		float totalWeight = 0.0f;
		int   visibleCount = 0;
		float gaps = 0.0f;   // fractional, because a collapsing child's gap closes with it
		for (const auto& child : mChildren) {
			if (!child) continue;
			const float presence = child->presence(gui);
			if (presence <= 0.004f) continue;
			if (visibleCount++) gaps += presence;
			requested += mainAxisRequest(*child, gui, available, vertical) * presence;
			const SizeSpec& spec = vertical ? child->height : child->width;
			// A child on its way out stops asking for the remainder, so what it was
			// filling is handed back over the same time it takes to go.
			if (spec.mode == SizeSpec::Mode::Fill) {
				totalWeight += (spec.value > 0.0f ? spec.value : 1.0f) * presence;
			}
		}
		requested += spacing * gaps;

		const float axisLength = vertical ? available.y : available.x;
		const float leftover = (axisLength > requested) ? (axisLength - requested) : 0.0f;

		// Where the row starts, and how much extra goes between children. A Fill child
		// has already taken the remainder, so with one present there is nothing here to
		// share and justification does nothing -- which is the right answer rather than a
		// special case.
		float leading = mainAlign().offset;
		float extraGap = 0.0f;
		if (totalWeight <= 0.0f && leftover > 0.0f) {
			switch (spread) {
			case Distribute::None:
				// Nothing to spread, so the children move together, wherever the
				// alignment for this axis puts them. Stretch means nothing along the
				// axis a child's own `fill` already stretches into, so it reads as Start.
				switch (mainAlign().word) {
				case Align::Center: leading += leftover * 0.5f; break;
				case Align::End:    leading += leftover; break;
				default:            break;
				}
				break;
			case Distribute::SpaceBetween:
				// With one child there is no "between", so it stays where Start puts it.
				if (visibleCount > 1) extraGap = leftover / static_cast<float>(visibleCount - 1);
				break;
			case Distribute::SpaceAround:
				if (visibleCount > 0) {
					extraGap = leftover / static_cast<float>(visibleCount);
					leading = extraGap * 0.5f;
				}
				break;
			}
		}

		// Pass two: place each child in sequence, handing the leftover to the Fill ones.
		float cursor = padding + leading;
		for (size_t i = 0; i < mChildren.size(); ++i) {
			Widget* child = mChildren[i].get();
			if (!child) continue;
			const float presence = child->presence(gui);
			if (presence <= 0.004f) continue;

			const SizeSpec& mainSpec = vertical ? child->height : child->width;
			float main = mainAxisRequest(*child, gui, available, vertical);
			if (mainSpec.mode == SizeSpec::Mode::Fill && totalWeight > 0.0f) {
				const float weight = (mainSpec.value > 0.0f ? mainSpec.value : 1.0f) * presence;
				main = mainSpec.clamp(leftover * (weight / totalWeight), axisLength);
			}
			// The room it gets, rather than the room it asked for. Everything below is
			// laid out from this, so a collapsing child takes the ones after it with it.
			main *= presence;

			// Cross axis. A child that named a size keeps it; one that did not either
			// fills the container or takes the size of its content, depending on `align`.
			//
			// A child may answer the container back: alignSelf is asked first and the
			// container's own setting is the fallback, so "everything stretched except
			// this one, centred" is one property on the exception rather than a wrapper
			// around it.
			const Alignment childCross = vertical ? child->hAlignSelf : child->vAlignSelf;
			const Alignment crossAlign = resolveAlign(this->crossAlign(), childCross);
			const SizeSpec& crossSpec = vertical ? child->width : child->height;
			const float crossAvail = vertical ? available.x : available.y;
			// Through clamp like every other answer: filling means taking the line, less
			// whatever this child's own bounds and offset say about it. Equal means it
			// really did take all of it, and only then is there nothing to align.
			float cross = crossSpec.clamp(crossAvail, crossAvail);
			bool  fills = (cross >= crossAvail);

			if (crossSpec.mode == SizeSpec::Mode::Fixed && crossSpec.value > 0.0f) {
				cross = crossSpec.clamp(crossSpec.value, crossAvail);
				fills = false;
			} else if (crossSpec.mode == SizeSpec::Mode::Content) {
				const glm::vec2 content = child->measureContent(gui, available);
				cross = crossSpec.clamp(vertical ? content.x : content.y, crossAvail);
				fills = false;
			} else if (crossSpec.mode != SizeSpec::Mode::Fill && crossAlign.word != Align::Stretch) {
				// No opinion, and the container is not stretching it: as big as it needs.
				const glm::vec2 content = child->measureContent(gui, available);
				cross = crossSpec.clamp(vertical ? content.x : content.y, crossAvail);
				fills = false;
			}

			// A child with its own size under Stretch sits at the start: the container
			// offered to stretch it and it declined, which does not also mean "move me".
			const float crossOffset = fills ? crossAlign.offset
			                                : crossOffsetFor(crossAlign, crossAvail, cross);

			Rect placed = vertical
				? Rect{ area.x + padding + crossOffset, area.y + cursor, cross, main }
				: Rect{ area.x + cursor, area.y + padding + crossOffset, main, cross };
			// Where it is drawn may lag where it belongs; where it belongs is what the
			// cursor below advances by, so a slide never changes what anything measures.
			//
			// Not eased while collapsing: the size is already being animated by presence,
			// and easing toward an easing value would arrive late and overshoot nothing.
			const Rect drawn = (presence < 0.999f)
				? placed : easeInto(gui, *child, placed, glm::vec2(area.x, area.y));
			child->setArranged(drawn);
			// Outside what is actually visible: nothing to draw, and nothing that could
			// be clicked either, since the clip rect is what input is tested against.
			//
			// The question is "is this inside the clip rect", not "is this inside a
			// scroll view" -- so a long column costs only what is on screen whether it
			// is being scrolled, clipped by a dock panel, or clipped by anything else
			// that has not been written yet. Arranged first regardless, so measuring
			// and scroll-into-view still work on a child that was not drawn.
			if (drawn.overlaps(gui.currentClipRect())) {
				if (presence < 0.999f) {
					// Clipped to the room it has left, so its contents are cut off by the
					// edge closing on them rather than spilling past it, and faded so a
					// half-height text field reads as leaving rather than as broken.
					gui.pushClipRect(drawn);
					gui.pushOpacity(presence);
					child->paint(gui, glm::vec2(drawn.x, drawn.y));
					gui.popOpacity();
					gui.popClipRect();
				} else {
					child->paint(gui, glm::vec2(drawn.x, drawn.y));
				}
			}

			cursor += main + spacing * presence + extraGap;
		}
	}

	// ---- Scroll --------------------------------------------------------------------
	glm::vec2 Scroll::measureContent(Gui& gui, glm::vec2 available) const {
		// A scroll view takes the space it is offered. Reporting what it contains would
		// defeat the point — the contents are exactly what is allowed to be too big.
		(void)gui;
		return available;
	}

	void Scroll::paint(Gui& gui, glm::vec2 origin) {
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
		float widest = 0.0f;
		int counted = 0;
		for (size_t i = 0; i < mChildren.size(); ++i) {
			const Widget* child = mChildren[i].get();
			if (!child || !child->visible) continue;
			const float h = mainAxisRequest(*child, gui, measureSpace, true);
			if (heights) heights[i] = h;
			if (counted++) total += spacing;
			total += h;
			// Only asked when it can be used: measuring every child across the axis
			// costs a text measurement each, and a column that cannot scroll sideways
			// has nothing to do with the answer.
			if (horizontal) {
				const float w = crossAxisRequest(*child, gui, measureSpace, true);
				widest = (std::max)(widest, w);
			}
		}
		mContentHeight = total;
		mContentWidth = horizontal ? widest + padding * 2.0f : 0.0f;

		const float maxOff = maxOffset();
		// Sideways, the room is whatever the widest child needs past the view. The bar
		// along the bottom takes its own strip, the way the vertical one does.
		const float horizontalBar = (horizontal && mContentWidth > view.w) ? barWidth : 0.0f;
		const float maxOffX = horizontal
			? (std::max)(0.0f, mContentWidth - (view.w - barWidth)) : 0.0f;
		const GuiInput& in = gui.input();

		// The wheel, if nothing inside the view already claimed it this frame. Held
		// sideways -- shift, or a wheel that tilts -- it scrolls sideways, which is the
		// convention every other scrolling thing on the machine follows.
		if (in.scroll != 0.0f && !gui.scrollConsumed() && view.contains(in.pointer)) {
			const bool sideways = in.shift || !vertical;
			if (sideways && maxOffX > 0.0f) {
				offsetX -= in.scroll * wheelStep;
				gui.consumeScroll();
			} else if (!sideways && maxOff > 0.0f) {
				offset -= in.scroll * wheelStep;
				gui.consumeScroll();
			}
		}
		offsetX = std::clamp(offsetX, 0.0f, maxOffX);

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

		// See Scroll::paint: `offset` is where it is scrolled to, and this is where it is
		// drawn scrolled to. Not while the thumb is held, because a drag is the hand and
		// the content should not trail it.
		float drawnOffset = offset;
		float drawnOffsetX = offsetX;
		const float scrollSeconds = motionSeconds();
		if (scrollSeconds > 0.0f && !mDraggingThumb) {
			drawnOffset = gui.motion().value(gui.motionKey(mId.c_str(), kMotionScroll),
			                                 offset, scrollSeconds);
			// Its own key, so the two axes ease independently rather than one dragging
			// the other along behind it.
			drawnOffsetX = gui.motion().value(gui.motionKey(mId.c_str(), kMotionScroll ^ 0x5BF03635u),
			                                  offsetX, scrollSeconds);
		}

		// Pass two: place the children and paint the ones that can be seen.
		gui.pushClipRect(view);
		float cursor = padding - drawnOffset;
		for (size_t i = 0; i < mChildren.size(); ++i) {
			Widget* child = mChildren[i].get();
			if (!child || !child->visible) continue;
			const float h = heights ? heights[i] : mainAxisRequest(*child, gui, measureSpace, true);
			// Across the column. Stretching is the default and costs nothing to decide;
			// anything else asks the child how wide it wants to be, which is the same
			// rule a Stack follows and for the same reason -- alignment is only
			// meaningful once a child is allowed to be narrower than its container.
			const Alignment crossAlign = resolveAlign(hAlign, child->hAlignSelf);
			// Stretched, the child takes the column's width less its own bounds and
			// offset -- the same rule a Stack applies to a filling child.
			float width = child->width.clamp(inner, inner);
			float crossOffset = crossAlign.offset;
			if (crossAlign.word != Align::Stretch) {
				width = crossAxisRequest(*child, gui, measureSpace, true);
				// Zero means the child asked to fill; wider than the view is not a size
				// a scroll view can honour, since only the height scrolls.
				if (width <= 0.0f || width > inner) width = inner;
				crossOffset = crossOffsetFor(crossAlign, inner, width);

			}
			const Rect placed{ view.x + padding + crossOffset - drawnOffsetX, view.y + cursor,
			                   width, h };
			cursor += h + spacing;
			// Scrolled out of sight: nothing to draw and nothing that could be clicked,
			// and skipping it is what keeps a long list costing only what is on screen.
			if (placed.y + placed.h < view.y || placed.y > view.y + view.h) continue;
			child->setArranged(placed);
			child->paint(gui, glm::vec2(placed.x, placed.y));
		}
		gui.popClipRect();

		// Sideways, when there is anywhere to go. Plainer than the vertical bar on
		// purpose: it appears only when it is needed, so it has nothing to fade in from.
		if (maxOffX > 0.0f) {
			const TextFieldStyle& style = gui.theme().textField(variant);
			const Rect hTrack{ view.x, view.y + view.h - barWidth,
			                   (std::max)(0.0f, view.w - barWidth), barWidth };
			const float thumbW = (std::max)(28.0f, hTrack.w * (hTrack.w / mContentWidth));
			const float span = (std::max)(0.0f, hTrack.w - thumbW);
			const float frac = (maxOffX > 0.0f) ? (drawnOffsetX / maxOffX) : 0.0f;
			if (!mDraggingThumbX && in.pressed && hTrack.contains(in.pointer)) {
				const float thumbX = hTrack.x + frac * span;
				mThumbGrabX = (in.pointer.x < thumbX || in.pointer.x > thumbX + thumbW)
					? thumbW * 0.5f : in.pointer.x - thumbX;
				mDraggingThumbX = true;
			}
			if (mDraggingThumbX && span > 0.0f) {
				offsetX = ((in.pointer.x - mThumbGrabX - hTrack.x) / span) * maxOffX;
				offsetX = std::clamp(offsetX, 0.0f, maxOffX);
			}
			gui.drawRect(hTrack, style.scrollTrack);
			gui.drawRect(Rect{ hTrack.x + frac * span, hTrack.y + 2.0f, thumbW, barWidth - 4.0f },
			             mDraggingThumbX ? style.scrollThumbHover : style.scrollThumb);
		}
		if (in.released) mDraggingThumbX = false;

		// The bar last, so it sits above the content rather than under it -- and asked
		// for whether or not there is anything to scroll, so it has something to fade in
		// from the next time there is.
		{
			const TextFieldStyle& style = gui.theme().textField(variant);
			const float frac = (maxOff > 0.0f) ? (drawnOffset / maxOff) : 0.0f;
			const Rect real{ track.x + 2.0f, track.y + frac * range, barWidth - 4.0f, thumbH };
			const bool hot = mDraggingThumb || (maxOff > 0.0f && real.contains(in.pointer));
			const Gui::ScrollBarLook bar = gui.scrollBar(
				gui.motionKey(mId.c_str(), kMotionKnob), maxOff > 0.0f, hot,
				thumbH, track.h, style);
			if (bar.presence > 0.004f) {
				gui.pushOpacity(bar.presence);
				gui.drawRect(track, style.scrollTrack);
				const float span = (std::max)(0.0f, track.h - bar.thumb);
				gui.drawRect(Rect{ track.x + 2.0f, track.y + frac * span,
				                   barWidth - 4.0f, bar.thumb }, bar.colour);
				gui.popOpacity();
			}
		}
	}

	void Scroll::scrollToShow(const Rect& target) {
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
		const LabelStyle& style = gui.theme().label(variant);
		uint32_t c = (variant != kDefaultVariant) ? style.color : color;
		Rect abs = placement(gui, origin);
		const float line = gui.lineHeight(style.font);

		if (!wrap) {
			const float x = abs.x + alignOffset(hAlign, abs.w,
			                                    gui.measureText(text.c_str(), style.font), hAlignOffset);
			gui.label(text.c_str(), glm::vec2(x, abs.y + alignOffset(vAlign, abs.h, line, vAlignOffset)),
			          c, style.font);
			return;
		}
		// Broken to the width it was actually given, not the width it asked for: a
		// wrapped label that measured itself against one width and drew against another
		// would leave a gap under it or run past the bottom of what it was allotted.
		//
		// Each line is aligned on its own, which is what alignment of a paragraph means;
		// the block as a whole is what vAlign moves.
		const auto lines = gui.wrapText(text, abs.w, style.font);
		float y = abs.y + alignOffset(vAlign, abs.h, static_cast<float>(lines.size()) * line, vAlignOffset);
		for (const auto& span : lines) {
			const std::string one = text.substr(span.first, span.second);
			const float x = abs.x + alignOffset(hAlign, abs.w,
			                                    gui.measureText(one.c_str(), style.font), hAlignOffset);
			gui.label(one.c_str(), glm::vec2(x, y), c, style.font);
			y += line;
		}
	}

	void Button::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		// With children, the button draws itself around them and its own text is not
		// used -- the children are what it says. Drawn with an empty label rather than
		// through a second code path, so the background, the hover and the press are
		// the same ones every other button has.
		const bool held = !mChildren.empty();
		const bool clicked = gui.button(mId.c_str(), held ? "" : text.c_str(), abs, variant,
		                                hAlign, hAlignOffset);
		if (held) {
			// Inside the padding, and clipped to it: a child that does not fit is cut
			// off at the button's edge rather than drawn over whatever is beside it.
			const Rect inner{ abs.x + padding, abs.y + padding,
			                  (std::max)(0.0f, abs.w - padding * 2.0f),
			                  (std::max)(0.0f, abs.h - padding * 2.0f) };
			gui.pushClipRect(inner);
			paintChildren(gui, glm::vec2(inner.x, inner.y));
			gui.popClipRect();
		}
		if (clicked && onClick) onClick();
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
		// kButtonPadX / kButtonPadY are in GuiTypes.h, because Gui::button needs the same
		// numbers to inset aligned text by.
		//
		// Gui::checkbox draws a square box of rect.h and starts the label 8px after it.
		constexpr float kCheckboxGap = 8.0f;
	}


	// ---- Image ---------------------------------------------------------------------
	Image::~Image() { release(); }

	// The texture is not destroyed here, and cannot be.
	//
	// This widget is destroyed from the application's update -- a screen swapped, a layout
	// reloaded -- and by then the frame's draw list has already been built, holding this
	// texture's address. It is recorded from after the update returns. Destroying the
	// texture here means the GUI records a descriptor from freed memory, which is what an
	// `Invalid VkImageView 0xdddddddddddddddd` is: the fill pattern of a freed heap block.
	//
	// So ownership goes to the engine, which destroys it at the top of a frame -- once the
	// GPU is idle and the GUI has released the descriptor set it cached by that address.
	void Image::release() const {
		if (mTexture) rendeerRetireTexture(std::move(mTexture));
		mTexture.reset();
	}

	void Image::ensureLoaded() const {
		if (mLoaded == source && ((mTexture && mTexture->isValid()) || mFailed)) return;
		release();
		mLoaded = source;
		mFailed = false;
		if (source.empty()) { mFailed = true; return; }
		mTexture = std::make_unique<Texture>(Texture::loadFromFile(source));
		if (!mTexture->isValid()) {
			mFailed = true;
			mTexture.reset();
			RDA_LOG_WARNING("Image '" << mId << "': cannot load " << source);
		}
	}

	glm::vec2 Image::measureContent(Gui& gui, glm::vec2 available) const {
		(void)gui;
		ensureLoaded();
		if (!mTexture) return { 0.0f, 0.0f };
		const glm::vec2 own(static_cast<float>(mTexture->extent().width),
		                    static_cast<float>(mTexture->extent().height));
		// A picture bigger than the room it is in asks for the room, keeping its shape --
		// otherwise width="content" on a photograph asks for four thousand pixels.
		if (available.x <= 0.0f || own.x <= available.x) return own;
		return { available.x, own.y * (available.x / own.x) };
	}

	void Image::paint(Gui& gui, glm::vec2 origin) {
		ensureLoaded();
		if (!mTexture) return;
		const Rect box = placement(gui, origin);
		if (fit == Fit::Stretch) { gui.image(box, mTexture.get()); return; }

		// Contain: the largest rectangle of the picture's shape that fits, centred. Done
		// here rather than in a shader, which keeps the quad the thing that is positioned
		// -- the same as everything else in this file.
		const float ow = static_cast<float>(mTexture->extent().width);
		const float oh = static_cast<float>(mTexture->extent().height);
		if (ow <= 0.0f || oh <= 0.0f || box.w <= 0.0f || box.h <= 0.0f) return;
		const float scale = (std::min)(box.w / ow, box.h / oh);
		const float w = ow * scale;
		const float h = oh * scale;
		gui.image({ box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h }, mTexture.get());
	}

	// ---- Tabs ----------------------------------------------------------------------
	float Tabs::barSize(Gui& gui) const {
		return barHeight > 0.0f ? barHeight : std::round(gui.lineHeight() + 10.0f);
	}

	glm::vec2 Tabs::measureContent(Gui& gui, glm::vec2 available) const {
		const float bar = barSize(gui);
		// As tall as the bar plus the page showing, because the pages that are not showing
		// are not there as far as layout is concerned.
		glm::vec2 page{ 0.0f, 0.0f };
		int index = 0;
		for (const auto& child : children()) {
			if (!dynamic_cast<Tab*>(child.get())) continue;
			if (index++ == value) {
				page = child->measureContent(gui, { available.x, available.y - bar });
				break;
			}
		}
		return { (std::max)(available.x, page.x), bar + page.y };
	}

	void Tabs::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		const float bar = barSize(gui);

		// The bar. Each title is a button, so hovering and pressing come from the theme
		// like everything else; the selected one is drawn with this widget's variant and
		// the rest with the default, which is the whole of "looks selected".
		float x = box.x;
		int index = 0;
		gui.pushId(mId.c_str());
		for (const auto& child : children()) {
			Tab* tab = dynamic_cast<Tab*>(child.get());
			if (!tab) continue;
			const float w = gui.measureText(tab->title.c_str()) + 22.0f;
			const Variant look = (index == value) ? variant : kDefaultVariant;
			if (gui.button(tab->id().c_str(), tab->title.c_str(), { x, box.y, w, bar }, look)
			    && index != value) {
				value = index;
				if (onChange) onChange(index);
			}
			x += w + 2.0f;
			++index;
		}
		gui.popId();

		// ---- the page ----------------------------------------------------------------
		const float seconds = motionSeconds();
		const uint32_t key = gui.motionKey(mId.c_str(), kMotionOpen);

		// A page changed. The one being left goes on being painted until the transition is
		// over, and keeping it costs nothing because it was never destroyed: a Tab is a
		// Widget and a Widget persists, which is the same property that lets a tab keep
		// what was typed into it.
		if (mShown != value) {
			if (seconds > 0.0f && mShown >= 0) {
				mLeaving = mShown;
				gui.motion().reset(key, 0.0f); // the transition begins at its beginning
			}
			mShown = value;
		}

		// 0 the moment the page changed, 1 once the new one has fully arrived.
		float progress = 1.0f;
		if (seconds > 0.0f && mLeaving >= 0) {
			progress = gui.motion().value(key, 1.0f, seconds, Easing::InOut);
			if (progress >= 0.999f) mLeaving = -1;
		}

		// Which way they slide: going forward through the bar moves left, going back moves
		// right, so the movement agrees with where the pages sit in the row of titles.
		const float direction = (mLeaving > value) ? -1.0f : 1.0f;
		const float travel = box.w * 0.06f; // a hint of movement, not a journey

		index = 0;
		for (const auto& child : children()) {
			Tab* tab = dynamic_cast<Tab*>(child.get());
			if (!tab) continue;
			const int at = index++;
			if (at == value) {
				paintPage(gui, *tab, box, bar, direction * travel * (1.0f - progress), progress);
			} else if (at == mLeaving) {
				paintPage(gui, *tab, box, bar, -direction * travel * progress, 1.0f - progress);
			}
		}
	}

	// One page of a Tabs, offset sideways and faded by however far through it is.
	//
	// Clipped to the body so a page on its way in or out draws neither over the row of
	// titles nor past the edge of what the tabs were given. The rect stays relative to the
	// box and the origin stays the box, exactly as it is without a transition -- the slide
	// is the only thing added.
	void Tabs::paintPage(Gui& gui, Widget& page, const Rect& box, float bar,
	                     float dx, float alpha) {
		if (alpha <= 0.004f) return;
		gui.pushClipRect({ box.x, box.y + bar, box.w, box.h - bar });
		gui.pushOpacity(alpha);
		page.rect = { dx, bar, box.w, box.h - bar };
		page.paint(gui, glm::vec2(box.x, box.y));
		gui.popOpacity();
		gui.popClipRect();
	}

	// ---- Select --------------------------------------------------------------------
	glm::vec2 Select::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		// Wide enough for the longest choice, so opening the list does not change what
		// the closed box looks like.
		float widest = gui.measureText(placeholder.c_str());
		for (const auto& child : children()) {
			if (const Option* one = dynamic_cast<const Option*>(child.get())) {
				widest = (std::max)(widest, gui.measureText(one->text.c_str()));
			}
		}
		return { widest + 34.0f, std::round(gui.lineHeight() + 10.0f) };
	}

	void Select::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		mClosedRect = box;

		const ButtonStyle& s = gui.theme().button(variant);
		const bool inside = box.contains(gui.input().pointer);
		if (inside && gui.input().pressed) { mOpen = !mOpen; gui.setFocus(gui.widgetId(mId.c_str())); }

		// ---- the keyboard ------------------------------------------------------------
		//
		// Shut, it opens on Enter or Space. Open, the arrows move a highlight, Enter
		// takes what is highlighted and Escape leaves without changing anything -- which
		// is the one thing a dropdown must always allow and the reason it is listed
		// separately from choosing.
		const uint32_t wid = gui.widgetId(mId.c_str());
		gui.focusable(wid, box);
		int options = 0;
		for (const auto& child : children()) {
			if (dynamic_cast<const Option*>(child.get())) ++options;
		}
		if (gui.focusActivated(wid)) {
			if (!mOpen) {
				mOpen = true;
				// Opened from the keyboard, it starts on whatever is already chosen, so
				// the first arrow moves from there rather than from the top.
				mHighlight = 0;
				int at = 0;
				for (const auto& child : children()) {
					const Option* one = dynamic_cast<const Option*>(child.get());
					if (!one) continue;
					if (one->value == value) { mHighlight = at; break; }
					++at;
				}
			} else {
				chooseHighlighted();
			}
		} else if (mOpen) {
			if (const int step = gui.focusStep(wid); step != 0 && options > 0) {
				if (mHighlight < 0) mHighlight = (step > 0) ? 0 : options - 1;
				else mHighlight = (mHighlight + step + options) % options;
			}
			if (gui.focusEscaped(wid)) { mOpen = false; mHighlight = -1; }
		}

		// How far open it is, which is what the list is drawn from -- and is the reason
		// the list goes on being drawn after `mOpen` is false, since otherwise it would
		// have nowhere to close from. Asked for every frame, open or shut: a value nobody
		// asks for is dropped, and one that had been dropped would open at full height.
		mOpenAmount = mOpen ? 1.0f : 0.0f;
		if (s.motion.seconds > 0.0f) {
			mOpenAmount = gui.motion().value(gui.motionKey(mId.c_str(), kMotionOpen),
			                                 mOpenAmount, s.motion.seconds, s.motion.curve);
		}

		// The box's own fill follows what it is doing, like a button's -- which it did not
		// before, so a select was the one control on the screen that still snapped.
		gui.drawRectRounded(box, gui.motion().colour(
			gui.motionKey(mId.c_str(), kMotionFill),
			mOpen ? s.pressed : (inside ? s.hovered : s.normal),
			s.motion.seconds, s.motion.curve), s.radius);

		// Whatever is chosen, or the placeholder when the value matches no option -- which
		// is what an unset signal looks like, and reads better than an empty box.
		const char* shown = placeholder.c_str();
		int count = 0;
		for (const auto& child : children()) {
			const Option* one = dynamic_cast<const Option*>(child.get());
			if (!one) continue;
			++count;
			if (one->value == value) shown = one->text.c_str();
		}
		const float textY = box.y + (box.h - gui.lineHeight(s.font)) * 0.5f;
		gui.drawText(shown, { box.x + 10.0f, textY }, s.text, s.font);

		// The chevron. There is no transform to turn one over with -- every quad this GUI
		// draws is axis-aligned -- so the two glyphs cross-fade in place instead, which
		// over a tenth of a second reads as the one becoming the other.
		const glm::vec2 chevron{ box.x + box.w - 18.0f, textY };
		if (mOpenAmount < 0.999f) {
			gui.drawText("v", chevron, fadeTo(s.text, 1.0f - mOpenAmount), s.font);
		}
		if (mOpenAmount > 0.001f) {
			gui.drawText("^", chevron, fadeTo(s.text, mOpenAmount), s.font);
		}

		// The list has to escape whatever clips this widget, so it is not drawn here.
		if (mOpenAmount > 0.004f && count > 0) gui.drawAbove(this, origin);
	}

	// What Enter does with the row the keyboard is on. Written once and called from the
	// key handling, so a keyboard choice and a mouse choice are the same event.
	void Select::chooseHighlighted() {
		mOpen = false;
		if (mHighlight < 0) return;
		int at = 0;
		for (const auto& child : children()) {
			const Option* one = dynamic_cast<const Option*>(child.get());
			if (!one) continue;
			if (at++ != mHighlight) continue;
			if (value != one->value) {
				value = one->value;
				if (onChange) onChange(one->value);
			}
			return;
		}
	}

	void Select::paintAbove(Gui& gui, glm::vec2 origin) {
		(void)origin;
		const ButtonStyle& s = gui.theme().button(variant);
		const float rowHeight = std::round(gui.lineHeight(s.font) + 8.0f);
		size_t count = 0;
		for (const auto& child : children()) {
			if (dynamic_cast<const Option*>(child.get())) ++count;
		}
		const Rect list{ mClosedRect.x, mClosedRect.y + mClosedRect.h + 2.0f,
		                 mClosedRect.w, rowHeight * static_cast<float>(count) };

		// Unrolled to however far open it is. The rows are laid out where they will
		// finally sit and the box revealing them grows past them, so it reads as a list
		// coming out of the control rather than as a panel appearing beside it.
		const Rect shown{ list.x, list.y, list.w, list.h * mOpenAmount };
		gui.pushClipRect(shown);
		gui.pushOpacity(mOpenAmount);
		// One on its way shut takes no input. It is still on the screen, and without this
		// the press that closed it would also pick whatever row it happened to be over.
		const bool closing = !mOpen;
		if (closing) gui.pushInert();

		// An open list is part of this control as far as the keyboard is concerned.
		//
		// The press that lands on a row is not on the control's own rect, so without this
		// it reads as a press on nothing and takes the keyboard away -- and since a row is
		// chosen on release, the value would still arrive while the ring quietly vanished.
		// Claimed rather than re-focused: it already has focus, and this only says the
		// press was ours.
		if (!closing && gui.input().pressed && shown.contains(gui.input().pointer)) {
			gui.setFocus(gui.widgetId(mId.c_str()));
		}

		// Opaque, whatever the panel style says. A panel is translucent on purpose -- it
		// sits over content and is meant to. A dropdown is not: it is a list of words to
		// read, and the label it covers showing through it makes both unreadable.
		const uint32_t opaque = gui.theme().panel(kDefaultVariant).body | 0xFF000000u;
		gui.drawRectRounded(shown, opaque, s.radius);

		float y = list.y;
		int index = 0;
		for (const auto& child : children()) {
			const Option* one = dynamic_cast<const Option*>(child.get());
			if (!one) continue;
			const Rect row{ list.x, y, list.w, rowHeight };
			// The keyboard's row looks exactly like the pointer's: there is one
			// highlight in a list, however the reader is moving it.
			const bool over = row.contains(gui.input().pointer) || (index == mHighlight);
			++index;
			// Faded to nothing rather than not drawn, so a pointer running down the list
			// leaves each row on its way out instead of switching them on and off. Only
			// the alpha moves: the two ends are the same colour.
			gui.drawRectRounded(row, gui.motion().colour(
				gui.motionKey(one->id().c_str(), kMotionFill),
				over ? s.hovered : fadeTo(s.hovered, 0.0f),
				s.motion.seconds, s.motion.curve), 0.0f);
			gui.drawText(one->text.c_str(),
			             { row.x + 10.0f, row.y + (row.h - gui.lineHeight(s.font)) * 0.5f },
			             s.text, s.font);
			// Chosen on release rather than press: the press is what opened the list, and
			// acting on it would pick whatever happened to be under the pointer then.
			if (over && gui.input().released) {
				mOpen = false;
				if (value != one->value) {
					value = one->value;
					if (onChange) onChange(one->value);
				}
			}
			y += rowHeight;
		}

		if (closing) gui.popInert();
		gui.popOpacity();
		gui.popClipRect();

		// A press anywhere else closes it. Checked after the rows so a press on one of
		// them is not also a press outside, and outside the block above so it is asked
		// with the real pointer -- a list already closing has nothing to close.
		if (mOpen && gui.input().pressed && !list.contains(gui.input().pointer)
		    && !mClosedRect.contains(gui.input().pointer)) {
			mOpen = false;
		}
	}

	glm::vec2 Label::measureContent(Gui& gui, glm::vec2 available) const {
		const TextStyle& font = gui.theme().label(variant).font;
		const float line = gui.lineHeight(font);
		if (!wrap) return { gui.measureText(text.c_str(), font), line };
		// Wrapping turns a width into a height, which is the one case where measuring
		// has to look at the space it was offered rather than only at its own content.
		const size_t lines = gui.wrapText(text, available.x, font).size();
		return { available.x, static_cast<float>(lines) * line };
	}

	glm::vec2 Button::measureContent(Gui& gui, glm::vec2 available) const {
		// Around its children when it has them: as big as the biggest, plus the padding
		// on both sides. A child that fills asks for nothing, so a button holding only
		// those falls back to the room it was offered rather than collapsing.
		if (!mChildren.empty()) {
			const glm::vec2 room{ (std::max)(0.0f, available.x - padding * 2.0f),
			                      (std::max)(0.0f, available.y - padding * 2.0f) };
			glm::vec2 content{ 0.0f, 0.0f };
			for (const auto& child : mChildren) {
				if (!child || !child->visible) continue;
				const glm::vec2 one = child->measureContent(gui, room);
				content.x = (std::max)(content.x, one.x);
				content.y = (std::max)(content.y, one.y);
			}
			return { content.x + padding * 2.0f, content.y + padding * 2.0f };
		}
		(void)available;
		const TextStyle& font = gui.theme().button(variant).font;
		return { gui.measureText(text.c_str(), font) + kButtonPadX * 2.0f,
		         gui.lineHeight(font) + kButtonPadY * 2.0f };
	}

	glm::vec2 Checkbox::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		const TextStyle& font = gui.theme().checkbox(variant).font;
		const float line = gui.lineHeight(font);
		// The box is square and as tall as the row, so the row's height is the box.
		return { line + kCheckboxGap + gui.measureText(label.c_str(), font), line };
	}

	glm::vec2 Slider::measureContent(Gui& gui, glm::vec2 available) const {
		// Nothing to measure: it holds no text. A track about two thirds of a line reads
		// as a control rather than a hairline, and the knob overhangs it by 2px either
		// side the way Gui::sliderFloat draws it.
		return { available.x, std::round(gui.lineHeight() * 0.66f) };
	}

	glm::vec2 TextField::measureContent(Gui& gui, glm::vec2 available) const {
		const TextFieldStyle& used = (variant != kDefaultVariant) ? gui.theme().textField(variant) : style;
		// Counted once per edit rather than once per measure: a content-sized field is
		// measured at least once a frame, and counting newlines walks the whole document.
		if (mLinesVersion != mTextVersion) {
			mLines = 1;
			for (char c : mText) {
				if (c == '\n') ++mLines;
			}
			mLinesVersion = mTextVersion;
		}
		size_t lines = mLines;
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
		if (gui.textField(mId.c_str(), mText, mPlaceholder, abs, used, &focused, mTextVersion)) {
			// The field edited the string in place, so nothing else can have noticed.
			++mTextVersion;
			if (onChange) onChange(mText);
		}
	}

	// ---- Scroll ---------------------------------------------------------------------


	void Splitter::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		if (gui.splitter(mId.c_str(), value, minValue, maxValue, abs, vertical)) {
			if (onChange) onChange(value);
		}
	}

	namespace {
		// Replays a backend's drawing inside the viewport.
		//
		// Coordinates are the viewport's own, so a drawing never has to know where on
		// screen it ended up -- the same list is correct in a docked pane, a dialog, or
		// full width. The clip is what makes that safe: a drawing that overruns is
		// cropped at the edge rather than loose in somebody else's interface.
		void paintCommands(Gui& gui, const Rect& area, const std::vector<DrawCommand>& commands) {
			gui.pushClipRect(area);
			for (const DrawCommand& c : commands) {
				switch (c.op) {
				case DrawOp::Clear:
					gui.drawRect(area, c.color);
					break;
				case DrawOp::Rect:
					gui.drawRectRounded({ area.x + c.a, area.y + c.b, c.c, c.d }, c.color, c.e);
					break;
				case DrawOp::Line:
					gui.drawLine({ area.x + c.a, area.y + c.b }, { area.x + c.c, area.y + c.d },
					             c.color, c.e > 0.0f ? c.e : 1.0f);
					break;
				case DrawOp::Text: {
					TextStyle style;
					style.size = c.e; // 0 means the interface's own size, which is the default
					gui.drawText(c.text.c_str(), { area.x + c.a, area.y + c.b }, c.color, style);
					break;
				}
				}
			}
			gui.popClipRect();
		}
	}

	void Viewport::paint(Gui& gui, glm::vec2 origin) {
		// With an explicit size use it; otherwise fill the container body (the clip rect).
		const bool legacyFill = !mArrangedValid && rect.w <= 0.0f && rect.h <= 0.0f &&
		                        anchor == Anchor::TopLeft;
		Rect area = legacyFill ? gui.currentClipRect() : placement(gui, origin);

		Viewports& all = viewports();
		all.reportPlacement(name, area); // the size a backend lays its drawing out to

		if (const std::vector<DrawCommand>* commands = all.commands(name)) {
			paintCommands(gui, area, *commands);
			return;
		}

		// Not a 2D one, so it is the GPU's -- if this is the viewport that got it. The
		// rect goes back to the engine, which sizes the offscreen target to match and
		// leaves this area out of the composited GUI so the target shows through.
		if (all.gpuViewport() == name) {
			gui.setViewportRect(area);
			const Texture* scene = gui.sceneTexture();
			if (scene) { gui.image(area, scene); return; }
			// Fullscreen mode: the scene goes straight to the window and there is no
			// texture to show. A C++ callback needs ViewportMode::Widget for the same
			// reason -- without an offscreen target there is nothing to record into.
			gui.drawRect(area, rgba(18, 20, 26));
			return;
		}

		// A second viewport, with nothing of its own to draw.
		gui.drawRect(area, rgba(18, 20, 26));
	}
}
