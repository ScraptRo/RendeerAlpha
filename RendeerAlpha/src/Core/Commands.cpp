#include <Core/Commands.h>
#include <Core/Json.h>
#include <Logger/Logger.h>

#include <cstdlib>

namespace RDA {

	// ---- CommandArgs -----------------------------------------------------------------

	// Parsed once, on the first read, and shared by copies: a handler that reads three
	// arguments should not parse the array three times, and most callers never read at all.
	struct CommandArgs::Parsed {
		std::vector<Json::Item> items;
	};

	const CommandArgs::Parsed& CommandArgs::parsed() const {
		if (!mParsed) {
			auto one = std::make_shared<Parsed>();
			if (!Json::readArray(mJson, one->items)) one->items.clear();
			mParsed = std::move(one);
		}
		return *mParsed;
	}

	size_t CommandArgs::size() const {
		if (mJson == "[]") return 0;
		return parsed().items.size();
	}

	double CommandArgs::number(size_t index, double fallback) const {
		const auto& items = parsed().items;
		if (index >= items.size()) return fallback;
		const Json::Item& item = items[index];
		switch (item.kind) {
		case Json::Item::Kind::Number: return item.number;
		case Json::Item::Kind::Bool:   return item.flag ? 1.0 : 0.0;
		case Json::Item::Kind::Text: {
			// A number that arrived as text -- a <textfield>'s value -- is still the number.
			char* end = nullptr;
			const double value = std::strtod(item.text.c_str(), &end);
			return end && end != item.text.c_str() ? value : fallback;
		}
		default: return fallback;
		}
	}

	bool CommandArgs::boolean(size_t index, bool fallback) const {
		const auto& items = parsed().items;
		if (index >= items.size()) return fallback;
		const Json::Item& item = items[index];
		switch (item.kind) {
		case Json::Item::Kind::Bool:   return item.flag;
		case Json::Item::Kind::Number: return item.number != 0.0;
		case Json::Item::Kind::Text:   return !item.text.empty();
		case Json::Item::Kind::Null:   return false;
		default:                       return true;
		}
	}

	std::string CommandArgs::text(size_t index, std::string_view fallback) const {
		const auto& items = parsed().items;
		if (index >= items.size()) return std::string(fallback);
		const Json::Item& item = items[index];
		switch (item.kind) {
		case Json::Item::Kind::Text: return item.text;
		case Json::Item::Kind::Null: return std::string(fallback);
		default:                     return item.raw;   // 3, true, [1,2] -- as written
		}
	}

	std::string CommandArgs::raw(size_t index) const {
		const auto& items = parsed().items;
		return index < items.size() ? items[index].raw : std::string("null");
	}

	void CommandArgs::append(std::string_view json) {
		mParsed.reset();
		mJson.pop_back();                     // the closing bracket
		if (mJson.size() > 1) mJson.push_back(',');
		mJson.append(json);
		mJson.push_back(']');
	}

	CommandArgs& CommandArgs::addNumber(double value) {
		std::string one;
		Json::appendNumber(one, value);
		append(one);
		return *this;
	}

	CommandArgs& CommandArgs::addBool(bool value) {
		append(value ? "true" : "false");
		return *this;
	}

	CommandArgs& CommandArgs::addText(std::string_view value) {
		std::string one;
		Json::appendString(one, value);
		append(one);
		return *this;
	}

	CommandArgs& CommandArgs::addJson(std::string_view json) {
		append(json.empty() ? std::string_view("null") : json);
		return *this;
	}

	// ---- Commands --------------------------------------------------------------------

	// Never destroyed, like the other three tables: a destructor that runs after main may
	// still reach this. See bindings() in Layout/Bindings.cpp for the order that bites.
	Commands& commands() {
		static Commands* registry = new Commands;
		return *registry;
	}

	uint32_t Commands::define(std::string_view name) {
		const std::string key(name);
		const auto found = mByName.find(key);
		if (found != mByName.end()) return found->second;

		const uint32_t id = static_cast<uint32_t>(mNames.size());
		mNames.push_back(key);
		mWork.emplace_back();
		mActions.emplace_back();
		mByName.emplace(key, id);
		return id;
	}

	uint32_t Commands::find(std::string_view name) const {
		const auto found = mByName.find(std::string(name));
		return found == mByName.end() ? kNoCommand : found->second;
	}

	const std::string& Commands::name(uint32_t command) const {
		static const std::string none;
		return command < mNames.size() ? mNames[command] : none;
	}

	void Commands::bind(uint32_t command, std::function<void()> work) {
		if (command >= mWork.size()) return;
		if (!work) { mWork[command] = nullptr; return; }
		mWork[command] = [work = std::move(work)](const CommandArgs&) { work(); };
	}

	void Commands::bind(uint32_t command, CommandWork work) {
		if (command >= mWork.size()) return;
		mWork[command] = std::move(work);
	}

	bool Commands::hasWork(uint32_t command) const {
		return command < mWork.size() && static_cast<bool>(mWork[command]);
	}

	void Commands::setAction(uint32_t command, CommandAction action) {
		if (command >= mActions.size()) return;
		mActions[command] = std::move(action);
	}

	bool Commands::hasAction(uint32_t command) const {
		return command < mActions.size() && static_cast<bool>(mActions[command]);
	}

	bool Commands::invoke(uint32_t command, const CommandArgs& args) {
		std::string result, error;
		if (call(command, args, result, error)) return true;
		// Answered, and failed: the action has reported why, with its line. Only nothing
		// answering at all is the caller's to report.
		return hasAction(command);
	}

	bool Commands::call(uint32_t command, const CommandArgs& args, std::string& result,
	                    std::string& error) {
		result = "null";
		if (command >= mNames.size()) {
			error = "no such command";
			return false;
		}
		const bool action = hasAction(command);
		if (!action && !hasWork(command)) {
			error = "nothing answers commands." + mNames[command] +
			        " -- no handler is bound to it, and state.ts does not answer it";
			return false;
		}
		if (action) {
			// Copied: an action may reload state.ts, which replaces this very function.
			const CommandAction run = mActions[command];
			if (!run(args, result, error)) return false;
		}
		runWork(command, args);
		return true;
	}

	void Commands::runWork(uint32_t command, const CommandArgs& args) {
		if (!hasWork(command)) return;
		// Copied for the same reason: a handler may bind its own command again.
		const CommandWork work = mWork[command];
		work(args);
	}

}
