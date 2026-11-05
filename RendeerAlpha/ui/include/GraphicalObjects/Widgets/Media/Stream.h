#pragma once
#include <GraphicalObjects/Widgets/WidgetBase.h>

namespace RDA {

	// A picture that keeps arriving: a camera, a decoded video, a model's output as it is
	// produced, the result of a filter that runs every frame.
	//
	// The difference from `<image src="mem:...">` is what happens on the second frame.
	// An image is registered, which builds a texture; a stream is pushed, which writes
	// into the one already there. At thirty frames a second that is the difference
	// between a feed and a stall -- see Streams.h.
	//
	// It also reports that it is being looked at. A producer can ask whether anybody is
	// watching before it decodes the next frame, which is what stops a feed on a screen
	// nobody is showing from costing anything.
	class Stream : public Widget {
	public:
		enum class Fit : uint8_t {
			Contain, // the whole frame inside the box, aspect kept, centred
			Stretch, // fill the box, aspect ignored
		};
		explicit Stream(std::string id, std::string name = {})
			: Widget(std::move(id)), name(std::move(name)) {}

		// Which stream to show. Bindable, so one widget can follow whichever feed is
		// selected.
		std::string name;
		Fit         fit = Fit::Contain;

		// The frame's own size in pixels, so width="content" means what it looks like.
		glm::vec2 measureContent(Gui& gui, glm::vec2 available) const override;
		void paint(Gui& gui, glm::vec2 origin) override;
	};
}
