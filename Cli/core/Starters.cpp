#include "Starters.h"

#include <iterator>

namespace Cli {

	namespace {
		// A container is given a height because an empty one has none, and something with no
		// height cannot be dropped into. It is editable because the only reason to drop one in
		// is to put something in it.
		constexpr Starter kStarters[] = {
			{ "stack",  "<stack id=\"$id\" arrange=\"vertical\" spacing={8} height={120} editable />", 200.0f, 120.0f },
			{ "panel",  "<panel id=\"$id\" height={120} editable />", 200.0f, 120.0f },
			{ "scroll", "<scroll id=\"$id\" height={120} editable />", 200.0f, 120.0f },
			{ "label",  "<label id=\"$id\" text=\"Label\" />", 120.0f, 22.0f },
			{ "button", "<button id=\"$id\" text=\"Button\" />", 120.0f, 30.0f },
			{ "checkbox", "<checkbox id=\"$id\" label=\"Checkbox\" />", 140.0f, 24.0f },
			{ "slider", "<slider id=\"$id\" value={50} min={0} max={100} />", 160.0f, 24.0f },
			{ "number", "<number id=\"$id\" label=\"Number\" value={1} />", 140.0f, 28.0f },
			{ "textfield", "<textfield id=\"$id\" placeholder=\"Type here\" />", 180.0f, 30.0f },
			{ "select",
			  "<select id=\"$id\" placeholder=\"Choose\">\n"
			  "  <option id=\"$id-first\" text=\"First\" value=\"first\" />\n"
			  "  <option id=\"$id-second\" text=\"Second\" value=\"second\" />\n"
			  "</select>", 160.0f, 30.0f },
			{ "image",  "<image id=\"$id\" src=\"res/picture.png\" />", 120.0f, 120.0f },
		};

		void replaceAll(std::string& text, const std::string& what, const std::string& with) {
			for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) {
				text.replace(at, what.size(), with);
			}
		}
	}

	const Starter* starters(size_t& count) {
		count = std::size(kStarters);
		return kStarters;
	}

	const Starter* starterFor(std::string_view type) {
		for (const Starter& one : kStarters) {
			if (type == one.type) return &one;
		}
		return nullptr;
	}

	std::string starterSource(const Starter& starter, const std::string& id, const std::string& placement) {
		std::string source = starter.source;
		if (!placement.empty()) {
			const std::string first = "id=\"$id\"";
			const size_t at = source.find(first);
			if (at != std::string::npos) source.insert(at + first.size(), " " + placement);
		}
		replaceAll(source, "$id", id);
		return source;
	}
}
