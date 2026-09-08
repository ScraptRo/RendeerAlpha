#include <Core/Commands.h>
#include <Logger/Logger.h>

namespace RDA {

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
		mByName.emplace(key, id);
		return id;
	}

	uint32_t Commands::find(std::string_view name) const {
		const auto found = mByName.find(std::string(name));
		return found == mByName.end() ? kNoCommand : found->second;
	}

	void Commands::bind(uint32_t command, std::function<void()> work) {
		if (command >= mWork.size()) return;
		mWork[command] = std::move(work);
	}

	bool Commands::invoke(uint32_t command) {
		if (command >= mWork.size() || !mWork[command]) return false;
		mWork[command]();
		return true;
	}

}
