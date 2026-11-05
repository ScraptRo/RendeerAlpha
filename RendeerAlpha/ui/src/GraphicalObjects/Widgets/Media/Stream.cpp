#include <GraphicalObjects/Widgets/Media/Stream.h>
#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>

namespace RDA {

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
}
