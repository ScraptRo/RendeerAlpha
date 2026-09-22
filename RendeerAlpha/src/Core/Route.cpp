#include <Core/Route.h>
#include <Core/Svg.h>
#include <Logger/Logger.h>

#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace RDA {
	namespace Route {

		struct Shape {
			// Flattened to a polyline, and each point's distance from the start alongside
			// it. Sampling is then a search in `travelled` and one lerp -- which is what
			// makes "half way along" mean half the distance rather than half the parameter.
			std::vector<float> xy;
			std::vector<float> travelled;
			float length = 0.0f;
		};

		namespace {
			// Half a pixel would be right if a route were drawn, but a route is *followed*
			// -- the error is in where something is, not in how an edge looks, and a
			// hundredth of the route's own size is well under what an eye can see moving.
			constexpr float kTolerance = 0.01f;

			std::mutex mLock;
			std::unordered_map<std::string, std::unique_ptr<Shape>> mByText;
		}

		const Shape* named(const std::string& path) {
			if (path.empty()) return nullptr;
			std::lock_guard<std::mutex> held(mLock);
			const auto found = mByText.find(path);
			if (found != mByText.end()) return found->second.get();

			// A route that will not parse is remembered as one that will not, so a layout
			// with a typo in it says so once rather than once a frame.
			auto shape = std::make_unique<Shape>();
			std::vector<float> points;
			if (!Svg::flattenPath(path, kTolerance, points) || points.size() < 4) {
				RDA_LOG_WARNING("route: '" << path << "' is not a path this engine can "
				                              "follow, so whatever asked for it travels in "
				                              "a straight line");
				mByText[path] = nullptr;
				return nullptr;
			}

			shape->xy = std::move(points);
			shape->travelled.reserve(shape->xy.size() / 2);
			shape->travelled.push_back(0.0f);
			for (size_t i = 2; i < shape->xy.size(); i += 2) {
				const float dx = shape->xy[i] - shape->xy[i - 2];
				const float dy = shape->xy[i + 1] - shape->xy[i - 1];
				shape->length += std::sqrt(dx * dx + dy * dy);
				shape->travelled.push_back(shape->length);
			}
			Shape* out = shape.get();
			mByText[path] = std::move(shape);
			return out;
		}

		void at(const Shape& shape, float progress, float& x, float& y) {
			if (shape.xy.size() < 4) { x = y = 0.0f; return; }
			if (progress <= 0.0f || shape.length <= 0.0f) {
				x = shape.xy[0];
				y = shape.xy[1];
				return;
			}
			if (progress >= 1.0f) {
				x = shape.xy[shape.xy.size() - 2];
				y = shape.xy[shape.xy.size() - 1];
				return;
			}

			const float wanted = progress * shape.length;
			// Binary search rather than a walk: a long route flattens to hundreds of
			// points and this is asked once per moving widget per frame.
			size_t low = 0, high = shape.travelled.size() - 1;
			while (low + 1 < high) {
				const size_t mid = (low + high) / 2;
				if (shape.travelled[mid] <= wanted) low = mid; else high = mid;
			}
			const float span = shape.travelled[high] - shape.travelled[low];
			const float t = span > 0.0f ? (wanted - shape.travelled[low]) / span : 0.0f;
			const size_t a = low * 2, b = high * 2;
			x = shape.xy[a] + (shape.xy[b] - shape.xy[a]) * t;
			y = shape.xy[a + 1] + (shape.xy[b + 1] - shape.xy[a + 1]) * t;
		}

		void from(const Shape& shape, float& x, float& y) {
			if (shape.xy.size() < 2) { x = y = 0.0f; return; }
			x = shape.xy[0];
			y = shape.xy[1];
		}

		void chord(const Shape& shape, float& dx, float& dy) {
			if (shape.xy.size() < 4) { dx = dy = 0.0f; return; }
			dx = shape.xy[shape.xy.size() - 2] - shape.xy[0];
			dy = shape.xy[shape.xy.size() - 1] - shape.xy[1];
		}
	}
}
