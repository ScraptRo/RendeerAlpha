#include <GraphicalObjects/Docking.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <cstdlib>

// What a DockSpace is, apart from what it does each frame: the panels it owns, the types
// they can be spawned from, where they start, and how the arrangement is written down and
// read back. The frame itself is in DockPanes.cpp and DockTiles.cpp.
namespace RDA {

	static DockDrop dropFor(DockSide side) {
		switch (side) {
		case DockSide::Left:   return DockDrop::Left;
		case DockSide::Right:  return DockDrop::Right;
		case DockSide::Top:    return DockDrop::Top;
		case DockSide::Bottom: return DockDrop::Bottom;
		default:               return DockDrop::Center;
		}
	}

	uint32_t DockSpace::nextKeySalt() {
		// Dock spaces are created while the interface is being built, on the one thread
		// the GUI is touched from, so a plain counter is enough. Multiplied out so two
		// neighbouring spaces do not land in neighbouring slots.
		static uint32_t counter = 0;
		return ++counter * 2654435761u;
	}

	void DockSpace::structuralChange() {
		++mRevision;      // the GUI cache watches this
		mResizeSplit = nullptr; // a collapse can delete the node being dragged
	}

	DockContainer* DockSpace::add(const char* id, const char* title) {
		return adopt(std::make_unique<DockContainer>(id, title));
	}

	DockContainer* DockSpace::adopt(std::unique_ptr<DockContainer> container) {
		if (!container) return nullptr;
		structuralChange();
		// Two floating panels that start in the same place are one panel as far as anyone
		// can see -- which is what every layout with two of them and no positions got,
		// because they all start at the same default. Stepped down and across until it is
		// clear of the others, the way spawn() already cascades.
		if (container->dock == DockSide::Floating) {
			for (int guard = 0; guard < 32; ++guard) {
				bool taken = false;
				for (const auto& other : mContainers) {
					if (other->dock != DockSide::Floating) continue;
					if (std::fabs(other->floatingRect.x - container->floatingRect.x) < 1.0f &&
					    std::fabs(other->floatingRect.y - container->floatingRect.y) < 1.0f) {
						taken = true;
						break;
					}
				}
				if (!taken) break;
				container->floatingRect.x += 26.0f;
				container->floatingRect.y += 26.0f;
			}
		}
		mContainers.push_back(std::move(container));
		return mContainers.back().get();
	}

	void DockSpace::placeFloatingWithin(const Rect& area) {
		if (mFloatingPlaced) return;
		mFloatingPlaced = true;
		if (area.x == 0.0f && area.y == 0.0f) return;
		for (auto& cp : mContainers) {
			if (cp->dock != DockSide::Floating) continue;
			cp->floatingRect.x += area.x;
			cp->floatingRect.y += area.y;
		}
	}
	DockContainer* DockSpace::find(const char* id) {
		for (auto& c : mContainers) if (c->id() == id) return c.get();
		return nullptr;
	}
	DockContainer* DockSpace::containerFor(const std::string& id) const {
		for (auto& c : mContainers) if (c->id() == id) return c.get();
		return nullptr;
	}

	// ---- container prototypes -----------------------------------------------------
	void DockSpace::define(const char* type, const char* title, ContainerBuilder builder) {
		mTypes[type] = ContainerType{ title, std::move(builder) };
	}

	DockContainer* DockSpace::spawnWithId(const char* type, const char* id) {
		auto it = mTypes.find(type);
		if (it == mTypes.end()) return nullptr;
		DockContainer* c = add(id, it->second.title.c_str());
		c->type = type;
		c->closable = true;
		if (it->second.builder) it->second.builder(*c); // populate this instance's widgets
		return c;
	}

	DockContainer* DockSpace::spawn(const char* type) {
		// Spawning from a widget callback happens while update() is walking mContainers,
		// and adding to that vector there invalidates the walk. Queued instead, and
		// created at the top of the next update — the same deferral the close button uses.
		if (WidgetWalkGuard::active()) {
			mPendingSpawns.push_back(type);
			return nullptr;
		}
		std::string id = std::string(type) + "#" + std::to_string(++mSpawnCounter);
		DockContainer* c = spawnWithId(type, id.c_str());
		if (c) {
			c->dock = DockSide::Floating;
			// Cascade repeated spawns so they don't land exactly on top of each other.
			float offset = static_cast<float>(mSpawnCounter % 8) * 26.0f;
			c->floatingRect = { 70.0f + offset, 70.0f + offset, 280.0f, 220.0f };
		}
		return c;
	}

	// Anything a callback asked for last frame, created before this frame walks the
	// container list.
	void DockSpace::flushPendingSpawns() {
		if (mPendingSpawns.empty()) return;
		std::vector<std::string> pending;
		pending.swap(mPendingSpawns);
		for (const std::string& type : pending) spawn(type.c_str());
	}

	void DockSpace::remove(DockContainer* container) {
		if (!container) return;
		// Asked for twice, it is closed once: the second request would be for a container
		// the first has already destroyed by the time the queue is drained.
		if (std::find(mPendingRemove.begin(), mPendingRemove.end(), container) !=
		    mPendingRemove.end()) {
			return;
		}
		mPendingRemove.push_back(container); // applied at the end of update()
	}
	void DockSpace::removeById(const char* id) {
		remove(find(id));
	}

	void DockSpace::applyPendingRemovals() {
		if (mPendingRemove.empty()) return;
		// Taken out of the member and applied in the order they were asked for. This was
		// a single slot, so two panels closed in one frame -- one handler closing a pair,
		// a press on a close box and a call from the application -- kept only the later
		// request, and the earlier panel stayed open.
		std::vector<DockContainer*> closing;
		closing.swap(mPendingRemove);
		for (DockContainer* c : closing) removeNow(c);
	}

	void DockSpace::removeNow(DockContainer* c) {
		// What it looked like, kept before any of it is taken apart. The ghost is drawn
		// from this after the container itself is gone.
		ClosingPane ghost;
		ghost.id = c->id();
		ghost.title = c->title;
		ghost.variant = c->variant;
		ghost.rect = (c->computedRect.w > 1.0f && c->computedRect.h > 1.0f)
			? c->computedRect : c->floatingRect;
		if (ghost.rect.w > 1.0f && ghost.rect.h > 1.0f) mClosing.push_back(std::move(ghost));

		structuralChange();
		if (mDragging == c) mDragging = nullptr;
		if (mPressTab == c) mPressTab = nullptr;
		if (mResizingFloat == c) mResizingFloat = nullptr;
		if (mTileDrag == c) mTileDrag = nullptr;
		if (mTileResize == c) mTileResize = nullptr;
		// The hole closes behind it, which is TileGrid::remove's whole job.
		mGrid.remove(c->id());
		// Out of the tree first: that may collapse its pane away, and every pointer we
		// hold into the tree has just been dropped by structuralChange().
		mTree.remove(c->id());
		for (std::string& anchor : mSeedAnchor) if (anchor == c->id()) anchor.clear();
		mSeeded.erase(c->id());
		mContainers.erase(
			std::remove_if(mContainers.begin(), mContainers.end(),
				[c](const std::unique_ptr<DockContainer>& p) { return p.get() == c; }),
			mContainers.end());
	}

	// ---- seeding ------------------------------------------------------------------
	DockNode* DockSpace::largestPane() const {
		std::vector<DockNode*> leaves;
		mTree.collectLeaves(leaves);
		DockNode* best = nullptr;
		float bestArea = -1.0f;
		for (DockNode* leaf : leaves) {
			const float area = leaf->rect.w * leaf->rect.h;
			if (area > bestArea) { bestArea = area; best = leaf; }
		}
		return best;
	}

	void DockSpace::seedTree(glm::vec2 viewport) {
		// Centre first: it is the base the edge strips are carved out of, and seeding an
		// edge first would leave the centre panel landing as a tab beside it rather than
		// in the middle.
		//
		// Then the edges, in the reverse of how they should end up nested — splitRoot
		// wraps the entire tree, so whichever is seeded last is outermost. Taking Left
		// and Right last gives them the full window height, which is both what the old
		// five-slot layout did and what loadLayout() migrates an old file into. Seeding
		// in container-creation order instead would make a fresh start and a migrated
		// layout disagree about the same set of panels.
		static constexpr DockSide kSeedOrder[] = {
			DockSide::Center, DockSide::Bottom, DockSide::Top, DockSide::Right, DockSide::Left
		};
		for (const DockSide want : kSeedOrder) {
			for (auto& cp : mContainers) {
				DockContainer* c = cp.get();
				if (!c->visible || c->dock != want) continue;
				if (mSeeded.count(c->id())) continue;
				mSeeded.insert(c->id());
				structuralChange();

				const int hint = static_cast<int>(c->dock);
				// A panel with the same hint already opened a pane: share it as tabs.
				if (!mSeedAnchor[hint].empty()) {
					if (DockNode* mate = mTree.leafFor(mSeedAnchor[hint])) {
						mTree.addTab(mate, c->id());
						continue;
					}
					mSeedAnchor[hint].clear(); // that panel has since moved or closed
				}

				if (c->dock == DockSide::Center || mTree.empty()) {
					DockNode* pane = mTree.empty() ? mTree.root() : largestPane();
					mTree.addTab(pane, c->id());
				} else {
					// dockSize was a pixel width/height; as a share of the window it
					// gives the same proportions at the size it was chosen for.
					const bool horizontal = (c->dock == DockSide::Left || c->dock == DockSide::Right);
					const float span = horizontal ? viewport.x : viewport.y;
					float share = (span > 0.0f && c->dockSize > 0.0f) ? (c->dockSize / span) : 0.25f;
					mTree.splitRoot(dropFor(c->dock), c->id(), std::clamp(share, 0.08f, 0.60f));
				}
				mSeedAnchor[hint] = c->id();
			}
		}
	}

	// ---- layout persistence -------------------------------------------------------
	std::string DockSpace::saveLayout() const {
		std::ostringstream out;
		// Both arrangements are written, not just the one in use. A space that was
		// switched from one to the other keeps what the reader had done on each side,
		// and a file is readable by either -- the grid's block begins with "tiles" and
		// the tree's with its own header, so neither can be mistaken for the other.
		out << mTree.save(); // version header + the pane tree
		if (!mGrid.empty()) out << mGrid.save();
		// Every container, so a spawned instance can be recreated and a floating one
		// gets its window back. Whether it is docked is the tree's business.
		for (const auto& c : mContainers) {
			out << "c " << c->id() << ' ' << (c->type.empty() ? "-" : c->type) << ' '
				<< c->floatingRect.x << ' ' << c->floatingRect.y << ' '
				<< c->floatingRect.w << ' ' << c->floatingRect.h << '\n';
		}
		return out.str();
	}

	void DockSpace::loadLayout(const std::string& data) {
		// The old format put a slot index before the floating rect on each container
		// line. Everything else about reading them is the same, and DockLayout reads the
		// line for both of us: it is the one that has to read it for the old format anyway.
		const bool legacy = DockLayout::isLegacyFormat(data);

		std::istringstream in(data);
		std::string line;
		std::vector<DockContainer*> written;   // in the order the file has them
		while (std::getline(in, line)) {
			DockLayout::SavedContainer saved;
			if (!DockLayout::readContainerLine(line, legacy, saved)) continue;
			const std::string& id = saved.id;
			const Rect& fr = saved.floating;

			DockContainer* c = find(id.c_str());
			if (!c && saved.type != "-") {
				// A spawned instance that no longer exists — recreate it from its type.
				c = spawnWithId(saved.type.c_str(), id.c_str());
				const std::size_t hash = id.rfind('#');
				if (hash != std::string::npos) {
					const uint64_t n = std::strtoull(id.c_str() + hash + 1, nullptr, 10);
					if (n > mSpawnCounter) mSpawnCounter = n;
				}
			}
			if (!c) continue;
			written.push_back(c);
			if (fr.w > 0.0f && fr.h > 0.0f) c->floatingRect = fr;
			// Placed by the file, so seeding leaves it alone. A container the file does
			// not mention is new since the layout was saved and still gets its hint.
			mSeeded.insert(c->id());
		}

		// The file's order is the stacking the floating panels were left in -- the last
		// one raised is last -- so it is put back. Each moves to the end in turn, which
		// leaves them in file order behind anything the file did not mention.
		for (DockContainer* c : written) {
			auto at = std::find_if(mContainers.begin(), mContainers.end(),
			                       [c](const std::unique_ptr<DockContainer>& p) { return p.get() == c; });
			if (at != mContainers.end()) std::rotate(at, at + 1, mContainers.end());
		}

		mTree.load(data); // ignores the lines above; migrates the old format if needed
		// The grid's block, if the file has one. Found rather than parsed in step with
		// the rest, because the two blocks are independent and either may be missing.
		if (const size_t at = data.find("\ntiles "); at != std::string::npos) {
			mGrid.load(data.substr(at + 1));
		} else if (data.rfind("tiles ", 0) == 0) {
			mGrid.load(data);
		}
		structuralChange();
	}

	bool DockSpace::saveLayoutToFile(const std::string& path) const {
		std::ofstream file(path, std::ios::trunc);
		if (!file) return false;
		file << saveLayout();
		return true;
	}

	bool DockSpace::loadLayoutFromFile(const std::string& path) {
		std::ifstream file(path);
		if (!file) return false;
		std::ostringstream ss;
		ss << file.rdbuf();
		loadLayout(ss.str());
		return true;
	}
}
