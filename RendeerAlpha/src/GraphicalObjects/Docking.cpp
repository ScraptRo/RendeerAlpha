#include <GraphicalObjects/Docking.h>
#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <cstdlib>

namespace RDA {

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
		return adopt(std::make_unique<DockContainer>(id, title));
	}

	DockContainer* DockSpace::adopt(std::unique_ptr<DockContainer> container) {
		if (!container) return nullptr;
		structuralChange();
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
	DockDrop DockSpace::windowEdgeAt(glm::vec2 p, const Rect& area, Rect* previewOut) const {
		if (!area.contains(p)) return DockDrop::None;
		const float right = area.x + area.w, bottom = area.y + area.h;
		DockDrop where = DockDrop::None;
		if      (p.x < area.x + kEdgeBand) where = DockDrop::Left;
		else if (p.x > right - kEdgeBand)  where = DockDrop::Right;
		else if (p.y < area.y + kEdgeBand) where = DockDrop::Top;
		else if (p.y > bottom - kEdgeBand) where = DockDrop::Bottom;
		if (where == DockDrop::None || !previewOut) return where;

		const float ew = area.w * kEdgeShare, eh = area.h * kEdgeShare;
		switch (where) {
		case DockDrop::Left:   *previewOut = { area.x, area.y, ew, area.h }; break;
		case DockDrop::Right:  *previewOut = { right - ew, area.y, ew, area.h }; break;
		case DockDrop::Top:    *previewOut = { area.x, area.y, area.w, eh }; break;
		default:               *previewOut = { area.x, bottom - eh, area.w, eh }; break;
		}
		return where;
	}

	// ---- painting one pane ---------------------------------------------------------
	void DockSpace::drawPane(Gui& gui, DockNode* leaf, bool& busy) {
		if (!leaf || leaf->tabs.empty()) return;

		// Where the tree put it, and where it is drawn on its way there.
		//
		// A pane keyed by the panel it holds, so when that panel is docked somewhere else
		// the entry follows it and the pane glides from its old home to its new one --
		// which is the whole of "the panel moved" rather than "one pane vanished and
		// another appeared".
		//
		// Never while something is being dragged. A splitter drag recomputes these rects
		// from where the pointer is every frame, and a panel being carried follows the
		// hand; easing either means the thing you are holding trails you.
		Rect slot = leaf->rect;
		const DockStyle& chromeMotion = gui.theme().dock(kDefaultVariant);
		const float seconds = chromeMotion.motion.seconds;
		if (seconds > 0.0f) {
			const uint32_t key = gui.motionKey(leaf->tabs.front().c_str(), kMotionPlace);
			Motion& motion = gui.motion();
			if (mResizeSplit || mDragging) {
				// Tracked exactly while something is being dragged, not eased -- and
				// tracked rather than simply skipped, because a value nobody asks for is
				// dropped. Skipped, the glide after a drop would start from nothing: the
				// entry would be created fresh at the destination and already be there.
				motion.reset(key + 0u, slot.x);
				motion.reset(key + 1u, slot.y);
				motion.reset(key + 2u, slot.w);
				motion.reset(key + 3u, slot.h);
			} else {
				slot = Rect{
					motion.value(key + 0u, slot.x, seconds, chromeMotion.motion.curve),
					motion.value(key + 1u, slot.y, seconds, chromeMotion.motion.curve),
					motion.value(key + 2u, slot.w, seconds, chromeMotion.motion.curve),
					motion.value(key + 3u, slot.h, seconds, chromeMotion.motion.curve),
				};
			}
		}

		// The pane's own chrome follows the theme's default dock style; each tab is drawn
		// with its own container's variant, so two panels sharing a pane can look
		// different from one another.
		const DockStyle& pane = gui.theme().dock(kDefaultVariant);
		const float tabH = pane.tabHeight;
		if (slot.w < 8.0f || slot.h < tabH) return; // squeezed to nothing

		const GuiInput& in = gui.input();
		gui.drawRect(slot, pane.pane);
		gui.drawRect({ slot.x, slot.y, slot.w, tabH }, pane.tabStrip);

		float tx = slot.x;
		for (size_t i = 0; i < leaf->tabs.size(); ++i) {
			DockContainer* t = containerFor(leaf->tabs[i]);
			if (!t || !t->visible) continue;
			const DockStyle& s = gui.theme().dock(t->variant);
			const float tw = gui.measureText(t->title.c_str()) + s.tabPadding * 2.0f;
			const Rect tabR{ tx, slot.y, tw, tabH };
			const bool activeTab = (static_cast<int>(i) == leaf->active);
			// Keyed on the panel rather than on its position in the strip, so reordering
			// tabs does not make two of them trade colours on the way past each other.
			const uint32_t tabColour = gui.motion().colour(
				gui.motionKey(leaf->tabs[i].c_str(), kMotionFill),
				activeTab ? s.tabActive : s.tab, s.motion.seconds, s.motion.curve);
			if (s.radius > 0.0f) {
				gui.drawRectRounded(tabR, tabColour, s.radius);
			} else {
				gui.drawRect(tabR, tabColour);
			}
			gui.drawText(t->title.c_str(), { tabR.x + s.tabPadding, tabR.y + 3.0f }, s.tabText);
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
			const DockStyle& s = gui.theme().dock(act->variant);
			const Rect xr{ slot.x + slot.w - s.closeWidth - 2.0f, slot.y, s.closeWidth, tabH };
			const bool hot = xr.contains(in.pointer);
			const uint32_t closeColour = gui.motion().colour(
				gui.motionKey(act->id().c_str(), kMotionMark),
				hot ? s.closeHover : s.close, s.motion.seconds, s.motion.curve);
			gui.drawText("x", { xr.x + 6.0f, xr.y + 3.0f }, closeColour);
			if (!busy && in.pressed && hot) { mPendingRemove = act; busy = true; }
		}

		const Rect body{ slot.x, slot.y + tabH, slot.w, slot.h - tabH };
		act->computedRect = slot;
		gui.pushId(act->id().c_str()); // isolate this instance's widget state
		gui.pushClipRect(body);
		act->drawContents(gui, body);
		gui.popClipRect();
		gui.popId();
	}

	// ---- the frame -----------------------------------------------------------------
	void DockSpace::update(Gui& gui, const Rect& area) {
		// Anything a callback asked for last frame, created before this frame walks the
		// container list.
		if (!mPendingSpawns.empty()) {
			std::vector<std::string> pending;
			pending.swap(mPendingSpawns);
			for (const std::string& type : pending) spawn(type.c_str());
		}

		const GuiInput& in = gui.input();
		// The splitters and the drop guidance belong to the dock space rather than to any
		// one panel, so they take the theme's default dock style.
		const DockStyle& chrome = gui.theme().dock(kDefaultVariant);
		// Only the size, for the parts that ask how much room there is rather than where
		// it is: seeding a pane's share, and nothing else.
		const glm::vec2 viewport{ area.w, area.h };

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
				c->floatingRect = { in.pointer.x - 40.0f, in.pointer.y - chrome.tabHeight * 0.5f, 260.0f, 220.0f };
				c->dock = DockSide::Floating;
				c->fresh = false; // pulled out of a pane, not newly arrived
				mTree.remove(c->id());
				for (std::string& anchor : mSeedAnchor) if (anchor == c->id()) anchor.clear();
				structuralChange();
				mTree.layout(area, kSplit);
				mDragging = c;
				mDragOffset = { 40.0f, chrome.tabHeight * 0.5f };
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
			previewZone = windowEdgeAt(in.pointer, area, &previewRect);
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
				// What was drawn last frame rather than what was asked for: a panel that
				// is still arriving is not at its rect yet, and grabbing one has to agree
				// with where it looks like it is.
				const Rect r = (c->computedRect.w > 0.0f) ? c->computedRect : c->floatingRect;
				const Rect grip{ r.x + r.w - 16.0f, r.y + r.h - 16.0f, 16.0f, 16.0f };
				if (grip.contains(in.pointer)) { mResizingFloat = c; busy = true; break; }
				if (c->closable) {
					const Rect xr{ r.x + r.w - 20.0f, r.y, 18.0f, chrome.tabHeight };
					if (xr.contains(in.pointer)) { mPendingRemove = c; busy = true; break; }
				}
				const Rect titleBar{ r.x, r.y, r.w, chrome.tabHeight };
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
			? Rect{ centrePane->rect.x, centrePane->rect.y + chrome.tabHeight,
			        centrePane->rect.w, centrePane->rect.h - chrome.tabHeight }
			: area;

		// --- splitters between panes ---
		std::vector<DockNode*> splits;
		mTree.collectSplits(splits);
		for (DockNode* split : splits) {
			const Rect handle = split->handleRect(kSplit);
			const bool hot = (split == mResizeSplit) || (!busy && handle.contains(in.pointer));
			// A splitter has no id of its own, so its handle's position is its identity --
			// stable for as long as it is in the same place, which is as long as it needs
			// to be for a hover to fade.
			const uint32_t splitKey = static_cast<uint32_t>(handle.x) * 2654435761u
			                        ^ static_cast<uint32_t>(handle.y);
			gui.drawRect(handle, gui.motion().colour(splitKey ^ kMotionFill,
			                                         hot ? chrome.splitterHover : chrome.splitter,
			                                         chrome.motion.seconds, chrome.motion.curve));
			if (!busy && in.pressed && handle.contains(in.pointer)) {
				mResizeSplit = split;
				busy = true;
			}
		}

		// --- draw floating containers (creation order; last on top) ---
		for (auto& cp : mContainers) {
			DockContainer* c = cp.get();
			// Hidden, so the next time it is shown it arrives rather than being suddenly
			// there again. A docked one is drawn by its pane instead.
			if (!c->visible) { c->fresh = true; continue; }
			if (mTree.contains(c->id())) continue;
			const bool active = (c == mDragging);
			const DockStyle& s = gui.theme().dock(c->variant);
			const float seconds = s.motion.seconds;
			Motion& motion = gui.motion();

			// A floating panel keeps the same value a docked pane uses for its rect, so
			// the two are one continuous position. Dock it and the pane glides from where
			// the panel was let go of into the space it was given -- rather than the panel
			// disappearing here and a pane appearing over there.
			//
			// *Where* it is belongs to the hand and is tracked exactly, never eased: a
			// panel being carried that lagged the pointer would feel like rubber. How big
			// it is does not, and that is the other half of the same glide -- a panel
			// pulled out of a pane shrinks from that pane's size to a window's while you
			// carry it, which is the drop played backwards. The grip is the exception,
			// because there the hand is holding the size itself.
			Rect r = c->floatingRect;
			if (seconds > 0.0f) {
				const uint32_t key = gui.motionKey(c->id().c_str(), kMotionPlace);
				motion.reset(key + 0u, r.x);
				motion.reset(key + 1u, r.y);
				if (c == mResizingFloat) {
					motion.reset(key + 2u, r.w);
					motion.reset(key + 3u, r.h);
				} else {
					r.w = motion.value(key + 2u, r.w, seconds, s.motion.curve);
					r.h = motion.value(key + 3u, r.h, seconds, s.motion.curve);
				}
			}

			// Arriving: spawned, or restored from a saved arrangement, or shown again --
			// it fades and grows into place, which is the closing ghost's shrink played
			// the other way round.
			float arrive = 1.0f;
			if (seconds > 0.0f) {
				const uint32_t openKey = gui.motionKey(c->id().c_str(), kMotionOpen);
				if (c->fresh) motion.reset(openKey, 0.0f); // begins at its beginning
				arrive = motion.value(openKey, 1.0f, seconds, s.motion.curve);
			}
			c->fresh = false;
			if (arrive < 0.999f) {
				const float grow = 0.94f + arrive * 0.06f;
				r = Rect{ r.x + r.w * (1.0f - grow) * 0.5f,
				          r.y + r.h * (1.0f - grow) * 0.5f,
				          r.w * grow, r.h * grow };
				gui.pushOpacity(arrive); // the contents fade with the frame around them
			}
			c->computedRect = r; // what the next frame's input is tested against

			if (s.radius > 0.0f) gui.drawRectRounded(r, s.pane, s.radius);
			else                 gui.drawRect(r, s.pane);
			// The same value its tab uses once it is docked, so a panel dropped into a
			// pane does not also change colour in the moment it changes shape.
			gui.drawRect({ r.x, r.y, r.w, s.tabHeight },
			             motion.colour(gui.motionKey(c->id().c_str(), kMotionFill),
			                           active ? s.titleBarActive : s.titleBar,
			                           seconds, s.motion.curve));
			gui.drawText(c->title.c_str(), { r.x + s.tabPadding - 2.0f, r.y + 3.0f }, s.tabText);
			if (c->closable) {
				const Rect xr{ r.x + r.w - 20.0f, r.y, 18.0f, chrome.tabHeight };
				const bool hot = xr.contains(in.pointer);
				gui.drawText("x", { xr.x + 6.0f, xr.y + 3.0f },
				             motion.colour(gui.motionKey(c->id().c_str(), kMotionMark),
				                           hot ? s.closeHover : s.close,
				                           seconds, s.motion.curve));
			}
			const Rect body{ r.x, r.y + chrome.tabHeight, r.w, r.h - chrome.tabHeight };
			gui.pushId(c->id().c_str()); // isolate this instance's widget state
			gui.pushClipRect(body);
			c->drawContents(gui, body);
			gui.popClipRect();
			gui.popId();

			// Resize grip: a small stair of marks in the bottom-right corner.
			for (int g = 0; g < 3; ++g) {
				const float o = 4.0f + g * 4.0f;
				gui.drawRect({ r.x + r.w - o - 2.0f, r.y + r.h - 6.0f, 2.0f, 2.0f }, s.grip);
				gui.drawRect({ r.x + r.w - 6.0f, r.y + r.h - o - 2.0f, 2.0f, 2.0f }, s.grip);
			}

			if (arrive < 0.999f) gui.popOpacity();
		}

		// --- drop guidance while dragging ---
		//
		// It fades in with the drag and out again after the drop, rather than blinking on
		// the frame a panel is picked up. Kept painting for the length of the fade, which
		// is why this is asked every frame and not only while dragging: the guidance has
		// to outlive the drag that asked for it.
		const uint32_t guideKey = gui.motionKey("#dropguide", kMotionOpen);
		const float guide = gui.motion().value(guideKey, mDragging ? 1.0f : 0.0f,
		                                       chrome.motion.seconds, chrome.motion.curve);
		if (guide > 0.004f) {
			gui.pushOpacity(guide);
			// The four window edges, so docking across a whole side is discoverable.
			const Rect bands[4] = {
				{ area.x, area.y, kEdgeBand, area.h },
				{ area.x + area.w - kEdgeBand, area.y, kEdgeBand, area.h },
				{ area.x, area.y, area.w, kEdgeBand },
				{ area.x, area.y + area.h - kEdgeBand, area.w, kEdgeBand },
			};
			const DockDrop bandSide[4] = { DockDrop::Left, DockDrop::Right, DockDrop::Top, DockDrop::Bottom };
			for (int i = 0; i < 4; ++i) {
				const bool hot = wholeEdge && (bandSide[i] == previewZone);
				gui.drawRect(bands[i], hot ? chrome.dropBandHot : chrome.dropBand);
			}
			// The pane under the pointer, and the share of it the drop would take. Only
			// while there is still a drag to guide -- after the drop these have nothing
			// to point at, and the edges fade out on their own.
			if (mDragging) {
				if (previewPane) gui.drawRect(previewPane->rect, chrome.dropPane);
				if (previewZone != DockDrop::None) gui.drawRect(previewRect, chrome.dropPreview);
			}
			gui.popOpacity();
		}

		// --- panels on their way out ---
		//
		// Drawn last, so a closing panel fades over the panes that are already gliding
		// into the space it left. Chrome only: the container it belonged to is gone, and
		// what a panel looks like while it goes is its frame and its name.
		for (size_t i = 0; i < mClosing.size();) {
			ClosingPane& ghost = mClosing[i];
			const DockStyle& s = gui.theme().dock(ghost.variant);
			const float seconds = s.motion.seconds;
			if (seconds <= 0.0f) { mClosing.erase(mClosing.begin() + i); continue; }

			const uint32_t key = gui.motionKey(ghost.id.c_str(), kMotionCollapse);
			if (!ghost.started) {
				// From its beginning, not from whatever the pane that used to be here had
				// reached: this panel's place in the table is one it has just inherited.
				gui.motion().reset(key, 0.0f);
				ghost.started = true;
			}
			const float gone = gui.motion().value(key, 1.0f, seconds, s.motion.curve);
			if (gone >= 0.999f) { mClosing.erase(mClosing.begin() + i); continue; }

			// Shrinking a little as it fades, toward its own middle, so it reads as
			// leaving rather than as merely dimming.
			const float shrink = 1.0f - gone * 0.06f;
			const Rect r{
				ghost.rect.x + ghost.rect.w * (1.0f - shrink) * 0.5f,
				ghost.rect.y + ghost.rect.h * (1.0f - shrink) * 0.5f,
				ghost.rect.w * shrink, ghost.rect.h * shrink,
			};
			gui.pushOpacity(1.0f - gone);
			if (s.radius > 0.0f) gui.drawRectRounded(r, s.pane, s.radius);
			else                 gui.drawRect(r, s.pane);
			gui.drawRect({ r.x, r.y, r.w, s.tabHeight }, s.tabStrip);
			gui.drawText(ghost.title.c_str(),
			             { r.x + s.tabPadding, r.y + 3.0f }, s.tabText);
			gui.popOpacity();
			++i;
		}

		applyPendingRemovals(); // erase any container closed this frame
	}

	// ---- the dock space as a widget -----------------------------------------------

	DockHost::~DockHost() {
		// On the way out, including the teardown a hot reload does -- which is why an
		// arrangement survives editing the layout that declared it.
		if (!persist.empty()) mSpace.saveLayoutToFile(persist);
	}

	Widget* DockHost::addChild(std::unique_ptr<Widget> child) {
		if (dynamic_cast<DockContainer*>(child.get())) {
			// Ownership moves to the dock space, which is what positions it. The pointer
			// handed back is still a Widget*, so whatever built the tree goes on adding
			// this panel's contents to it without knowing any of that happened.
			return mSpace.adopt(std::unique_ptr<DockContainer>(
				static_cast<DockContainer*>(child.release())));
		}
		return Widget::addChild(std::move(child));
	}

	void DockHost::paint(Gui& gui, glm::vec2 origin) {
		const Rect area = placement(gui, origin);

		// A backdrop, if anything that is not a panel was put in here.
		paintChildren(gui, glm::vec2(area.x, area.y));

		if (!mOpened) {
			mOpened = true;
			mSpace.placeFloatingWithin(area);
			// After the panels exist, because a saved arrangement is matched to them by
			// id and one that arrives first has nothing to match against.
			if (!persist.empty()) mSpace.loadLayoutFromFile(persist);
		}

		// Clipped to itself. A panel dragged past the edge stops at it, which is what
		// makes this a dock area rather than a second window manager.
		gui.pushClipRect(area);
		mSpace.update(gui, area);
		gui.popClipRect();
	}
}
