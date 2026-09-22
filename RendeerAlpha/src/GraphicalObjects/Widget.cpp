#include <GraphicalObjects/Widget.h>
#include <GraphicalObjects/Images.h>
#include <GraphicalObjects/Streams.h>
#include <Core/Svg.h>
#include <Core/Route.h>
#include <GraphicalObjects/Viewports.h>
#include <Core/Tables.h>
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
		const Rect where = [&] {
			if (mArrangedValid) {
				mArrangedValid = false; // consumed; re-set by the parent next frame
				return mArranged;
			}
			const Rect resolved = resolveRect(origin, gui.currentClipRect());
			// A widget a container placed by its own x and y was never eased at all --
			// only a stack's children were, and a stack has no x and y to move between.
			// So this is the other half of what a route is for, and it is opt-in twice
			// over: a route has to be written, and a duration has to be given.
			const float seconds = motionSeconds();
			if (route.empty() || seconds <= 0.0f) return resolved;
			const Route::Shape* shape = Route::named(route);
			Motion& motion = gui.motion();
			const uint32_t key = gui.motionKey(mId.c_str(), kMotionPlace);
			const glm::vec2 travelled =
				motion.along(key, { resolved.x, resolved.y }, seconds, pace, shape);
			return Rect{ travelled.x, travelled.y, resolved.w, resolved.h };
		}();

		// Where this ended up, for whoever asked -- a <popup> anchored to it, and nothing
		// else so far. Free when nobody is asking.
		gui.noteAnchorIfWanted(mId, where);

		// Declared before this widget's children paint, so a button sitting on the bar
		// wins the press over the bar itself.
		if (dragWindow) gui.windowGrip(mId.c_str(), where);

		// Hover, for whoever asked. Only the change is reported: a handler wants to know
		// the pointer arrived, not that it is still here.
		if (onHover) {
			const bool over = where.contains(gui.input().pointer);
			if (over != mHovered) {
				mHovered = over;
				onHover(over);
			}
		}
		return where;
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
		// Position travels; size does not. A route is a route through space, and a widget
		// growing is not going anywhere -- so the two halves are paced the same and only
		// one of them follows a shape.
		const glm::vec2 where = motion.along(
			key, { placed.x - parentOrigin.x, placed.y - parentOrigin.y }, seconds,
			child.pace, Route::named(child.route));
		return Rect{
			parentOrigin.x + where.x,
			parentOrigin.y + where.y,
			motion.value(key + 2u, placed.w, seconds, child.pace),
			motion.value(key + 3u, placed.h, seconds, child.pace),
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
			// The size it would have if it were not going anywhere, kept for what is
			// inside it -- see the slot and the block below. A child that fills has no
			// size of its own to preserve, so for that one the two are the same and its
			// contents genuinely do shrink.
			const float fullMain = (mainSpec.mode == SizeSpec::Mode::Fill)
				? main : mainAxisRequest(*child, gui, available, vertical);
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
			// This is the one case `route` and `pace` do not shape, and the one an author
			// reaches for them in first -- said out loud in docs/frontend/motion.md.
			const bool collapsing = presence < 0.999f;
			const Rect slot = collapsing
				? placed : easeInto(gui, *child, placed, glm::vec2(area.x, area.y));

			// The slot is the room that is left; `inside` is the block that is in it.
			//
			// They are the same thing until something collapses, and then they are not:
			// the contents keep their full size and slide, so that the far edge closing
			// on them pushes them along rather than eating them. A panel's padding
			// survives, a centred label stays centred, and nothing re-wraps on the way
			// out -- which the other two answers cannot all manage at once. Laying the
			// contents out again each frame re-wraps text while it disappears, and
			// leaving them anchored (what this did) slices a button in half down the
			// middle and cuts a row of text through the letters.
			//
			// Which way they travel follows the edge: a sidebar closing to the left takes
			// its insides left, a panel closing upwards takes them up. In both the near
			// edge is fixed -- that is where the widget starts -- so the far edge is the
			// one doing the pushing.
			Rect inside = slot;
			if (collapsing) {
				if (vertical) {
					inside.y = slot.y + slot.h - fullMain;
					inside.h = fullMain;
				} else {
					inside.x = slot.x + slot.w - fullMain;
					inside.w = fullMain;
				}
			}
			child->setArranged(inside);
			// Outside what is actually visible: nothing to draw, and nothing that could
			// be clicked either, since the clip rect is what input is tested against.
			//
			// The question is "is this inside the clip rect", not "is this inside a
			// scroll view" -- so a long column costs only what is on screen whether it
			// is being scrolled, clipped by a dock panel, or clipped by anything else
			// that has not been written yet. Arranged first regardless, so measuring
			// and scroll-into-view still work on a child that was not drawn.
			if (slot.overlaps(gui.currentClipRect())) {
				if (collapsing) {
					// Clipped to the room it has left, so what has already slid past the
					// near edge is gone rather than drawn over its neighbour -- every
					// clip below this intersects with it, so nothing inside can widen it
					// back out. Faded as well, so a panel on its way out reads as leaving
					// rather than as a window onto half of itself.
					gui.pushClipRect(slot);
					gui.pushOpacity(presence);
					child->paint(gui, glm::vec2(inside.x, inside.y));
					gui.popOpacity();
					gui.popClipRect();
				} else {
					child->paint(gui, glm::vec2(slot.x, slot.y));
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

	// ---- Popup -----------------------------------------------------------------------
	glm::vec2 Popup::measureContent(Gui& gui, glm::vec2 available) const {
		// As big as what is inside it. A popup is not in anyone's flow, so nothing gives
		// it a size and this is what decides how big it comes out.
		//
		// A child that names a fixed size is taken at its word rather than measured. A
		// vertical <stack width={160}> measures its *content* width, which for a stack is
		// whatever room it was offered -- so measuring it here made every popup as wide as
		// the window. What the layout wrote is the better answer whenever it wrote one.
		glm::vec2 content{ 0.0f, 0.0f };
		for (const auto& child : mChildren) {
			if (!child || !child->visible) continue;
			const glm::vec2 measured = child->measureContent(gui, available);
			const float w = child->width.mode == SizeSpec::Mode::Fixed && child->width.value > 0.0f
				? child->width.value : measured.x;
			const float h = child->height.mode == SizeSpec::Mode::Fixed && child->height.value > 0.0f
				? child->height.value : measured.y;
			content.x = (std::max)(content.x, w);
			content.y = (std::max)(content.y, h);
		}
		return content;
	}

	void Popup::paint(Gui& gui, glm::vec2 origin) {
		(void)origin;
		// Asked for every frame, open or shut. The note is only taken for anchors
		// somebody wants, and "somebody wants it" has to be true before the frame the
		// popup opens on -- otherwise it would have nowhere to appear the first time.
		if (!anchor.empty()) gui.wantAnchor(anchor);
		if (!open) return;

		// Nothing is drawn here: a popup that drew in the tree would be clipped by
		// whatever panel it happens to live in, and covered by whatever comes after it.
		gui.drawAbove(this, glm::vec2(0.0f));

		// Asked for now rather than in paintAbove, because the claim is for the *next*
		// frame's walk and paintAbove runs after this frame's has finished either way.
		if (blocking) gui.claimPointer();
	}

	void Popup::paintAbove(Gui& gui, glm::vec2 origin) {
		(void)origin;
		const glm::vec2 viewport = gui.input().viewport;

		// How big it wants to be. A size written on the popup wins, the way it does
		// everywhere else; otherwise it is as big as what is inside it.
		const glm::vec2 wanted = measureContent(gui, viewport);
		float w = rect.w > 0.0f ? rect.w : wanted.x + padding * 2.0f;
		float h = rect.h > 0.0f ? rect.h : wanted.y + padding * 2.0f;
		w = (std::min)(w, viewport.x);
		h = (std::min)(h, viewport.y);

		// Where it goes. With no anchor it is placed like anything else -- its own x/y --
		// so a popup can be a plain floating panel without inventing a second mechanism.
		Rect anchorRect{ rect.x, rect.y, 0.0f, 0.0f };
		bool anchored = false;
		if (!anchor.empty()) anchored = gui.anchorRect(anchor, anchorRect);

		float x = anchorRect.x;
		float y = anchorRect.y;
		if (anchored) {
			switch (placement) {
			case Placement::Below: y = anchorRect.y + anchorRect.h + gap; break;
			case Placement::Above: y = anchorRect.y - gap - h;            break;
			case Placement::Right: x = anchorRect.x + anchorRect.w + gap; break;
			case Placement::Left:  x = anchorRect.x - gap - w;            break;
			case Placement::Over:  break;
			}

			// Flipped rather than pushed when it would fall off the edge it is growing
			// towards. A menu near the bottom of the window opening upwards is what every
			// reader expects; sliding it up so it covers its own button is not.
			if (placement == Placement::Below && y + h > viewport.y &&
			    anchorRect.y - gap - h >= 0.0f) {
				y = anchorRect.y - gap - h;
			} else if (placement == Placement::Above && y < 0.0f &&
			           anchorRect.y + anchorRect.h + gap + h <= viewport.y) {
				y = anchorRect.y + anchorRect.h + gap;
			} else if (placement == Placement::Right && x + w > viewport.x &&
			           anchorRect.x - gap - w >= 0.0f) {
				x = anchorRect.x - gap - w;
			} else if (placement == Placement::Left && x < 0.0f &&
			           anchorRect.x + anchorRect.w + gap + w <= viewport.x) {
				x = anchorRect.x + anchorRect.w + gap;
			}
		}

		// And kept on screen either way. A popup half outside the window is a popup with
		// half its options unreachable.
		x = std::clamp(x, 0.0f, (std::max)(0.0f, viewport.x - w));
		y = std::clamp(y, 0.0f, (std::max)(0.0f, viewport.y - h));
		mShown = Rect{ x, y, w, h };

		gui.beginPanel(mId.c_str(), mShown, variant);
		// Inside the padding, the way a button holds its children.
		paintChildren(gui, glm::vec2(mShown.x + padding, mShown.y + padding));
		gui.endPanel();

		// ---- and when it goes away ----
		//
		// After the children, so a press on one of them is not also a press outside.
		//
		// Whether the anchor counts as outside depends on who has the pointer. While this
		// is blocking, the anchor's own onClick cannot fire -- the walk ran without a
		// pointer -- so a press there is an ordinary outside press and closes the menu,
		// which is what clicking a menu's button a second time should do. When it is not
		// blocking, that same press *does* reach the button, and if this closed as well
		// the two would cancel: the menu would shut and the toggle would reopen it in the
		// same click. So it is excluded there, and the button's own handler is what
		// closes it.
		const GuiInput& in = gui.input();
		const bool anchorIsOutside = blocking;
		bool dismissed = false;
		if (in.pressed && !mShown.contains(in.pointer) &&
		    !(anchored && !anchorIsOutside && anchorRect.contains(in.pointer))) {
			dismissed = true;
		}
		// Escape, which is the other way out of anything on this screen -- the same
		// meaning it has in a text field.
		for (const GuiEditKey key : in.editKeys) {
			if (key == GuiEditKey::Escape) dismissed = true;
		}
		if (dismissed) {
			// `open` is not written here. It is a binding, and a widget that wrote its own
			// bound property would be overwritten by the signal on the next frame and
			// flicker. The layout closes itself, which is what onClose is for.
			if (onClose) onClose();
		}
	}

	void Panel::paint(Gui& gui, glm::vec2 origin) {
		Rect abs = placement(gui, origin);
		gui.beginPanel(mId.c_str(), abs, variant);
		// Children are positioned relative to the panel's top-left.
		paintChildren(gui, glm::vec2(abs.x, abs.y));
		gui.endPanel();
	}

	const std::vector<TextSpan>& Label::runs() const {
		if (mRunsFrom == spans && mRunsTextSize == text.size()) return mRuns;
		mRunsFrom = spans;
		mRunsTextSize = text.size();
		mRuns.clear();

		// "start:length:colour;start:length:colour" -- three fields, two separators, and
		// nothing that needs a tokenizer. Anything malformed is skipped rather than
		// refused: this arrives from a backend a character at a time while a model is
		// still writing, and half a triple should draw as plain text, not as nothing.
		const int size = static_cast<int>(text.size());
		size_t at = 0;
		while (at < spans.size()) {
			size_t semi = spans.find(';', at);
			if (semi == std::string::npos) semi = spans.size();
			const std::string_view one(spans.data() + at, semi - at);
			at = semi + 1;

			const size_t a = one.find(':');
			if (a == std::string_view::npos) continue;
			const size_t b = one.find(':', a + 1);
			if (b == std::string_view::npos) continue;

			TextSpan span;
			span.start  = std::atoi(std::string(one.substr(0, a)).c_str());
			span.length = std::atoi(std::string(one.substr(a + 1, b - a - 1)).c_str());
			if (!parseColor(std::string(one.substr(b + 1)).c_str(), span.color)) continue;

			// Clamped to the text it indexes. A span past the end is what a backend that
			// coloured one string and sent it with another looks like, and clipping it is
			// the only answer that does not read out of bounds.
			if (span.length <= 0) continue;
			if (span.start < 0) { span.length += span.start; span.start = 0; }
			if (span.start >= size || span.length <= 0) continue;
			span.length = (std::min)(span.length, size - span.start);
			mRuns.push_back(span);
		}

		// Sorted and made disjoint, which is what the draw below relies on. An overlap is
		// resolved in favour of whichever starts first -- a rule, rather than whatever
		// order the sender happened to write them in.
		std::sort(mRuns.begin(), mRuns.end(),
		          [](const TextSpan& l, const TextSpan& r) { return l.start < r.start; });
		int reach = 0;
		size_t kept = 0;
		for (TextSpan& span : mRuns) {
			if (span.start < reach) {
				span.length -= reach - span.start;
				span.start = reach;
				if (span.length <= 0) continue;
			}
			reach = span.start + span.length;
			mRuns[kept++] = span;
		}
		mRuns.resize(kept);
		return mRuns;
	}

	void Label::paint(Gui& gui, glm::vec2 origin) {
		// A named variant's color takes precedence; otherwise the per-instance `color`.
		const LabelStyle& style = gui.theme().label(variant);
		uint32_t c = (variant != kDefaultVariant) ? style.color : color;
		Rect abs = placement(gui, origin);
		const float line = gui.lineHeight(style.font);
		const std::vector<TextSpan>& coloured = runs();

		if (!wrap) {
			const float x = abs.x + alignOffset(hAlign, abs.w,
			                                    gui.measureText(text.c_str(), style.font), hAlignOffset);
			const glm::vec2 at(x, abs.y + alignOffset(vAlign, abs.h, line, vAlignOffset));
			if (coloured.empty()) {
				gui.label(text.c_str(), at, c, style.font);
			} else {
				gui.labelSpans(text.c_str(), 0, static_cast<int>(text.size()), at, c,
				               coloured.data(), coloured.size(), style.font);
			}
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
			if (coloured.empty()) {
				gui.label(one.c_str(), glm::vec2(x, y), c, style.font);
			} else {
				// The whole string with this line's slice named, rather than the copy
				// above: the spans are written against the string, so a run crossing a
				// line break is drawn in its colour on both of them without the caller
				// having to work out where the breaks fell.
				gui.labelSpans(text.c_str(), static_cast<int>(span.first),
				               static_cast<int>(span.second), glm::vec2(x, y), c,
				               coloured.data(), coloured.size(), style.font);
			}
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
	void Image::retireFrames() const {
		for (auto& frame : mFrames) {
			if (frame) rendeerRetireTexture(std::move(frame));
		}
		mFrames.clear();
		mPlayhead = 0.0f;
	}

	void Image::release() const {
		if (mTexture) rendeerRetireTexture(std::move(mTexture));
		mTexture.reset();
		retireFrames();
		// Borrowed, so there is nothing to free -- but it must stop pointing at it, or a
		// source that changed from a registered picture to a file would go on drawing the
		// picture.
		mShared = nullptr;
	}

	void Image::ensureLoaded() const {
		const std::string_view prefix(kMemoryImagePrefix);
		const bool registered = source.compare(0, prefix.size(), prefix) == 0;

		// A registered picture is re-asked for whenever the registry moves, because a
		// redefine replaces the texture behind the same name -- and may well reuse the
		// address, so comparing pointers would miss it.
		if (registered) {
			if (mLoaded == source && mSharedRevision == images().revision()) return;
			release();
			mLoaded = source;
			mSharedRevision = images().revision();
			mShared = images().find(source.substr(prefix.size()));
			if (!mShared && !mFailed) {
				mFailed = true;
				RDA_LOG_WARNING("Image '" << mId << "': nothing registered as '"
				                << source.substr(prefix.size())
				                << "'. Register it before the frame that shows it -- in "
				                   "Python rda.define_image(name, bytes).");
			} else if (mShared) {
				mFailed = false;
			}
			return;
		}

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

	bool Image::isDrawing() const {
		// By extension, which is how every other picture decides what it is. A drawing has
		// no size of its own until somebody says how big to draw it, which is the whole
		// reason it cannot go down the same path as a file.
		return source.size() > 4 &&
		       (source.compare(source.size() - 4, 4, ".svg") == 0 ||
		        source.compare(source.size() - 4, 4, ".SVG") == 0);
	}

	void Image::ensureDrawn(uint32_t width, uint32_t height) const {
		if (width == 0 || height == 0) return;
		// Read once. Changing size re-draws the shape; it does not re-read the file, which
		// is the point of keeping the parsed document.
		if (mLoaded != source || !mDrawing) {
			release();
			mLoaded = source;
			mFailed = false;
			mDrawnWidth = mDrawnHeight = 0;
			std::shared_ptr<Svg::Picture> parsed = Svg::load(source);
			if (!parsed) {
				mFailed = true;
				mDrawing.reset();
				return;
			}
			mDrawing = parsed;
		}
		if (mFailed || !mDrawing) return;
		const bool sized = mDrawnWidth == width && mDrawnHeight == height;
		if (sized && (mTexture || !mFrames.empty())) return;


		const Svg::Picture& picture = *static_cast<const Svg::Picture*>(mDrawing.get());
		mLoopSeconds = Svg::duration(picture);
		mDrawnWidth = width;
		mDrawnHeight = height;
		// Handed over rather than dropped. These are the frames the *previous* size was
		// drawn at, and the GUI is holding a descriptor set for each one that it looks up
		// by address -- so destroying them here frees a sampler a frame in flight is
		// still reading from, and the address is then handed straight back by the
		// allocator to one of the new frames below, which is what made the GUI find a set
		// describing an image that had been rebuilt underneath it.
		retireFrames();

		// A still icon is one picture. A moving one is its whole loop, rasterised now so
		// that playing it later is free -- see the note beside mFrames.
		//
		// Thirty a second rather than sixty: an icon is small and a loop is short, and
		// nobody has ever looked at a spinner and wanted more frames. Capped, because a
		// long loop at a large size is memory somebody did not ask for, and a slower icon
		// is better than a hitch.
		const int wanted = mLoopSeconds > 0.0f
			? std::clamp(static_cast<int>(std::ceil(mLoopSeconds * 30.0f)), 2, 120) : 1;

		std::vector<unsigned char> pixels;
		for (int i = 0; i < wanted; ++i) {
			const float at = wanted > 1
				? mLoopSeconds * static_cast<float>(i) / static_cast<float>(wanted) : 0.0f;
			if (!Svg::rasterise(picture, width, height, pixels, at)) {
				mFailed = true;
				return;
			}
			auto frame = std::make_unique<Texture>(
				Texture::fromPixels(pixels.data(), width, height));
			if (!frame->isValid()) {
				mFailed = true;
				retireFrames();
				RDA_LOG_WARNING("Image '" << mId << "': cannot make a " << width << "x"
				                << height << " surface for " << source);
				return;
			}
			mFrames.push_back(std::move(frame));
		}
		if (mTexture) rendeerRetireTexture(std::move(mTexture));
		mTexture.reset();
	}

	glm::vec2 Image::measureContent(Gui& gui, glm::vec2 available) const {
		(void)gui;
		// A drawing is whatever size it says it is, without drawing anything: asking how
		// big it wants to be must not need a texture, because the answer is what decides
		// how big the texture will be.
		if (isDrawing()) {
			if (mLoaded != source || !mDrawing) {
				mLoaded = source;
				mFailed = false;
				std::shared_ptr<Svg::Picture> parsed = Svg::load(source);
				if (!parsed) { mFailed = true; return { 0.0f, 0.0f }; }
				mDrawing = parsed;
			}
			float w = 0.0f, h = 0.0f;
			Svg::size(*static_cast<const Svg::Picture*>(mDrawing.get()), w, h);
			const glm::vec2 own(w, h);
			if (available.x <= 0.0f || own.x <= available.x) return own;
			return { available.x, own.y * (available.x / own.x) };
		}
		ensureLoaded();
		const Texture* picture = active();
		if (!picture) return { 0.0f, 0.0f };
		const glm::vec2 own(static_cast<float>(picture->extent().width),
		                    static_cast<float>(picture->extent().height));
		// A picture bigger than the room it is in asks for the room, keeping its shape --
		// otherwise width="content" on a photograph asks for four thousand pixels.
		if (available.x <= 0.0f || own.x <= available.x) return own;
		return { available.x, own.y * (available.x / own.x) };
	}

	void Image::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		if (isDrawing()) {
			// Drawn at the box, not at some size it was exported at. `contain` is already
			// what the rasteriser does with the shape inside the box it is given, so both
			// fits come out of one call and there is no second scaling afterwards to go
			// soft.
			ensureDrawn(static_cast<uint32_t>((std::max)(1.0f, std::round(box.w))),
			            static_cast<uint32_t>((std::max)(1.0f, std::round(box.h))));
			if (mFrames.empty()) return;
			size_t frame = 0;
			if (mFrames.size() > 1 && mLoopSeconds > 0.0f) {
				mPlayhead = std::fmod(mPlayhead + gui.input().dt, mLoopSeconds);
				frame = (std::min)(mFrames.size() - 1,
				                   static_cast<size_t>(mPlayhead / mLoopSeconds *
				                                       static_cast<float>(mFrames.size())));
				// It will look different next frame, and nothing else on this window can
				// tell -- the tree has not changed and neither has the input.
				gui.keepAwake();
			}
			gui.image(box, mFrames[frame].get(), tint);
			return;
		}
		ensureLoaded();
		const Texture* picture = active();
		if (!picture) return;
		if (fit == Fit::Stretch) { gui.image(box, picture, tint); return; }

		// Contain: the largest rectangle of the picture's shape that fits, centred. Done
		// here rather than in a shader, which keeps the quad the thing that is positioned
		// -- the same as everything else in this file.
		const float ow = static_cast<float>(picture->extent().width);
		const float oh = static_cast<float>(picture->extent().height);
		if (ow <= 0.0f || oh <= 0.0f || box.w <= 0.0f || box.h <= 0.0f) return;
		const float scale = (std::min)(box.w / ow, box.h / oh);
		const float w = ow * scale;
		const float h = oh * scale;
		gui.image({ box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h }, picture,
		          tint);
	}

	// ---- Stream ----------------------------------------------------------------------
	glm::vec2 Stream::measureContent(Gui& gui, glm::vec2 available) const {
		(void)gui;
		const Texture* frame = name.empty() ? nullptr : streams().find(name);
		if (!frame) return { 0.0f, 0.0f };
		const glm::vec2 own(static_cast<float>(frame->extent().width),
		                    static_cast<float>(frame->extent().height));
		// A frame bigger than the room it is in asks for the room, keeping its shape --
		// the same answer <image> gives, and for the same reason.
		if (available.x <= 0.0f || own.x <= available.x) return own;
		return { available.x, own.y * (available.x / own.x) };
	}

	void Stream::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		if (name.empty()) return;

		// Said whether or not there is a frame yet: this is what tells a producer anybody
		// is looking, and a stream with no frame is exactly the one waiting for its first.
		streams().markSeen(name);

		const Texture* frame = streams().find(name);
		if (!frame) return;    // nothing pushed yet; the box stays empty rather than black

		if (fit == Fit::Stretch) { gui.image(box, frame); return; }
		const float ow = static_cast<float>(frame->extent().width);
		const float oh = static_cast<float>(frame->extent().height);
		if (ow <= 0.0f || oh <= 0.0f || box.w <= 0.0f || box.h <= 0.0f) return;
		const float scale = (std::min)(box.w / ow, box.h / oh);
		const float w = ow * scale;
		const float h = oh * scale;
		gui.image({ box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h }, frame);
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
	size_t Select::choiceCount() const {
		if (!of.empty()) {
			const uint32_t id = tables().find(of);
			return (id == kNoTable) ? 0 : tables().at(id).rows();
		}
		size_t count = 0;
		for (const auto& child : children()) {
			const Option* one = dynamic_cast<const Option*>(child.get());
			if (one && one->visible) ++count;
		}
		return count;
	}

	bool Select::choiceAt(size_t index, std::string_view& text, std::string_view& value) const {
		if (!of.empty()) {
			const uint32_t id = tables().find(of);
			if (id == kNoTable) return false;
			Table& table = tables().at(id);
			if (index >= table.rows()) return false;
			const uint32_t textCol = table.column(textColumn);
			const uint32_t valueCol = table.column(valueColumn);
			// A column the table does not have reads as empty rather than refusing: a
			// select whose value column is missing still shows its text, which is enough
			// to see what went wrong.
			text  = (textCol  == kNoColumn) ? std::string_view{} : table.text(index, textCol);
			value = (valueCol == kNoColumn) ? std::string_view{} : table.text(index, valueCol);
			return true;
		}
		size_t at = 0;
		for (const auto& child : children()) {
			const Option* one = dynamic_cast<const Option*>(child.get());
			if (!one || !one->visible) continue;
			if (at++ != index) continue;
			text = one->text;
			value = one->value;
			return true;
		}
		return false;
	}

	glm::vec2 Select::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		// Wide enough for the longest choice, so opening the list does not change what
		// the closed box looks like.
		float widest = gui.measureText(placeholder.c_str());
		const size_t choices = choiceCount();
		for (size_t i = 0; i < choices; ++i) {
			std::string_view text, ignored;
			if (!choiceAt(i, text, ignored)) continue;
			widest = (std::max)(widest, gui.measureText(std::string(text).c_str()));
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
		// Through the accessors, not by counting <option> children: the choices may come
		// from a table now, and counting children gave one of `of=` and none of the other
		// -- a dropdown filled from data that the keyboard could open and not move in.
		const int options = static_cast<int>(choiceCount());
		if (gui.focusActivated(wid)) {
			if (!mOpen) {
				mOpen = true;
				// Opened from the keyboard, it starts on whatever is already chosen, so
				// the first arrow moves from there rather than from the top.
				mHighlight = 0;
				for (int at = 0; at < options; ++at) {
					std::string_view text, oneValue;
					if (!choiceAt(static_cast<size_t>(at), text, oneValue)) continue;
					if (oneValue == value) { mHighlight = at; break; }
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
		std::string shown = placeholder;
		const size_t choices = choiceCount();
		const int count = static_cast<int>(choices);
		for (size_t i = 0; i < choices; ++i) {
			std::string_view text, oneValue;
			if (!choiceAt(i, text, oneValue)) continue;
			if (oneValue == value) { shown.assign(text); break; }
		}
		const float textY = box.y + (box.h - gui.lineHeight(s.font)) * 0.5f;
		gui.drawText(shown.c_str(), { box.x + 10.0f, textY }, s.text, s.font);

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
		std::string_view text, chosen;
		if (!choiceAt(static_cast<size_t>(mHighlight), text, chosen)) return;
		if (value != chosen) {
			value.assign(chosen);
			if (onChange) onChange(value);
		}
	}

	void Select::paintAbove(Gui& gui, glm::vec2 origin) {
		(void)origin;
		const ButtonStyle& s = gui.theme().button(variant);
		const float rowHeight = std::round(gui.lineHeight(s.font) + 8.0f);
		const size_t count = choiceCount();
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
		for (size_t index = 0; index < count; ++index) {
			std::string_view oneText, oneValue;
			if (!choiceAt(index, oneText, oneValue)) continue;
			const Rect row{ list.x, y, list.w, rowHeight };
			// The keyboard's row looks exactly like the pointer's: there is one
			// highlight in a list, however the reader is moving it.
			const bool over = row.contains(gui.input().pointer) ||
			                  (static_cast<int>(index) == mHighlight);
			// Faded to nothing rather than not drawn, so a pointer running down the list
			// leaves each row on its way out instead of switching them on and off. Only
			// the alpha moves: the two ends are the same colour.
			//
			// Keyed by position rather than by the option widget, because a table-backed
			// select has no widget per row -- and a row is the same row whichever it
			// came from.
			gui.drawRectRounded(row, gui.motion().colour(
				gui.motionKey(mId.c_str(), kMotionFill ^ static_cast<uint32_t>(index * 2654435761u)),
				over ? s.hovered : fadeTo(s.hovered, 0.0f),
				s.motion.seconds, s.motion.curve), 0.0f);
			gui.drawText(std::string(oneText).c_str(),
			             { row.x + 10.0f, row.y + (row.h - gui.lineHeight(s.font)) * 0.5f },
			             s.text, s.font);
			// Chosen on release rather than press: the press is what opened the list, and
			// acting on it would pick whatever happened to be under the pointer then.
			if (over && gui.input().released) {
				mOpen = false;
				if (value != oneValue) {
					value.assign(oneValue);
					if (onChange) onChange(value);
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
		Gui::TextFieldExtras extras;
		int  caretNow = mLastCaret;
		bool accepted = false;
		bool dismissed = false;
		bool submitted = false;
		extras.suggestion = mSuggestion.empty() ? nullptr : mSuggestion.c_str();
		extras.language = language.empty() ? nullptr : language.c_str();
		extras.submit = submitKey;
		extras.caret = &caretNow;
		extras.accepted = &accepted;
		extras.dismissed = &dismissed;
		extras.submitted = &submitted;

		if (gui.textField(mId.c_str(), mText, mPlaceholder, abs, used, &focused,
		                  mTextVersion, &extras)) {
			// The field edited the string in place, so nothing else can have noticed.
			++mTextVersion;
			if (onChange) onChange(mText);
		}

		// Taken or dropped, the offer is spent. Cleared here rather than left to the
		// application, so a suggestion cannot survive the keystroke that answered it and
		// be offered again on the next frame.
		if (accepted) {
			mSuggestion.clear();
			if (onAccept) onAccept();
		} else if (dismissed) {
			mSuggestion.clear();
			if (onDismiss) onDismiss();
		}
		// After onChange, so a handler that reads the text sees what was just typed
		// rather than what was there before the last keystroke.
		if (submitted && onSubmit) onSubmit();

		// Only on a move. This runs every frame the field paints, and a handler that fired
		// sixty times a second while nothing happened would be asked to debounce something
		// the engine already knows the answer to.
		if (caretNow != mLastCaret) {
			mLastCaret = caretNow;
			if (onCaret) onCaret(static_cast<float>(caretNow));
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
