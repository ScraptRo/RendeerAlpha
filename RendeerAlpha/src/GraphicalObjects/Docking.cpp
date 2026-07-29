#include <GraphicalObjects/Docking.h>
#include <GraphicalObjects/Gui.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <cstdlib>

namespace RDA {

	static constexpr float kTabH = 22.0f;    // tab / title bar height
	static constexpr float kSplit = 6.0f;     // resize-handle thickness
	static constexpr float kMinDock = 90.0f;  // smallest a docked strip can be
	static constexpr float kIcon = 42.0f;     // drop-target icon size
	static constexpr float kIconGap = 8.0f;

	static bool isEdge(DockSide s) {
		return s == DockSide::Left || s == DockSide::Right || s == DockSide::Top || s == DockSide::Bottom;
	}

	// The 5 drop-target icon rects, arranged as a plus at the center of `viewport`.
	static void dropIcons(glm::vec2 viewport, Rect out[5], DockSide sides[5]) {
		glm::vec2 c = viewport * 0.5f;
		float s = kIcon, g = kIconGap;
		out[0] = { c.x - s * 0.5f, c.y - s * 0.5f, s, s };               sides[0] = DockSide::Center;
		out[1] = { c.x - s * 0.5f - (s + g), c.y - s * 0.5f, s, s };     sides[1] = DockSide::Left;
		out[2] = { c.x - s * 0.5f + (s + g), c.y - s * 0.5f, s, s };     sides[2] = DockSide::Right;
		out[3] = { c.x - s * 0.5f, c.y - s * 0.5f - (s + g), s, s };     sides[3] = DockSide::Top;
		out[4] = { c.x - s * 0.5f, c.y - s * 0.5f + (s + g), s, s };     sides[4] = DockSide::Bottom;
	}

	DockContainer* DockSpace::add(const char* id, const char* title) {
		mContainers.push_back(std::make_unique<DockContainer>(id, title));
		return mContainers.back().get();
	}
	DockContainer* DockSpace::find(const char* id) {
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
		if (mDragging == c) mDragging = nullptr;
		if (mPressTab == c) mPressTab = nullptr;
		for (int i = 0; i < 6; ++i) if (mActive[i] == c) mActive[i] = nullptr;
		mContainers.erase(
			std::remove_if(mContainers.begin(), mContainers.end(),
				[c](const std::unique_ptr<DockContainer>& p) { return p.get() == c; }),
			mContainers.end());
	}

	// ---- layout persistence -------------------------------------------------------
	std::string DockSpace::saveLayout() const {
		std::ostringstream out;
		out << "rda-dock 1\n";
		for (const auto& c : mContainers) {
			out << "c " << c->id() << ' ' << (c->type.empty() ? "-" : c->type) << ' '
				<< static_cast<int>(c->dock) << ' '
				<< c->floatingRect.x << ' ' << c->floatingRect.y << ' '
				<< c->floatingRect.w << ' ' << c->floatingRect.h << ' '
				<< c->dockSize << '\n';
		}
		for (int side = 1; side <= 5; ++side) {
			out << "s " << side << ' ' << mSlotSize[side] << ' '
				<< (mActive[side] ? mActive[side]->id() : std::string("-")) << '\n';
		}
		return out.str();
	}

	void DockSpace::loadLayout(const std::string& data) {
		std::istringstream in(data);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream ls(line);
			char type = 0;
			ls >> type;
			if (type == 'c') {
				std::string id, ctype; int side = 0; Rect fr{}; float ds = 0.0f;
				ls >> id >> ctype >> side >> fr.x >> fr.y >> fr.w >> fr.h >> ds;
				DockContainer* c = find(id.c_str());
				if (!c && ctype != "-") {
					// A spawned instance that no longer exists — recreate it from its type.
					c = spawnWithId(ctype.c_str(), id.c_str());
					std::size_t hash = id.rfind('#');
					if (hash != std::string::npos) {
						uint64_t n = std::strtoull(id.c_str() + hash + 1, nullptr, 10);
						if (n > mSpawnCounter) mSpawnCounter = n;
					}
				}
				if (c) {
					if (side >= 0 && side < static_cast<int>(DockSide::Count)) c->dock = static_cast<DockSide>(side);
					c->floatingRect = fr;
					c->dockSize = ds;
				}
			} else if (type == 's') {
				int side = 0; float size = 0.0f; std::string activeId;
				ls >> side >> size >> activeId;
				if (side >= 1 && side <= 5) {
					mSlotSize[side] = size;
					mActive[side] = (activeId != "-") ? find(activeId.c_str()) : nullptr;
				}
			}
			// Other lines (including the "rda-dock 1" header) are ignored.
		}
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

	DockSide DockSpace::dropTargetAt(glm::vec2 pointer, glm::vec2 viewport, Rect* previewOut) const {
		Rect icons[5]; DockSide sides[5];
		dropIcons(viewport, icons, sides);
		for (int i = 0; i < 5; ++i) {
			if (icons[i].contains(pointer)) {
				if (previewOut) {
					float ew = viewport.x * 0.30f, eh = viewport.y * 0.30f;
					switch (sides[i]) {
					case DockSide::Left:   *previewOut = { 0, 0, ew, viewport.y }; break;
					case DockSide::Right:  *previewOut = { viewport.x - ew, 0, ew, viewport.y }; break;
					case DockSide::Top:    *previewOut = { 0, 0, viewport.x, eh }; break;
					case DockSide::Bottom: *previewOut = { 0, viewport.y - eh, viewport.x, eh }; break;
					default:               *previewOut = mCenterBody.w > 0 ? mCenterBody : Rect{ ew, eh, viewport.x - 2 * ew, viewport.y - 2 * eh }; break;
					}
				}
				return sides[i];
			}
		}
		return DockSide::Floating;
	}

	void DockSpace::computeLayout(RDL::frame_arena& arena, glm::vec2 viewport) {
		// The tab lists are rebuilt from nothing every frame and read only within it, so
		// they come out of the frame arena: count each side, take one array per side, fill
		// it. No allocation once the arena has grown to a frame's worth.
		for (int i = 0; i < 6; ++i) { mTabs[i] = nullptr; mTabCount[i] = 0; }
		for (auto& cp : mContainers) {
			if (cp->visible && cp->dock != DockSide::Floating) ++mTabCount[static_cast<int>(cp->dock)];
		}
		int filled[6]{};
		for (int i = 0; i < 6; ++i) {
			if (mTabCount[i] > 0) mTabs[i] = arena.allocate<DockContainer*>(mTabCount[i]);
			if (!mTabs[i]) mTabCount[i] = 0; // out of memory: treat the side as empty
		}
		for (auto& cp : mContainers) {
			if (!cp->visible || cp->dock == DockSide::Floating) continue;
			int s = static_cast<int>(cp->dock);
			if (mTabs[s] && filled[s] < mTabCount[s]) mTabs[s][filled[s]++] = cp.get();
		}

		for (int side = 1; side <= 5; ++side) {
			DockContainer** tabs = mTabs[side];
			const int count = mTabCount[side];
			if (count == 0) { mActive[side] = nullptr; continue; }
			bool activeStillHere = false;
			for (int i = 0; i < count && !activeStillHere; ++i) activeStillHere = (tabs[i] == mActive[side]);
			if (!mActive[side] || !activeStillHere) mActive[side] = tabs[0];
			if (isEdge(static_cast<DockSide>(side)) && mSlotSize[side] <= 0.0f)
				mSlotSize[side] = tabs[0]->dockSize;
		}

		Rect area{ 0.0f, 0.0f, viewport.x, viewport.y };
		auto sizeFor = [&](int side, float span) { return std::clamp(mSlotSize[side], kMinDock, span - kMinDock); };
		int L = (int)DockSide::Left, R = (int)DockSide::Right, T = (int)DockSide::Top, B = (int)DockSide::Bottom, C = (int)DockSide::Center;

		if (mTabCount[L]) { float s = sizeFor(L, area.w); mSlotRect[L] = { area.x, area.y, s, area.h }; area.x += s; area.w -= s; }
		if (mTabCount[R]) { float s = sizeFor(R, area.w); mSlotRect[R] = { area.x + area.w - s, area.y, s, area.h }; area.w -= s; }
		if (mTabCount[T]) { float s = sizeFor(T, area.h); mSlotRect[T] = { area.x, area.y, area.w, s }; area.y += s; area.h -= s; }
		if (mTabCount[B]) { float s = sizeFor(B, area.h); mSlotRect[B] = { area.x, area.y + area.h - s, area.w, s }; area.h -= s; }
		mSlotRect[C] = area;
		mCenterBody = (mTabCount[C] == 0) ? area : Rect{ area.x, area.y + kTabH, area.w, area.h - kTabH };
	}

	void DockSpace::update(Gui& gui, glm::vec2 viewport) {
		const GuiInput& in = gui.input();

		// --- advance an in-progress resize ---
		if (mResizeSide != DockSide::Floating) {
			int s = static_cast<int>(mResizeSide);
			const Rect& r = mSlotRect[s];
			float maxW = viewport.x - kMinDock, maxH = viewport.y - kMinDock;
			switch (mResizeSide) {
			case DockSide::Left:   mSlotSize[s] = std::clamp(in.pointer.x - r.x, kMinDock, maxW); break;
			case DockSide::Right:  mSlotSize[s] = std::clamp((r.x + r.w) - in.pointer.x, kMinDock, maxW); break;
			case DockSide::Top:    mSlotSize[s] = std::clamp(in.pointer.y - r.y, kMinDock, maxH); break;
			case DockSide::Bottom: mSlotSize[s] = std::clamp((r.y + r.h) - in.pointer.y, kMinDock, maxH); break;
			default: break;
			}
			if (in.released) mResizeSide = DockSide::Floating;
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
				float fw = 260.0f, fh = 220.0f;
				c->floatingRect = { in.pointer.x - 40.0f, in.pointer.y - kTabH * 0.5f, fw, fh };
				c->dock = DockSide::Floating;
				mDragging = c;
				mDragOffset = { 40.0f, kTabH * 0.5f };
				mPressTab = nullptr;
			}
		}

		// --- advance a floating drag ---
		Rect previewRect{}; DockSide previewSide = DockSide::Floating;
		if (mDragging) {
			mDragging->floatingRect.x = in.pointer.x - mDragOffset.x;
			mDragging->floatingRect.y = in.pointer.y - mDragOffset.y;
			previewSide = dropTargetAt(in.pointer, viewport, &previewRect);
			if (in.released) {
				if (previewSide != DockSide::Floating) {
					mDragging->dock = previewSide;
					mActive[static_cast<int>(previewSide)] = mDragging;
				}
				mDragging = nullptr;
			}
		}

		computeLayout(gui.frameArena(), viewport);

		bool busy = mDragging || mResizingFloat || mPressTab || mResizeSide != DockSide::Floating;

		// --- input: floating containers get first crack (topmost = last created) ---
		if (!busy && in.pressed) {
			for (auto it = mContainers.rbegin(); it != mContainers.rend(); ++it) {
				DockContainer* c = it->get();
				if (!c->visible || c->dock != DockSide::Floating) continue;
				Rect r = c->floatingRect;
				Rect grip{ r.x + r.w - 16.0f, r.y + r.h - 16.0f, 16.0f, 16.0f };
				if (grip.contains(in.pointer)) { mResizingFloat = c; busy = true; break; }
				if (c->closable) {
					Rect xr{ r.x + r.w - 20.0f, r.y, 18.0f, kTabH };
					if (xr.contains(in.pointer)) { mPendingRemove = c; busy = true; break; }
				}
				Rect titleBar{ r.x, r.y, r.w, kTabH };
				if (titleBar.contains(in.pointer)) {
					mDragging = c;
					mDragOffset = in.pointer - glm::vec2(r.x, r.y);
					busy = true;
					break;
				}
			}
		}

		// --- draw docked slots (+ their tab / resize input) ---
		for (int side = 1; side <= 5; ++side) {
			if (mTabCount[side] == 0) continue;
			Rect slot = mSlotRect[side];
			gui.drawRect(slot, rgba(22, 24, 30, 245));
			gui.drawRect({ slot.x, slot.y, slot.w, kTabH }, rgba(30, 33, 42));

			float tx = slot.x;
			for (int ti = 0; ti < mTabCount[side]; ++ti) {
				DockContainer* t = mTabs[side][ti];
				float tw = gui.measureText(t->title.c_str()) + 20.0f;
				Rect tabR{ tx, slot.y, tw, kTabH };
				bool activeTab = (mActive[side] == t);
				gui.drawRect(tabR, activeTab ? rgba(58, 92, 168) : rgba(40, 44, 54));
				gui.drawText(t->title.c_str(), { tabR.x + 10.0f, tabR.y + 3.0f }, rgba(232, 234, 240));
				if (!busy && in.pressed && tabR.contains(in.pointer)) {
					mActive[side] = t;
					mPressTab = t;
					mPressPos = in.pointer;
					busy = true;
				}
				tx += tw;
			}

			DockContainer* actC = mActive[side];
			if (actC) {
				// Close (x) for the active container, at the far right of the tab bar.
				if (actC->closable) {
					Rect xr{ slot.x + slot.w - 20.0f, slot.y, 18.0f, kTabH };
					bool xhot = xr.contains(in.pointer);
					gui.drawText("x", { xr.x + 6.0f, xr.y + 3.0f }, xhot ? rgba(255, 180, 180) : rgba(200, 200, 210));
					if (!busy && in.pressed && xhot) { mPendingRemove = actC; busy = true; }
				}
				Rect body{ slot.x, slot.y + kTabH, slot.w, slot.h - kTabH };
				actC->computedRect = slot;
				gui.pushId(actC->id().c_str()); // isolate this instance's widget state
				gui.pushClipRect(body);
				actC->drawContents(gui, body);
				gui.popClipRect();
				gui.popId();
			}

			if (isEdge(static_cast<DockSide>(side))) {
				Rect handle{};
				switch (static_cast<DockSide>(side)) {
				case DockSide::Left:   handle = { slot.x + slot.w - kSplit, slot.y, kSplit, slot.h }; break;
				case DockSide::Right:  handle = { slot.x, slot.y, kSplit, slot.h }; break;
				case DockSide::Top:    handle = { slot.x, slot.y + slot.h - kSplit, slot.w, kSplit }; break;
				case DockSide::Bottom: handle = { slot.x, slot.y, slot.w, kSplit }; break;
				default: break;
				}
				gui.drawRect(handle, rgba(70, 76, 90, 120));
				if (!busy && in.pressed && handle.contains(in.pointer)) {
					mResizeSide = static_cast<DockSide>(side);
					busy = true;
				}
			}
		}

		// --- draw floating containers (creation order; last on top) ---
		for (auto& cp : mContainers) {
			DockContainer* c = cp.get();
			if (!c->visible || c->dock != DockSide::Floating) continue;
			Rect r = c->floatingRect;
			bool active = (c == mDragging);
			gui.drawRect(r, rgba(22, 24, 30, 245));
			gui.drawRect({ r.x, r.y, r.w, kTabH }, active ? rgba(58, 92, 168) : rgba(44, 48, 58));
			gui.drawText(c->title.c_str(), { r.x + 8.0f, r.y + 3.0f }, rgba(232, 234, 240));
			if (c->closable) {
				Rect xr{ r.x + r.w - 20.0f, r.y, 18.0f, kTabH };
				bool xhot = xr.contains(in.pointer);
				gui.drawText("x", { xr.x + 6.0f, xr.y + 3.0f }, xhot ? rgba(255, 180, 180) : rgba(200, 200, 210));
			}
			Rect body{ r.x, r.y + kTabH, r.w, r.h - kTabH };
			gui.pushId(c->id().c_str()); // isolate this instance's widget state
			gui.pushClipRect(body);
			c->drawContents(gui, body);
			gui.popClipRect();
			gui.popId();

			// Resize grip: a small stair of marks in the bottom-right corner.
			for (int g = 0; g < 3; ++g) {
				float o = 4.0f + g * 4.0f;
				gui.drawRect({ r.x + r.w - o - 2.0f, r.y + r.h - 6.0f, 2.0f, 2.0f }, rgba(120, 128, 145, 200));
				gui.drawRect({ r.x + r.w - 6.0f, r.y + r.h - o - 2.0f, 2.0f, 2.0f }, rgba(120, 128, 145, 200));
			}
		}

		// --- drop-target overlay while dragging ---
		if (mDragging) {
			if (previewSide != DockSide::Floating) gui.drawRect(previewRect, rgba(90, 140, 240, 70));
			Rect icons[5]; DockSide sides[5];
			dropIcons(viewport, icons, sides);
			for (int i = 0; i < 5; ++i) {
				bool hot = (sides[i] == previewSide);
				gui.drawRect(icons[i], hot ? rgba(90, 140, 240, 220) : rgba(40, 44, 54, 200));
				// inner marker so the icon reads as a dock zone
				gui.drawRect({ icons[i].x + 6, icons[i].y + 6, icons[i].w - 12, icons[i].h - 12 },
				             hot ? rgba(150, 190, 255, 240) : rgba(80, 86, 100, 220));
			}
		}

		applyPendingRemovals(); // erase any container closed this frame
	}
}
