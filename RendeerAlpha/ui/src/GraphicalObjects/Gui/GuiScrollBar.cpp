#include <GraphicalObjects/Gui.h>

// Gui, scroll bars: what one looks like this frame, on its way to where it is going.
namespace RDA {

	Gui::ScrollBarLook Gui::scrollBar(uint32_t key, bool needed, bool hot,
	                                  float thumbSpan, float trackSpan,
	                                  const TextFieldStyle& style) {
		ScrollBarLook look;
		look.presence = needed ? 1.0f : 0.0f;
		// A bar nobody needs is one whose handle fills it. Content shrinking to fit ends
		// with the handle as long as the track, which is what "all of it is showing"
		// looks like, and the bar fades from there -- rather than the handle collapsing
		// to nothing on its way out, which reads as a fault.
		look.thumb = needed ? thumbSpan : trackSpan;
		look.colour = hot ? style.scrollThumbHover : style.scrollThumb;

		const float seconds = style.motion.seconds;
		if (seconds <= 0.0f) return look; // no duration asked for, and none stored
		const Easing curve = style.motion.curve;
		look.presence = mMotion.value(key + 0u, look.presence, seconds, curve);
		look.thumb    = mMotion.value(key + 1u, look.thumb, seconds, curve);
		look.colour   = mMotion.colour(key + 2u, look.colour, seconds, curve);
		return look;
	}
}
