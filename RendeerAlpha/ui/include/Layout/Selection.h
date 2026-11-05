#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Several rows chosen at once, for a <tree> or a <grid> with multiSelect.
//
// Rows are known by their keys rather than their positions, the way a single `selected`
// is: sorting a grid or opening a node in a tree moves rows about, and what was chosen
// has to stay chosen. Ranges are the exception, because "from here to there" is a
// question about the order on screen -- so a range is worked out from positions, through
// a function that names the key at each one, and stored as keys like everything else.
//
// What reaches the layout is one string, the keys one per line: signals are flat, and a
// line is what a key never contains -- the same reason a drop of files is handed over
// that way.
namespace RDA {

	// What a press or a key does to the selection, given what was held.
	enum class Pick : unsigned char {
		Only,      // this row and nothing else; it becomes the anchor
		Toggle,    // in if it was out, out if it was in; it becomes the anchor
		Range,     // from the anchor to here, and nothing else
		RangeAdd,  // from the anchor to here, added to what is already chosen
		Move,      // the keyboard moves, and the selection stays as it is
	};

	// A click: Ctrl toggles, Shift reaches from the anchor, both add the reach.
	Pick pickForClick(bool multi, bool ctrl, bool shift);
	// An arrow, a page key, Home or End: Shift reaches, Ctrl moves without choosing.
	Pick pickForKey(bool multi, bool ctrl, bool shift);

	class RowSelection {
	public:
		bool contains(std::string_view key) const { return mSet.count(std::string(key)) != 0; }
		bool empty() const { return mKeys.empty(); }
		size_t size() const { return mKeys.size(); }
		const std::vector<std::string>& keys() const { return mKeys; }
		const std::string& anchor() const { return mAnchor; }

		// The keys, one per line, in the order they were chosen.
		std::string joined() const;
		// From a binding: the keys one per line. True when that is a change. The anchor is
		// kept while it is still chosen, so Shift after the layout wrote the selection back
		// still reaches from where the reader was.
		bool assign(std::string_view joined);

		bool clear();
		// Every one of `count` rows, in order. The anchor stays where it was.
		bool all(size_t count, const std::function<std::string(size_t)>& keyAt);

		// Applies a pick to the row at shown position `to`. `count` is how many rows are
		// shown and `keyAt` names each one. True when the selection changed.
		bool apply(Pick pick, size_t to, size_t count, const std::function<std::string(size_t)>& keyAt);

		// From a screen reader, which asks for one row in or out with nothing held.
		bool add(const std::string& key);
		bool remove(const std::string& key);

	private:
		std::vector<std::string>        mKeys;
		std::unordered_set<std::string> mSet;
		std::string                     mAnchor;
	};
}
