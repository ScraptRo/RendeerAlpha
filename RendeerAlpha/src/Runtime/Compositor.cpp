#include <Runtime/Compositor.h>
#include <algorithm>

namespace RDA::Runtime {

	namespace {
		constexpr float kTileGap = 8.0f;
		constexpr float kMinTile = 160.0f;

		// The overlap of two rects, or an empty rect when they do not meet.
		Rect intersect(const Rect& a, const Rect& b) {
			const float x0 = (std::max)(a.x, b.x);
			const float y0 = (std::max)(a.y, b.y);
			const float x1 = (std::min)(a.x + a.w, b.x + b.w);
			const float y1 = (std::min)(a.y + a.h, b.y + b.h);
			if (x1 <= x0 || y1 <= y0) return Rect{ x0, y0, 0.0f, 0.0f };
			return Rect{ x0, y0, x1 - x0, y1 - y0 };
		}
	}

	RuntimeCompositor::Placement* RuntimeCompositor::mutableFind(uint32_t clientId, uint32_t surfaceId) {
		for (Placement& p : mPlacements) {
			if (p.clientId == clientId && p.surfaceId == surfaceId) return &p;
		}
		return nullptr;
	}

	const RuntimeCompositor::Placement* RuntimeCompositor::find(uint32_t clientId,
	                                                            uint32_t surfaceId) const {
		return const_cast<RuntimeCompositor*>(this)->mutableFind(clientId, surfaceId);
	}

	void RuntimeCompositor::place(uint32_t clientId, uint32_t surfaceId, const Rect& rect) {
		if (Placement* existing = mutableFind(clientId, surfaceId)) { existing->rect = rect; return; }
		Placement p;
		p.clientId = clientId;
		p.surfaceId = surfaceId;
		p.rect = rect;
		mPlacements.push_back(p);
	}

	void RuntimeCompositor::setVisible(uint32_t clientId, uint32_t surfaceId, bool visible) {
		if (Placement* p = mutableFind(clientId, surfaceId)) p->visible = visible;
	}

	void RuntimeCompositor::forget(uint32_t clientId, uint32_t surfaceId) {
		mPlacements.erase(
			std::remove_if(mPlacements.begin(), mPlacements.end(),
				[&](const Placement& p) { return p.clientId == clientId && p.surfaceId == surfaceId; }),
			mPlacements.end());
	}

	void RuntimeCompositor::raise(uint32_t clientId, uint32_t surfaceId) {
		for (size_t i = 0; i < mPlacements.size(); ++i) {
			if (mPlacements[i].clientId != clientId || mPlacements[i].surfaceId != surfaceId) continue;
			Placement moved = mPlacements[i];
			mPlacements.erase(mPlacements.begin() + i);
			mPlacements.push_back(moved); // last drawn is topmost
			return;
		}
	}

	void RuntimeCompositor::autoPlace(const std::vector<RuntimeHost::Surface>& surfaces,
	                                  glm::vec2 viewport) {
		// A placement whose surface has gone would otherwise linger and be composed
		// against nothing, so the dead ones go first.
		mPlacements.erase(
			std::remove_if(mPlacements.begin(), mPlacements.end(), [&](const Placement& p) {
				for (const RuntimeHost::Surface& s : surfaces) {
					if (s.clientId == p.clientId && s.surfaceId == p.surfaceId) return false;
				}
				return true;
			}),
			mPlacements.end());

		// Anything unplaced gets a tile. Sized to what the client asked for, but never
		// wider than the share it can have, so a client cannot claim the whole window by
		// requesting an enormous surface.
		size_t unplaced = 0;
		for (const RuntimeHost::Surface& s : surfaces) {
			if (!find(s.clientId, s.surfaceId)) ++unplaced;
		}
		if (unplaced == 0) return;

		const size_t total = mPlacements.size() + unplaced;
		const float share = (viewport.x - kTileGap * (total + 1)) / static_cast<float>(total);
		const float tileW = (std::max)(kMinTile, share);
		const float tileH = (std::max)(kMinTile, viewport.y - kTileGap * 2.0f);

		float cursor = kTileGap + static_cast<float>(mPlacements.size()) * (tileW + kTileGap);
		for (const RuntimeHost::Surface& s : surfaces) {
			if (find(s.clientId, s.surfaceId)) continue;
			const float w = (std::min)(tileW, static_cast<float>(s.width));
			const float h = (std::min)(tileH, static_cast<float>(s.height));
			place(s.clientId, s.surfaceId, Rect{ cursor, kTileGap, w, h });
			cursor += tileW + kTileGap;
		}
	}

	const std::vector<RuntimeCompositor::Layer>& RuntimeCompositor::compose(
		const std::vector<RuntimeHost::Surface>& surfaces) {
		mLayers.clear();

		for (const Placement& placement : mPlacements) {
			if (!placement.visible || placement.rect.w <= 0.0f || placement.rect.h <= 0.0f) continue;

			const RuntimeHost::Surface* surface = nullptr;
			for (const RuntimeHost::Surface& s : surfaces) {
				if (s.clientId == placement.clientId && s.surfaceId == placement.surfaceId) {
					surface = &s;
					break;
				}
			}
			if (!surface || surface->draw.vertices.empty() || surface->draw.commands.empty()) continue;

			Layer layer;
			layer.placement = placement;
			layer.version = surface->drawVersion;

			// Vertices move by the surface's origin; indices are untouched because each
			// layer keeps its own vertex buffer.
			layer.draw.vertices.reserve(surface->draw.vertices.size());
			for (const GuiVertex& v : surface->draw.vertices) {
				layer.draw.vertices.push_back(GuiVertex{
					{ v.pos.x + placement.rect.x, v.pos.y + placement.rect.y }, v.uv, v.color });
			}
			layer.draw.indices = surface->draw.indices;

			layer.draw.commands.reserve(surface->draw.commands.size());
			for (const GuiDrawCmd& c : surface->draw.commands) {
				// The client's clip, moved into the window and then cut down to the area
				// it was actually given. This is the containment: whatever a client asks
				// for, it cannot draw outside its own rect.
				const Rect clientClip{ c.clip.x + placement.rect.x, c.clip.y + placement.rect.y,
				                       c.clip.z - c.clip.x, c.clip.w - c.clip.y };
				const Rect clipped = intersect(clientClip, placement.rect);
				if (clipped.w <= 0.0f || clipped.h <= 0.0f) continue; // nothing visible

				GuiDrawCmd out = c;
				out.clip = { clipped.x, clipped.y, clipped.x + clipped.w, clipped.y + clipped.h };
				layer.draw.commands.push_back(out);
			}
			if (layer.draw.commands.empty()) continue; // entirely clipped away

			mLayers.push_back(std::move(layer));
		}
		return mLayers;
	}

	const RuntimeCompositor::Placement* RuntimeCompositor::surfaceAt(glm::vec2 point) const {
		// Back to front in draw order, so the topmost wins.
		for (size_t i = mPlacements.size(); i-- > 0;) {
			const Placement& p = mPlacements[i];
			if (p.visible && p.rect.contains(point)) return &p;
		}
		return nullptr;
	}
}
