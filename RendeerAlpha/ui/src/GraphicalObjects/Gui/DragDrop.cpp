#include <GraphicalObjects/Gui/DragDrop.h>
#include <GraphicalObjects/Gui/Canvas.h>
#include <algorithm>

// DragDrop: what a press picks up, what offers itself as a place to drop, and the figure drawn
// while something is carried.
namespace RDA {

	void DragDrop::notePickUp(Widget* widget, const Rect& where) {
		// Overwritten as the walk goes deeper, so the last one is the deepest.
		mPickUp = widget;
		mPickUpRect = where;
	}

	void DragDrop::offerTarget(Widget* widget, const Rect& where, const GuiInput& input,
	                           const Rect& clip) {
		// Not from a subtree that has been shown no pointer -- a screen on its way out, or
		// everything behind an open menu. Both hide it by parking it far outside anything,
		// which is the one thing a target can ask about without being told why.
		if (input.pointer.x < -1.0e5f) return;
		// Only the part of it that can be seen. A row scrolled half out of a list is a
		// target where it shows and not over the header that hides the rest.
		const float x0 = (std::max)(where.x, clip.x), y0 = (std::max)(where.y, clip.y);
		const float x1 = (std::min)(where.x + where.w, clip.x + clip.w);
		const float y1 = (std::min)(where.y + where.h, clip.y + clip.h);
		if (x1 <= x0 || y1 <= y0) return;
		mDropTargets.emplace_back(widget, Rect{ x0, y0, x1 - x0, y1 - y0 });
	}

	void DragDrop::finishDrag(bool dropped, bool& awake) {
		(void)dropped;
		// Every target that is still drawn hears that nothing is over it any more.
		for (const auto& one : mDropTargets) one.first->setDragOver(false);
		mDrag = DragState{};
		awake = true;
	}

	void DragDrop::drawFigure(const Rect& target, bool haveTarget, const GuiInput& input,
	                          Canvas& canvas, const Theme& theme) {
		const FocusStyle& ring = theme.focus(kDefaultVariant);
		const uint32_t edge = ring.color;
		const float t = (std::max)(1.0f, ring.width);
		auto outline = [&](const Rect& r) {
			canvas.addRect({ r.x, r.y, r.w, t }, edge);
			canvas.addRect({ r.x, r.y + r.h - t, r.w, t }, edge);
			canvas.addRect({ r.x, r.y + t, t, (std::max)(0.0f, r.h - t * 2.0f) }, edge);
			canvas.addRect({ r.x + r.w - t, r.y + t, t, (std::max)(0.0f, r.h - t * 2.0f) }, edge);
		};

		// Where it would land, in the focus ring's colour: the one colour every theme
		// already has for "this is the one", and a drop target is exactly that.
		if (haveTarget) {
			canvas.addRect(target, fadeTo(edge, 0.12f));
			outline(target);
		}

		// What is being carried, held where it was taken hold of -- a figure the size of
		// what was picked up, with its words on it, rather than the widget itself drawn a
		// second time. A widget painted twice in a frame asks for its animated values
		// twice from two places, and the one left behind would start sliding after the
		// one in the hand.
		const Rect figure{ input.pointer.x - mDrag.grab.x, input.pointer.y - mDrag.grab.y,
		                   mDrag.size.x, mDrag.size.y };
		canvas.addRect(figure, fadeTo(edge, 0.22f));
		outline(figure);
		if (!mDrag.label.empty() && canvas.font()) {
			const LabelStyle& text = theme.label(kDefaultVariant);
			canvas.pushClip(glm::vec4(figure.x, figure.y, figure.x + figure.w, figure.y + figure.h));
			const float y = figure.y + (figure.h - canvas.lineHeight(text.font)) * 0.5f;
			canvas.drawText(mDrag.label.c_str(), { figure.x + 8.0f, y }, text.color, text.font);
			canvas.popClip();
		}
	}

	void DragDrop::resetFrame() {
		mPickUp = nullptr;
		mDropTargets.clear();
		mFileTarget = nullptr;
	}

	bool DragDrop::deliverFiles(const GuiInput& input) {
		if (!input.droppedFiles.empty() && mFileTarget && mFileTarget->onFiles) {
			std::string paths;
			for (const std::string& one : input.droppedFiles) {
				if (!paths.empty()) paths += '\n';
				paths += one;
			}
			mFileTarget->onFiles(paths);
			return true;
		}
		return false;
	}

	bool DragDrop::update(const GuiInput& input, Interaction& interaction, Canvas& canvas,
	                      const Theme& theme) {
		bool awake = false;
		// ---- a drag ------------------------------------------------------------------
		//
		// Picked up on the press, but nothing travels until the pointer has gone a few
		// units with the button held: a press that stays put is a click, and a click on
		// a draggable card has to go on being one.
		if (mPickUp && input.lPressed && !mDrag.active) {
			// Unless something took the press as a drag of its own. A button's press is a
			// click waiting to happen, which a drag may cancel; a slider's is already
			// moving the slider.
			if (interaction.active == 0 || interaction.active == interaction.clickPress) {
				mDrag = DragState{};
				mDrag.pending = true;
				mDrag.payload = mPickUp->drag;
				mDrag.kind = mPickUp->dragKind;
				mDrag.label = mPickUp->dragLabel();
				mDrag.size = { mPickUpRect.w, mPickUpRect.h };
				mDrag.grab = input.pointer - glm::vec2(mPickUpRect.x, mPickUpRect.y);
				mDrag.from = input.pointer;
			}
		}
		if (mDrag.pending) {
			constexpr float kDragSlop = 4.0f; // units the pointer may wander and still click
			const glm::vec2 moved = input.pointer - mDrag.from;
			if (!input.lDown) {
				mDrag = DragState{};
			} else if (moved.x * moved.x + moved.y * moved.y > kDragSlop * kDragSlop) {
				mDrag.pending = false;
				mDrag.active = true;
				// The press is the drag's now. Whatever it landed on does not also click
				// when the button comes up -- the card goes where it was carried and
				// stays unopened.
				interaction.active = 0;
			}
		}
		if (mDrag.active) {
			Widget* target = nullptr;
			Rect targetRect{};
			for (auto it = mDropTargets.rbegin(); it != mDropTargets.rend(); ++it) {
				if (it->second.contains(input.pointer)) {
					target = it->first;
					targetRect = it->second;
					break;
				}
			}
			for (const auto& one : mDropTargets) one.first->setDragOver(one.first == target);

			bool cancelled = false;
			for (const GuiEditKey key : input.editKeys) {
				if (key == GuiEditKey::Escape) cancelled = true;
			}
			if (cancelled) {
				finishDrag(false, awake);
			} else if (!input.lDown) {
				// Copied before the handler runs: it may write state that rebinds the very
				// widget that offered itself, and finishDrag clears what is carried.
				const std::string payload = mDrag.payload;
				const bool landed = target && target->onDrop;
				finishDrag(landed, awake);
				if (landed) target->onDrop(payload);
			} else {
				drawFigure(targetRect, target != nullptr, input, canvas, theme);
				awake = true;
			}
		}
		return awake;
	}
}
