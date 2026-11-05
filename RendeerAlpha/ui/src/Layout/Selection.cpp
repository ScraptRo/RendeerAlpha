#include <Layout/Selection.h>

#include <algorithm>

namespace RDA {

	Pick pickForClick(bool multi, bool ctrl, bool shift) {
		if (!multi) return Pick::Only;
		if (ctrl && shift) return Pick::RangeAdd;
		if (shift) return Pick::Range;
		if (ctrl) return Pick::Toggle;
		return Pick::Only;
	}

	Pick pickForKey(bool multi, bool ctrl, bool shift) {
		if (!multi) return Pick::Only;
		// Ctrl+Shift+Down reaches like Shift+Down: keeping the rest is what Ctrl+click is
		// for, and the keyboard has Ctrl+Space for that.
		if (shift) return Pick::Range;
		if (ctrl) return Pick::Move;
		return Pick::Only;
	}

	std::string RowSelection::joined() const {
		std::string out;
		for (const std::string& key : mKeys) {
			if (!out.empty()) out += '\n';
			out += key;
		}
		return out;
	}

	bool RowSelection::assign(std::string_view joined) {
		std::vector<std::string> keys;
		std::unordered_set<std::string> set;
		while (!joined.empty()) {
			const size_t end = joined.find('\n');
			std::string_view line = joined.substr(0, end);
			// A line ending written by Windows is the line's end, not part of the key.
			if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
			if (!line.empty() && set.insert(std::string(line)).second) keys.emplace_back(line);
			if (end == std::string_view::npos) break;
			joined.remove_prefix(end + 1);
		}
		if (keys == mKeys) return false;
		mKeys = std::move(keys);
		mSet = std::move(set);
		if (!mSet.count(mAnchor)) mAnchor = mKeys.empty() ? std::string() : mKeys.back();
		return true;
	}

	bool RowSelection::clear() {
		if (mKeys.empty()) return false;
		mKeys.clear();
		mSet.clear();
		return true;
	}

	bool RowSelection::add(const std::string& key) {
		if (!mSet.insert(key).second) return false;
		mKeys.push_back(key);
		mAnchor = key;
		return true;
	}

	bool RowSelection::remove(const std::string& key) {
		if (!mSet.erase(key)) return false;
		mKeys.erase(std::find(mKeys.begin(), mKeys.end(), key));
		return true;
	}

	bool RowSelection::all(size_t count, const std::function<std::string(size_t)>& keyAt) {
		bool changed = false;
		for (size_t i = 0; i < count; ++i) {
			std::string key = keyAt(i);
			if (mSet.insert(key).second) { mKeys.push_back(std::move(key)); changed = true; }
		}
		return changed;
	}

	bool RowSelection::apply(Pick pick, size_t to, size_t count,
	                         const std::function<std::string(size_t)>& keyAt) {
		if (to >= count) return false;
		const std::string key = keyAt(to);
		switch (pick) {
		case Pick::Move:
			return false;
		case Pick::Only: {
			mAnchor = key;
			if (mKeys.size() == 1 && mKeys.front() == key) return false;
			mKeys.assign(1, key);
			mSet.clear();
			mSet.insert(key);
			return true;
		}
		case Pick::Toggle:
			mAnchor = key;
			if (mSet.count(key)) return remove(key);
			mSet.insert(key);
			mKeys.push_back(key);
			return true;
		case Pick::Range:
		case Pick::RangeAdd: {
			// Where the anchor is shown now -- which a sort or a node opening may have moved
			// -- and if it is not shown at all, here: a reach from nowhere is this row.
			size_t from = count;
			if (!mAnchor.empty()) {
				for (size_t i = 0; i < count; ++i) {
					if (keyAt(i) == mAnchor) { from = i; break; }
				}
			}
			if (from == count) { from = to; mAnchor = key; }
			const size_t lo = (std::min)(from, to), hi = (std::max)(from, to);
			std::vector<std::string> keys;
			std::unordered_set<std::string> set;
			if (pick == Pick::RangeAdd) { keys = mKeys; set = mSet; }
			// From the anchor towards the row reached, so the keys read in the order the
			// reader swept them.
			for (size_t n = 0; n <= hi - lo; ++n) {
				const size_t i = (from <= to) ? lo + n : hi - n;
				std::string k = keyAt(i);
				if (set.insert(k).second) keys.push_back(std::move(k));
			}
			if (keys == mKeys) return false;
			mKeys = std::move(keys);
			mSet = std::move(set);
			return true;
		}
		}
		return false;
	}
}
