#include <GraphicalObjects/Widgets/Containers/Scroll.h>
#include "../WidgetInternal.h"
#include <GraphicalObjects/Gui.h>
#include <algorithm>

namespace RDA {

	using detail::mainAxisRequest;
	using detail::crossAxisRequest;

	// ---- Scroll --------------------------------------------------------------------
	glm::vec2 Scroll::measureContent(Gui& gui, glm::vec2 available) const {
		// A scroll view takes the space it is offered. Reporting what it contains would
		// defeat the point — the contents are exactly what is allowed to be too big.
		(void)gui;
		return available;
	}

	void Scroll::paint(Gui& gui, glm::vec2 origin) {
		const Rect view = placement(gui, origin);
		// A starting value. Refined below once the content is measured, because a bar
		// along the bottom takes a strip out of what can be seen.
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
			// Out of flow is out of the scroll range too, or a menu would add its own
			// height to how far the view can be scrolled.
			if (!child->inFlow()) { if (heights) heights[i] = 0.0f; continue; }
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

		// Sideways, the room is whatever the widest child needs past the view.
		const float maxOffX = horizontal
			? (std::max)(0.0f, mContentWidth - (view.w - barWidth)) : 0.0f;

		// And the bar along the bottom takes its own strip, the way the one down the side
		// does -- so what can be seen down the view is its height less that strip.
		//
		// This is what the `horizontalBar` written here was for, and it was written and
		// never used. Without it the vertical range is computed as though the bar were not
		// there, and the bar then sits over the last few units of content the range stops
		// short of: in a view that scrolls both ways, the bottom of the content could not
		// be reached at all. Taken only when the bar is really there, which is the same
		// test that draws it -- a view that cannot scroll sideways loses nothing.
		const float horizontalBar = (maxOffX > 0.0f) ? barWidth : 0.0f;
		mViewHeight = view.h - horizontalBar;

		const float maxOff = maxOffset();
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
			if (!mDraggingThumb && in.lPressed && track.contains(in.pointer)) {
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
		if (in.lReleased) mDraggingThumb = false;
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
			// Painted at the top of the view and stepped over, cursor and all: a widget
			// that is not in the flow does not take a row's worth of spacing either.
			if (!child->inFlow()) {
				child->paint(gui, glm::vec2(view.x + padding, view.y + padding));
				continue;
			}
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
			if (!mDraggingThumbX && in.lPressed && hTrack.contains(in.pointer)) {
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
		if (in.lReleased) mDraggingThumbX = false;

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
}
