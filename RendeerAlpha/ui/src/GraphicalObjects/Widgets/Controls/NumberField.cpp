#include <GraphicalObjects/Widgets/Controls/NumberField.h>
#include "../WidgetInternal.h"
#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cmath>

namespace RDA {

	using detail::either;
	using detail::readNumber;

	// ---- NumberField ---------------------------------------------------------------
	namespace {
		// The value, at the places asked for, with trailing zeros kept -- a column of
		// numbers that changed width as they were dragged would be unreadable.
		std::string numberText(double value, int decimals) {
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.*f",
			              std::clamp(decimals, 0, 9), value);
			return buffer;
		}
	}

	glm::vec2 NumberField::measureContent(Gui& gui, glm::vec2 available) const {
		(void)available;
		const TextFieldStyle& s = (variant != kDefaultVariant)
			? gui.theme().textField(variant) : TextFieldStyle::forMode(TextFieldMode::Line);
		const float text = gui.measureText(label.c_str()) +
		                   gui.measureText(numberText(value, decimals).c_str());
		// Room for both, the padding either side, and a gap between them that does not
		// close up when the number grows a digit.
		return { text + s.padding * 4.0f + 16.0f, gui.lineHeight() + s.padding * 2.0f };
	}

	void NumberField::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		const TextFieldStyle& base = (variant != kDefaultVariant)
			? gui.theme().textField(variant) : TextFieldStyle::forMode(TextFieldMode::Line);
		const GuiInput& in = gui.input();

		// --- typing ---------------------------------------------------------------
		//
		// A real text field, because it has to be: selection, the caret, undo and every
		// editing key are already there and a second half-implementation of them would
		// be worse at all of it.
		if (mTyping) {
			Gui::TextFieldExtras extras;
			bool submitted = false;
			extras.submitted = &submitted;
			extras.submit = SubmitKey::Enter;
			gui.textField(mId.c_str(), mBuffer, "", box, base, &mFocused,
			              mBufferVersion, &extras);
			// Finished with, either way: Enter said so, and losing the keyboard says the
			// same thing -- clicking elsewhere commits what was typed, which is what
			// every numeric field anywhere does.
			if (submitted || !mFocused) {
				mTyping = false;
				try {
					const double typed = std::stod(mBuffer);
					const double was = value;
					value = (minValue < maxValue) ? std::clamp(typed, minValue, maxValue) : typed;
					if (value != was && onChange) onChange(value);
				} catch (const std::exception&) {
					// Not a number. The value it had is the value it keeps.
				}
			}
			return;
		}

		// --- dragging --------------------------------------------------------------
		const uint32_t wid = gui.motionKey(mId.c_str(), 0u);
		const bool over = box.contains(in.pointer);

		if (mDragging) {
			const float travelled = in.pointer.x - mDragStart.x;
			// Shift is the fine adjustment, the way it is in every tool that has this.
			const double scale = in.shift ? 0.1 : 1.0;
			const double was = value;
			double next = mDragFrom + static_cast<double>(travelled) * step * scale;
			if (minValue < maxValue) next = std::clamp(next, minValue, maxValue);
			if (next != was) {
				value = next;
				if (onChange) onChange(value);
			}
			if (in.lReleased) {
				mDragging = false;
				// A press that went nowhere was a click, and a click means type.
				if (std::abs(in.pointer.x - mDragStart.x) < 3.0f &&
				    std::abs(in.pointer.y - mDragStart.y) < 3.0f) {
					mTyping = true;
					mBuffer = numberText(value, decimals);
					++mBufferVersion;
					gui.setFocus(gui.widgetId(mId.c_str()));
					mFocused = true;
				}
			}
		} else if (over && in.lPressed) {
			mDragging = true;
			mDragFrom = value;
			mDragStart = in.pointer;
		}

		// --- drawing ---------------------------------------------------------------
		const uint32_t fill = gui.motion().colour(
			wid ^ kMotionFill,
			(mDragging || over) ? base.selection : base.background,
			base.motion.seconds, base.motion.curve);
		if (base.radius > 0.0f) gui.drawRectRounded(box, fill, base.radius);
		else                    gui.drawRect(box, fill);
		// The border is four edges rather than a frame call: addFrame lays a fill over
		// its border, and this one has a fill of its own already.

		const std::string shown = numberText(value, decimals);
		const float top = box.y + (box.h - gui.lineHeight()) * 0.5f;
		if (!label.empty()) {
			gui.drawText(label.c_str(), { box.x + base.padding, top }, base.placeholder);
		}
		// The number against the right edge, so a column of them lines up on the point
		// rather than on however long each label happens to be.
		const float width = gui.measureText(shown.c_str());
		gui.drawText(shown.c_str(), { box.x + box.w - base.padding - width, top }, base.text);
	}

	bool NumberField::describe(AccessibleInfo& out) const {
		out.role = AccessibleRole::Edit;
		out.name = either(alt, label);
		out.value = numberText(value, decimals);
		out.focusable = true;
		out.actions = kCanSetValue;
		if (maxValue != minValue) {
			out.hasRange = true;
			out.rangeMin = minValue;
			out.rangeMax = maxValue;
			out.rangeValue = value;
			out.rangeStep = step;
		}
		return true;
	}
	void NumberField::accessibleAction(AccessibleAction action, const std::string& text) {
		double wanted = 0.0;
		if (action != AccessibleAction::SetValue || !readNumber(text, wanted)) return;
		if (maxValue != minValue) wanted = std::clamp(wanted, (std::min)(minValue, maxValue),
		                                              (std::max)(minValue, maxValue));
		if (wanted == value) return;
		value = wanted;
		if (onChange) onChange(value);
	}
}
