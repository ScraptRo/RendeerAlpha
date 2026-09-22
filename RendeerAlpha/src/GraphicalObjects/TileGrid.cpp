#include <GraphicalObjects/TileGrid.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace RDA {

	Tile* TileGrid::find(const std::string& id) {
		for (Tile& tile : mTiles) {
			if (tile.id == id) return &tile;
		}
		return nullptr;
	}

	const Tile* TileGrid::find(const std::string& id) const {
		return const_cast<TileGrid*>(this)->find(id);
	}

	bool TileGrid::collidesWithAny(const Tile& tile, const std::string& ignore) const {
		for (const Tile& other : mTiles) {
			if (other.id == tile.id || other.id == ignore) continue;
			if (tile.overlaps(other)) return true;
		}
		return false;
	}

	Tile& TileGrid::place(const std::string& id, int cols, int rows) {
		if (Tile* existing = find(id)) return *existing;

		Tile tile;
		tile.id = id;
		tile.cols = std::clamp(cols, kMinCols, (std::max)(1, columns));
		tile.rows = (std::max)(kMinRows, rows);

		// The first place it fits, reading left to right and then down. A new panel
		// landing in the first hole is what keeps a grid from growing a ragged bottom
		// edge every time something is added.
		for (int row = 0; row < 1000; ++row) {
			for (int col = 0; col + tile.cols <= columns; ++col) {
				tile.col = col;
				tile.row = row;
				if (!collidesWithAny(tile, {})) {
					mTiles.push_back(tile);
					return mTiles.back();
				}
			}
		}
		// Nothing fits anywhere, which takes a thousand full rows. Put it at the bottom
		// rather than refusing: a panel that exists has to be somewhere.
		tile.col = 0;
		tile.row = rowsUsed();
		mTiles.push_back(tile);
		return mTiles.back();
	}

	Tile& TileGrid::placeAt(const std::string& id, int col, int row, int cols, int rows) {
		Tile* existing = find(id);
		if (!existing) {
			Tile tile;
			tile.id = id;
			mTiles.push_back(tile);
			existing = &mTiles.back();
		}
		moveTo(id, col, row, cols, rows);
		return *find(id);
	}

	void TileGrid::remove(const std::string& id) {
		const size_t before = mTiles.size();
		mTiles.erase(std::remove_if(mTiles.begin(), mTiles.end(),
		                            [&](const Tile& t) { return t.id == id; }),
		             mTiles.end());
		// The hole it left is closed, so removing a panel from the middle does not leave
		// the grid with a gap nothing will ever fill.
		if (mTiles.size() != before) compact({});
	}

	Rect TileGrid::rectFor(const Tile& tile, const Rect& area) const {
		const int wide = (std::max)(1, columns);
		// The gaps come out of the width first, so `columns` cells plus the gaps between
		// and either side of them is exactly the area. A tile of n cells then spans n
		// cells and the n-1 gaps it covers.
		const float cell = ((std::max)(0.0f, area.w - gap * (wide + 1))) / static_cast<float>(wide);
		const float x = area.x + gap + static_cast<float>(tile.col) * (cell + gap);
		const float w = static_cast<float>(tile.cols) * cell +
		                static_cast<float>(tile.cols - 1) * gap;
		const float y = area.y + gap + static_cast<float>(tile.row) * (rowHeight + gap);
		const float h = static_cast<float>(tile.rows) * rowHeight +
		                static_cast<float>(tile.rows - 1) * gap;
		return Rect{ x, y, (std::max)(0.0f, w), (std::max)(0.0f, h) };
	}

	void TileGrid::cellAt(const Rect& area, glm::vec2 point, int& col, int& row) const {
		const int wide = (std::max)(1, columns);
		const float cell = ((std::max)(1.0f, area.w - gap * (wide + 1))) / static_cast<float>(wide);
		col = static_cast<int>(std::floor((point.x - area.x - gap) / (cell + gap) + 0.5f));
		row = static_cast<int>(std::floor((point.y - area.y - gap) / (rowHeight + gap) + 0.5f));
	}

	int TileGrid::rowsUsed() const {
		int used = 0;
		for (const Tile& tile : mTiles) used = (std::max)(used, tile.row + tile.rows);
		return used;
	}

	void TileGrid::pushOthers(const Tile& moving) {
		// Breadth-first rather than recursive: a push can cascade through the whole grid,
		// and a chain of tiles each pushing the next is exactly the case where recursion
		// on a bad arrangement would not end.
		std::vector<std::string> front{ moving.id };
		for (int rounds = 0; rounds < 64 && !front.empty(); ++rounds) {
			std::vector<std::string> next;
			for (const std::string& id : front) {
				const Tile* pusher = find(id);
				if (!pusher) continue;
				const Tile above = *pusher;   // copied: the loop below moves tiles
				for (Tile& other : mTiles) {
					if (other.id == above.id) continue;
					if (!above.overlaps(other)) continue;
					other.row = above.row + above.rows;
					next.push_back(other.id);
				}
			}
			front.swap(next);
		}
	}

	void TileGrid::compact(const std::string& except) {
		// In reading order, so a tile is only ever pulled up past space that the tiles
		// above it have already finished claiming.
		std::vector<Tile*> order;
		order.reserve(mTiles.size());
		for (Tile& tile : mTiles) order.push_back(&tile);
		std::sort(order.begin(), order.end(), [](const Tile* a, const Tile* b) {
			if (a->row != b->row) return a->row < b->row;
			return a->col < b->col;
		});

		for (Tile* tile : order) {
			if (tile->id == except) continue;
			while (tile->row > 0) {
				const int was = tile->row;
				tile->row -= 1;
				if (collidesWithAny(*tile, {})) { tile->row = was; break; }
			}
		}
	}

	void TileGrid::moveTo(const std::string& id, int col, int row, int cols, int rows,
	                      const std::string& settling) {
		Tile* tile = find(id);
		if (!tile) return;

		const int wide = (std::max)(1, columns);
		tile->cols = std::clamp(cols, kMinCols, wide);
		tile->rows = (std::max)(kMinRows, rows);
		// Clamped rather than refused: a tile dragged past the right edge belongs against
		// it, and one dragged above the top belongs at the top.
		tile->col = std::clamp(col, 0, wide - tile->cols);
		tile->row = (std::max)(0, row);

		pushOthers(*tile);
		compact(settling);
	}

	std::string TileGrid::save() const {
		std::ostringstream out;
		out << "tiles " << columns << ' ' << rowHeight << ' ' << gap << '\n';
		for (const Tile& tile : mTiles) {
			// Ids carry no whitespace -- the same rule the tree's format relies on -- so
			// a line splits on spaces and needs no quoting.
			out << tile.id << ' ' << tile.col << ' ' << tile.row << ' '
			    << tile.cols << ' ' << tile.rows << '\n';
		}
		return out.str();
	}

	bool TileGrid::load(const std::string& text) {
		std::istringstream in(text);
		std::string word;
		if (!(in >> word) || word != "tiles") return false;

		int wide = columns;
		float height = rowHeight, space = gap;
		if (!(in >> wide >> height >> space)) return false;

		std::vector<Tile> read;
		Tile tile;
		while (in >> tile.id >> tile.col >> tile.row >> tile.cols >> tile.rows) {
			if (tile.id.empty()) continue;
			read.push_back(tile);
		}
		// Left alone on a file that says nothing, so a truncated save does not empty a
		// grid that was working.
		if (read.empty()) return false;

		columns = (std::max)(1, wide);
		rowHeight = height > 0.0f ? height : rowHeight;
		gap = space >= 0.0f ? space : gap;
		mTiles.swap(read);
		// Whatever was written, the grid's own rules hold afterwards: clamped into the
		// columns, no overlaps, no holes. A file edited by hand cannot put the grid into
		// a state the rest of this class does not expect.
		for (Tile& one : mTiles) {
			one.cols = std::clamp(one.cols, kMinCols, columns);
			one.rows = (std::max)(kMinRows, one.rows);
			one.col = std::clamp(one.col, 0, columns - one.cols);
			one.row = (std::max)(0, one.row);
		}
		for (const Tile& one : mTiles) pushOthers(one);
		compact({});
		return true;
	}
}
