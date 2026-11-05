#include "WidgetInternal.h"

namespace RDA::detail {

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

	// A number a screen reader sent, or false for one that is not.
	bool readNumber(const std::string& text, double& out) {
		try {
			size_t used = 0;
			out = std::stod(text, &used);
			return used > 0;
		} catch (...) {
			return false;
		}
	}
	const std::string& either(const std::string& first, const std::string& second) {
		return first.empty() ? second : first;
	}
}
