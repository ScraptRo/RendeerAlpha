#include <GraphicalObjects/Widgets/Containers/Stack.h>
#include "../WidgetInternal.h"
#include <Core/Route.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>

namespace RDA {

	using detail::mainAxisRequest;
	using detail::crossAxisRequest;

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

	glm::vec2 Stack::measureContent(Gui& gui, glm::vec2 available) const {
		float main = padding * 2.0f;
		float cross = 0.0f;
		int counted = 0;
		for (const auto& child : mChildren) {
			if (!child) continue;
			// A popup asks for nothing here: see Widget::inFlow.
			if (!child->inFlow()) continue;
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
			if (!child->inFlow()) continue;
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
			// Painted where the stack's own content begins and then stepped over: it
			// places itself against its anchor, and the cursor must not move for it or
			// everything after it would be pushed down by a widget that is not there.
			if (!child->inFlow()) {
				if (child->visible) child->paint(gui, glm::vec2(area.x + padding, area.y + padding));
				continue;
			}
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
			if (!slot.overlaps(gui.currentClipRect())) {
				// Not drawn, but still there: a Save button scrolled out of sight is still
				// the Save button, and Ctrl+S should not depend on where the scroll bar is.
				// Not while collapsing, which is on its way to not being there at all.
				if (!collapsing) gui.offerShortcutsIn(*child);
			} else {
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
}
