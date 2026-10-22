#pragma once
#include <cstdint>
#include <functional>
#include <memory>
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
// A command carries its arguments. It used not to -- state was the channel, written first
// and read by the handler -- and that was two steps for one intent, a signal declared only
// to carry a value across, and a race in a list: two clicks on two rows before the backend
// looked, and it read the second row twice. What an event means now travels with it, as
// one JSON array, because every language a backend is written in reads that natively.
// Signals stay what the interface shows; an argument is what one event meant.
//
// A command may also be answered by state.ts itself -- an action -- in which case a backend
// that binds it as well hears of it after the action has run. Whoever calls a command cannot
// tell which kind it named, which is what lets the work move from one side to the other
// without a layout changing.
namespace RDA {

	inline constexpr uint32_t kNoCommand = 0xFFFFFFFFu;

	// The arguments one call was made with: a JSON array, `[]` for none. Read by position,
	// with a fallback for one that is missing or is the wrong kind -- a handler written
	// against a declaration should not crash because a caller passed one argument short.
	class CommandArgs {
	public:
		CommandArgs() = default;
		// An array already written. Not checked here: the C ABI checks what it is handed,
		// and everything else builds one with the add* calls below.
		explicit CommandArgs(std::string json) : mJson(std::move(json)) {}

		const std::string& json() const { return mJson; }
		size_t size() const;
		bool   empty() const { return size() == 0; }

		double      number(size_t index, double fallback = 0.0) const;
		bool        boolean(size_t index, bool fallback = false) const;
		std::string text(size_t index, std::string_view fallback = {}) const;
		// One argument's own JSON, for one that is an array or an object.
		std::string raw(size_t index) const;

		CommandArgs& addNumber(double value);
		CommandArgs& addBool(bool value);
		CommandArgs& addText(std::string_view value);
		// An argument already written as JSON.
		CommandArgs& addJson(std::string_view json);

	private:
		struct Parsed;
		const Parsed& parsed() const;
		void append(std::string_view json);

		std::string                     mJson = "[]";
		mutable std::shared_ptr<Parsed> mParsed;
	};

	// The work a backend supplies. The two shapes are one thing: a handler that wants no
	// arguments is not made to take a parameter it ignores.
	using CommandWork = std::function<void(const CommandArgs&)>;

	// What state.ts answers a command with. Writes what the action returned, as JSON, into
	// `result` -- "null" for nothing -- or says why it failed.
	using CommandAction = std::function<bool(const CommandArgs& args, std::string& result,
	                                         std::string& error)>;

	class Commands {
	public:
		// Declares a command, or returns the existing one. Generated code calls this;
		// declaring the name and supplying the work are separate steps because the
		// layout is resolved against the names, not against what they do.
		uint32_t define(std::string_view name);
		uint32_t find(std::string_view name) const;
		size_t   count() const { return mNames.size(); }
		const std::string& name(uint32_t command) const;

		// Supplies the work. Replacing an existing one is allowed and is what a
		// reconfiguring application does.
		void bind(uint32_t command, std::function<void()> work);
		void bind(uint32_t command, CommandWork work);
		bool hasWork(uint32_t command) const;

		// What state.ts answers it with; an empty function takes it away again, which is
		// what reloading state.ts does before it gives the new ones.
		void setAction(uint32_t command, CommandAction action);
		bool hasAction(uint32_t command) const;

		// Runs it: the action if there is one, then the backend's work if there is that --
		// unless the action failed, in which case nothing happened that a watcher should
		// be told of. False means nothing answers it at all, which is a mistake worth
		// reporting rather than a silent nothing. An action that threw has said so itself.
		bool invoke(uint32_t command, const CommandArgs& args = {});

		// The same, reporting what it returned: the action's value as JSON, or "null" for a
		// command the backend answers. False with the reason when nothing answers it or the
		// action failed.
		bool call(uint32_t command, const CommandArgs& args, std::string& result, std::string& error);

		// The backend's half alone, for a caller that has run the action itself -- run-time
		// code calling an action calls it directly, keeping its arguments and its result as
		// JavaScript values, and then lets a watching backend know.
		void runWork(uint32_t command, const CommandArgs& args);

	private:
		std::vector<std::string>                    mNames;
		std::vector<CommandWork>                    mWork;
		std::vector<CommandAction>                  mActions;
		std::unordered_map<std::string, uint32_t>   mByName;
	};

	// The one registry, for the same reason signals have one: a layout is resolved
	// against names, and two registries would mean two answers for one name.
	Commands& commands();
}
