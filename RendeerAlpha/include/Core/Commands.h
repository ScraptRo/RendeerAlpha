#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Things the interface can ask the application to do.
//
// Signals carry values across the boundary; this carries intent. A button that saves a
// file cannot express "save the file" as an expression over state, and it should not have
// to: the work belongs in C++, and what the layout says is which work to ask for.
//
// The alternative was letting a handler call any function by name, which would have made
// a misspelling a runtime shrug. So a command is declared where state is declared, both
// sides are generated from that one declaration, and `commands.save()` in a layout either
// names something the application registered or fails to compile.
//
// Deliberately without arguments or a return value. State is the channel for data: an
// interface writes what it wants known and then asks for the work, and the backend reads
// the state it was given. Adding parameters would mean marshalling a second kind of value
// across the same boundary that already carries values perfectly well.
namespace RDA {

	inline constexpr uint32_t kNoCommand = 0xFFFFFFFFu;

	class Commands {
	public:
		// Declares a command, or returns the existing one. Generated code calls this;
		// declaring the name and supplying the work are separate steps because the
		// layout is resolved against the names, not against what they do.
		uint32_t define(std::string_view name);
		uint32_t find(std::string_view name) const;

		// Supplies the work. Replacing an existing one is allowed and is what a
		// reconfiguring application does.
		void bind(uint32_t command, std::function<void()> work);

		// Runs it. False means the name exists but nothing was bound to it, which is a
		// mistake worth reporting rather than a silent nothing.
		bool invoke(uint32_t command);


	private:
		std::vector<std::string>                    mNames;
		std::vector<std::function<void()>>          mWork;
		std::unordered_map<std::string, uint32_t>   mByName;
	};

	// The one registry, for the same reason signals have one: a layout is resolved
	// against names, and two registries would mean two answers for one name.
	Commands& commands();
}
