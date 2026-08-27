#include <GraphicalObjects/Docking.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <cstdlib>

namespace RDA {

	static constexpr float kTabH = 22.0f;    // tab / title bar height
	static constexpr float kSplit = 6.0f;    // splitter thickness
	static constexpr float kMinDock = 90.0f; // smallest a pane can be squeezed to
	static constexpr float kEdgeBand = 26.0f;// window border band = dock across that edge
	static constexpr float kEdgeShare = 0.25f;// how much of the window a full-edge dock takes

	static DockDrop dropFor(DockSide side) {
		switch (side) {
		case DockSide::Left:   return DockDrop::Left;
		case DockSide::Right:  return DockDrop::Right;
		case DockSide::Top:    return DockDrop::Top;
		case DockSide::Bottom: return DockDrop::Bottom;
		default:               return DockDrop::Center;
		}
	}

	void DockSpace::structuralChange() {
		++mRevision;      // the GUI cache watches this
		mResizeSplit = nullptr; // a collapse can delete the node being dragged
	}

	DockContainer* DockSpace::add(const char* id, const char* title) {
		structuralChange();
		mContainers.push_back(std::make_unique<DockContainer>(id, title));
		return mContainers.back().get();
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

	void DockSpace::remove(DockContainer* container) {
		if (container) mPendingRemove = container; // applied at the end of update()
	}
	void DockSpace::removeById(const char* id) {
		remove(find(id));
	}

	void DockSpace::applyPendingRemovals() {
		if (!mPendingRemove) return;
		DockContainer* c = mPendingRemove;
		mPendingRemove = nullptr;
		structuralChange();
		if (mDragging == c) mDragging = nullptr;
		if (mPressTab == c) mPressTab = nullptr;
		if (mResizingFloat == c) mResizingFloat = nullptr;
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
		out << mTree.save(); // version header + the pane tree
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
		// line. Everything else about reading them is the same.
		const bool legacy = data.rfind("rda-dock 1", 0) == 0;

		std::istringstream in(data);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream ls(line);
			char kind = 0;
			ls >> kind;
			if (kind != 'c') continue;
			std::string id, type;
			ls >> id >> type;
			Rect fr{};
			if (legacy) { int side = 0; ls >> side; }
			ls >> fr.x >> fr.y >> fr.w >> fr.h;

			DockContainer* c = find(id.c_str());
			if (!c && type != "-") {
				// A spawned instance that no longer exists — recreate it from its type.
				c = spawnWithId(type.c_str(), id.c_str());
				const std::size_t hash = id.rfind('#');
				if (hash != std::string::npos) {
					const uint64_t n = std::strtoull(id.c_str() + hash + 1, nullptr, 10);
					if (n > mSpawnCounter) mSpawnCounter = n;
				}
			}
			if (!c) continue;
			if (fr.w > 0.0f && fr.h > 0.0f) c->floatingRect = fr;
			// Placed by the file, so seeding leaves it alone. A container the file does
			// not mention is new since the layout was saved and still gets its hint.
			mSeeded.insert(c->id());
		}

		mTree.load(data); // ignores the lines above; migrates the old format if needed
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

	// ---- drop targets -------------------------------------------------------------
	DockDrop DockSpace::windowEdgeAt(glm::vec2 p, glm::vec2 viewport, Rect* previewOut) const {
		if (p.x < 0.0f || p.y < 0.0f || p.x > viewport.x || p.y > viewport.y) return DockDrop::None;
		DockDrop where = DockDrop::None;
		if      (p.x < kEdgeBand)              where = DockDrop::Left;
		else if (p.x > viewport.x - kEdgeBand) where = DockDrop::Right;
		else if (p.y < kEdgeBand)              where = DockDrop::Top;
		else if (p.y > viewport.y - kEdgeBand) where = DockDrop::Bottom;
		if (where == DockDrop::None || !previewOut) return where;

		const float ew = viewport.x * kEdgeShare, eh = viewport.y * kEdgeShare;
		switch (where) {
		case DockDrop::Left:   *previewOut = { 0.0f, 0.0f, ew, viewport.y }; break;
		case DockDrop::Right:  *previewOut = { viewport.x - ew, 0.0f, ew, viewport.y }; break;
		case DockDrop::Top:    *previewOut = { 0.0f, 0.0f, viewport.x, eh }; break;
		default:               *previewOut = { 0.0f, viewport.y - eh, viewport.x, eh }; break;
		}
		return where;
	}

	// ---- painting one pane ---------------------------------------------------------
	void DockSpace::drawPane(Gui& gui, DockNode* leaf, bool& busy) {
		if (!leaf || leaf->tabs.empty()) return;
		const Rect slot = leaf->rect;
		if (slot.w < 8.0f || slot.h < kTabH) return; // squeezed to nothing

		const GuiInput& in = gui.input();
		gui.drawRect(slot, rgba(22, 24, 30, 245));
		gui.drawRect({ slot.x, slot.y, slot.w, kTabH }, rgba(30, 33, 42));

		float tx = slot.x;
		for (size_t i = 0; i < leaf->tabs.size(); ++i) {
			DockContainer* t = containerFor(leaf->tabs[i]);
			if (!t || !t->visible) continue;
			const float tw = gui.measureText(t->title.c_str()) + 20.0f;
			const Rect tabR{ tx, slot.y, tw, kTabH };
			const bool activeTab = (static_cast<int>(i) == leaf->active);
			gui.drawRect(tabR, activeTab ? rgba(58, 92, 168) : rgba(40, 44, 54));
			gui.drawText(t->title.c_str(), { tabR.x + 10.0f, tabR.y + 3.0f }, rgba(232, 234, 240));
			if (!busy && in.pressed && tabR.contains(in.pointer)) {
				leaf->active = static_cast<int>(i);
				mPressTab = t;
				mPressPos = in.pointer;
				busy = true;
			}
			tx += tw;
		}

		const std::string* activeId = leaf->activeTab();
		DockContainer* act = activeId ? containerFor(*activeId) : nullptr;
		if (!act || !act->visible) return;

		// Close (x) for the active container, at the far right of the tab bar.
		if (act->closable) {
			const Rect xr{ slot.x + slot.w - 20.0f, slot.y, 18.0f, kTabH };
			const bool hot = xr.contains(in.pointer);
			gui.drawText("x", { xr.x + 6.0f, xr.y + 3.0f }, hot ? rgba(255, 180, 180) : rgba(200, 200, 210));
			if (!busy && in.pressed && hot) { mPendingRemove = act; busy = true; }
		}

		const Rect body{ slot.x, slot.y + kTabH, slot.w, slot.h - kTabH };
		act->computedRect = slot;
		gui.pushId(act->id().c_str()); // isolate this instance's widget state
		gui.pushClipRect(body);
		act->drawContents(gui, body);
		gui.popClipRect();
		gui.popId();
	}

	// ---- the frame -----------------------------------------------------------------
	void DockSpace::update(Gui& gui, glm::vec2 viewport) {
		// Anything a callback asked for last frame, created before this frame walks the
		// container list.
		if (!mPendingSpawns.empty()) {
			std::vector<std::string> pending;
			pending.swap(mPendingSpawns);
			for (const std::string& type : pending) spawn(type.c_str());
		}

		const GuiInput& in = gui.input();
		const Rect area{ 0.0f, 0.0f, viewport.x, viewport.y };

		// Laid out once so seeding can compare pane sizes, then again with what it added.
		mTree.layout(area, kSplit);
		seedTree(viewport);
		mTree.layout(area, kSplit);

		// --- advance a splitter drag ---
		if (mResizeSplit) {
			mTree.setRatioFromPointer(mResizeSplit, in.pointer, kSplit, kMinDock);
			mTree.layout(area, kSplit);
			if (in.released) mResizeSplit = nullptr;
		}

		// --- advance a floating-container resize (bottom-right grip) ---
		if (mResizingFloat) {
			Rect& fr = mResizingFloat->floatingRect;
			fr.w = std::max(140.0f, in.pointer.x - fr.x);
			fr.h = std::max(90.0f, in.pointer.y - fr.y);
			if (in.released) mResizingFloat = nullptr;
		}

		// --- a pressed tab may become a detach-drag ---
		if (mPressTab) {
			if (in.released) mPressTab = nullptr;
			else if (glm::length(in.pointer - mPressPos) > 6.0f) {
				DockContainer* c = mPressTab;
				mPressTab = nullptr;
				c->floatingRect = { in.pointer.x - 40.0f, in.pointer.y - kTabH * 0.5f, 260.0f, 220.0f };
				c->dock = DockSide::Floating;
				mTree.remove(c->id());
				for (std::string& anchor : mSeedAnchor) if (anchor == c->id()) anchor.clear();
				structuralChange();
				mTree.layout(area, kSplit);
				mDragging = c;
				mDragOffset = { 40.0f, kTabH * 0.5f };
			}
		}

		// --- advance a floating drag, working out where it would land ---
		Rect previewRect{};
		DockDrop previewZone = DockDrop::None;
		DockNode* previewPane = nullptr;
		bool wholeEdge = false;
		if (mDragging) {
			mDragging->floatingRect.x = in.pointer.x - mDragOffset.x;
			mDragging->floatingRect.y = in.pointer.y - mDragOffset.y;

			// The window border takes precedence: that is how you get a strip across the
			// full width or height rather than a share of one pane.
			previewZone = windowEdgeAt(in.pointer, viewport, &previewRect);
			wholeEdge = (previewZone != DockDrop::None);
			if (!wholeEdge) {
				previewPane = mTree.leafAt(in.pointer);
				if (previewPane && previewPane->tabs.empty()) previewPane = nullptr;
				if (previewPane) previewZone = mTree.dropZoneAt(previewPane, in.pointer, &previewRect);
			}

			if (in.released) {
				DockContainer* dropped = mDragging;
				mDragging = nullptr;
				if (wholeEdge) {
					mTree.splitRoot(previewZone, dropped->id(), kEdgeShare);
					structuralChange();
				} else if (previewPane && previewZone != DockDrop::None) {
					mTree.dropOnto(previewPane, previewZone, dropped->id());
					structuralChange();
				}
				previewPane = nullptr;
				previewZone = DockDrop::None;
				mTree.layout(area, kSplit);
			}
		}

		bool busy = mDragging || mResizingFloat || mPressTab || mResizeSplit;

		// --- input: floating containers get first crack (topmost = last created) ---
		if (!busy && in.pressed) {
			for (auto it = mContainers.rbegin(); it != mContainers.rend(); ++it) {
				DockContainer* c = it->get();
				if (!c->visible || mTree.contains(c->id())) continue;
				const Rect r = c->floatingRect;
				const Rect grip{ r.x + r.w - 16.0f, r.y + r.h - 16.0f, 16.0f, 16.0f };
				if (grip.contains(in.pointer)) { mResizingFloat = c; busy = true; break; }
				if (c->closable) {
					const Rect xr{ r.x + r.w - 20.0f, r.y, 18.0f, kTabH };
					if (xr.contains(in.pointer)) { mPendingRemove = c; busy = true; break; }
				}
				const Rect titleBar{ r.x, r.y, r.w, kTabH };
				if (titleBar.contains(in.pointer)) {
					mDragging = c;
					mDragOffset = in.pointer - glm::vec2(r.x, r.y);
					busy = true;
					break;
				}
			}
		}

		// --- draw the docked panes ---
		std::vector<DockNode*> leaves;
		mTree.collectLeaves(leaves);
		for (DockNode* leaf : leaves) drawPane(gui, leaf, busy);

		// Where the scene shows through: the pane seeded Center, else the biggest one.
		const int centreHint = static_cast<int>(DockSide::Center);
		DockNode* centrePane = mSeedAnchor[centreHint].empty()
			? nullptr : mTree.leafFor(mSeedAnchor[centreHint]);
		if (!centrePane) centrePane = largestPane();
		mCenterBody = (centrePane && !centrePane->tabs.empty())
			? Rect{ centrePane->rect.x, centrePane->rect.y + kTabH,
			        centrePane->rect.w, centrePane->rect.h - kTabH }
			: area;

		// --- splitters between panes ---
		std::vector<DockNode*> splits;
		mTree.collectSplits(splits);
		for (DockNode* split : splits) {
			const Rect handle = split->handleRect(kSplit);
			const bool hot = (split == mResizeSplit) || (!busy && handle.contains(in.pointer));
			gui.drawRect(handle, hot ? rgba(90, 140, 240, 170) : rgba(70, 76, 90, 120));
			if (!busy && in.pressed && handle.contains(in.pointer)) {
				mResizeSplit = split;
				busy = true;
			}
		}

		// --- draw floating containers (creation order; last on top) ---
		for (auto& cp : mContainers) {
			DockContainer* c = cp.get();
			if (!c->visible || mTree.contains(c->id())) continue;
			const Rect r = c->floatingRect;
			const bool active = (c == mDragging);
			gui.drawRect(r, rgba(22, 24, 30, 245));
			gui.drawRect({ r.x, r.y, r.w, kTabH }, active ? rgba(58, 92, 168) : rgba(44, 48, 58));
			gui.drawText(c->title.c_str(), { r.x + 8.0f, r.y + 3.0f }, rgba(232, 234, 240));
			if (c->closable) {
				const Rect xr{ r.x + r.w - 20.0f, r.y, 18.0f, kTabH };
				const bool hot = xr.contains(in.pointer);
				gui.drawText("x", { xr.x + 6.0f, xr.y + 3.0f }, hot ? rgba(255, 180, 180) : rgba(200, 200, 210));
			}
			const Rect body{ r.x, r.y + kTabH, r.w, r.h - kTabH };
			gui.pushId(c->id().c_str()); // isolate this instance's widget state
			gui.pushClipRect(body);
			c->drawContents(gui, body);
			gui.popClipRect();
			gui.popId();

			// Resize grip: a small stair of marks in the bottom-right corner.
			for (int g = 0; g < 3; ++g) {
				const float o = 4.0f + g * 4.0f;
				gui.drawRect({ r.x + r.w - o - 2.0f, r.y + r.h - 6.0f, 2.0f, 2.0f }, rgba(120, 128, 145, 200));
				gui.drawRect({ r.x + r.w - 6.0f, r.y + r.h - o - 2.0f, 2.0f, 2.0f }, rgba(120, 128, 145, 200));
			}
		}

		// --- drop guidance while dragging ---
		if (mDragging) {
			// The four window edges, so docking across a whole side is discoverable.
			const Rect bands[4] = {
				{ 0.0f, 0.0f, kEdgeBand, viewport.y },
				{ viewport.x - kEdgeBand, 0.0f, kEdgeBand, viewport.y },
				{ 0.0f, 0.0f, viewport.x, kEdgeBand },
				{ 0.0f, viewport.y - kEdgeBand, viewport.x, kEdgeBand },
			};
			const DockDrop bandSide[4] = { DockDrop::Left, DockDrop::Right, DockDrop::Top, DockDrop::Bottom };
			for (int i = 0; i < 4; ++i) {
				const bool hot = wholeEdge && (bandSide[i] == previewZone);
				gui.drawRect(bands[i], hot ? rgba(90, 140, 240, 150) : rgba(60, 68, 90, 90));
			}
			// The pane under the pointer, and the share of it the drop would take.
			if (previewPane) gui.drawRect(previewPane->rect, rgba(90, 140, 240, 30));
			if (previewZone != DockDrop::None) gui.drawRect(previewRect, rgba(90, 140, 240, 90));
		}

		applyPendingRemovals(); // erase any container closed this frame
	}
}
