// A widget as a few lines of a layout: what the editor drops in, and what `rda widgets` shows.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Cli {

	struct Starter {
		const char* type;
		// The source, with $id wherever an id goes. May be several lines.
		const char* source;
		// How big it is dropped where nothing arranges it: into a <panel> or a <container>,
		// where a widget with no size would fill its parent.
		float       width;
		float       height;
	};

	// In the order the editor lists them. Not every widget has one: an <option> or a <dock> means
	// nothing on its own, and a <list> needs a table the project has to declare first.
	const Starter* starters(size_t& count);
	const Starter* starterFor(std::string_view type);

	// The source with its id filled in. `placement` goes straight after the first id, for what
	// is being put somewhere by hand.
	std::string starterSource(const Starter& starter, const std::string& id,
	                          const std::string& placement = {});
}
