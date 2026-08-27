#include <Core/Signals.h>
#include <Logger/Logger.h>
#include <algorithm>

namespace RDA {

	Signals& signals() {
		static Signals table;
		return table;
	}

	uint32_t Signals::declare(std::string_view name, SignalType type) {
		const std::string key(name);
		if (const auto found = mByName.find(key); found != mByName.end()) {
			const uint32_t index = found->second;
			if (mSignals[index].type != type) {
				// Refused rather than reinterpreted: one name meaning two things would
				// surface as a wrong value somewhere far from here.
				RDA_LOG_WARNING("signal '" << key << "' already exists with a different type; "
				                "the existing one is kept");
			}
			return index;
		}

		Signal signal;
		signal.name = key;
		signal.type = type;
		mSignals.push_back(std::move(signal));
		const uint32_t index = static_cast<uint32_t>(mSignals.size() - 1);
		mByName.emplace(key, index);
		mQueued.push_back(false);
		return index;
	}

	uint32_t Signals::define(std::string_view name, double value) {
		const uint32_t index = declare(name, SignalType::Number);
		if (mSignals[index].type == SignalType::Number) mSignals[index].number = value;
		return index;
	}

	uint32_t Signals::define(std::string_view name, bool value) {
		const uint32_t index = declare(name, SignalType::Bool);
		if (mSignals[index].type == SignalType::Bool) mSignals[index].number = value ? 1.0 : 0.0;
		return index;
	}

	uint32_t Signals::define(std::string_view name, std::string_view value) {
		const uint32_t index = declare(name, SignalType::Text);
		if (mSignals[index].type == SignalType::Text) mSignals[index].text.assign(value);
		return index;
	}

	uint32_t Signals::find(std::string_view name) const {
		const auto found = mByName.find(std::string(name));
		return found == mByName.end() ? kNoSignal : found->second;
	}

	std::string_view Signals::name(uint32_t signal) const {
		return valid(signal) ? std::string_view(mSignals[signal].name) : std::string_view();
	}

	SignalType Signals::type(uint32_t signal) const {
		return valid(signal) ? mSignals[signal].type : SignalType::Number;
	}

	double Signals::number(uint32_t signal) const {
		return valid(signal) ? mSignals[signal].number : 0.0;
	}

	bool Signals::boolean(uint32_t signal) const {
		return valid(signal) && mSignals[signal].number != 0.0;
	}

	std::string_view Signals::text(uint32_t signal) const {
		return valid(signal) ? std::string_view(mSignals[signal].text) : std::string_view();
	}

	void Signals::markDirty(const Signal& signal) {
		for (const ObserverId observer : signal.observers) {
			// mQueued is indexed by observer, and observers are dense because the layout
			// runtime hands them out in order. A sparse id would grow this, which is why
			// they are allocated rather than chosen.
			if (observer >= mQueued.size()) mQueued.resize(observer + 1, false);
			if (mQueued[observer]) continue;
			mQueued[observer] = true;
			mDirty.push_back(observer);
		}
	}

	void Signals::set(uint32_t signal, double value) {
		if (!valid(signal) || mSignals[signal].type != SignalType::Number) return;
		Signal& target = mSignals[signal];
		if (target.number == value) return; // not a change, so not a notification
		target.number = value;
		markDirty(target);
	}

	void Signals::set(uint32_t signal, bool value) {
		if (!valid(signal) || mSignals[signal].type != SignalType::Bool) return;
		Signal& target = mSignals[signal];
		const double next = value ? 1.0 : 0.0;
		if (target.number == next) return;
		target.number = next;
		markDirty(target);
	}

	void Signals::set(uint32_t signal, std::string_view value) {
		if (!valid(signal) || mSignals[signal].type != SignalType::Text) return;
		Signal& target = mSignals[signal];
		if (target.text == value) return;
		target.text.assign(value);
		markDirty(target);
	}

	void Signals::observe(uint32_t signal, ObserverId observer) {
		if (!valid(signal)) return;
		std::vector<ObserverId>& observers = mSignals[signal].observers;
		// A binding that reads the same signal twice is one edge, not two, or a single
		// write would queue it twice and it would be evaluated twice.
		if (std::find(observers.begin(), observers.end(), observer) != observers.end()) return;
		observers.push_back(observer);
		if (observer >= mQueued.size()) mQueued.resize(observer + 1, false);
	}

	void Signals::forget(ObserverId observer) {
		for (Signal& signal : mSignals) {
			std::vector<ObserverId>& observers = signal.observers;
			observers.erase(std::remove(observers.begin(), observers.end(), observer),
			                observers.end());
		}
		// Also out of the pending list: re-evaluating a binding whose widget has gone is
		// exactly the use-after-free this is meant to prevent.
		mDirty.erase(std::remove(mDirty.begin(), mDirty.end(), observer), mDirty.end());
		if (observer < mQueued.size()) mQueued[observer] = false;
	}

	void Signals::clearDirty() {
		for (const ObserverId observer : mDirty) {
			if (observer < mQueued.size()) mQueued[observer] = false;
		}
		mDirty.clear();
	}

	void Signals::clear() {
		mSignals.clear();
		mByName.clear();
		mDirty.clear();
		mQueued.clear();
	}
}
