#include <GraphicalObjects/Motion.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <cmath>

namespace RDA {

	namespace {
		// How many frames a value may go unasked-for before it is dropped. Two rather than
		// one, because a widget hidden for a single frame -- a tab mid-switch, a row
		// scrolled just off the end -- should not forget where it was.
		constexpr uint8_t kIdleFrames = 2;

		// The most time one step may account for.
		//
		// In an on-demand loop the frame time is wall-clock since the last frame, and a
		// window that has been idle wakes with seconds on the clock. Handing that to an
		// animation spends its whole duration in one step, so every transition started
		// from an idle window would arrive instantly -- which is exactly what happened,
		// and looked like animation being broken rather than time being wrong.
		//
		// Clamped to a thirtieth: a genuinely slow frame makes animations run slightly
		// slow, which nobody notices, instead of skipping them, which everybody does.
		constexpr float kMaxStep = 1.0f / 30.0f;

		float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

		// Colours are R8G8B8A8 in one word; interpolating them means doing it per channel,
		// because the packed integers between two colours are not colours between them.
		uint32_t mixColour(uint32_t a, uint32_t b, float t) {
			uint32_t out = 0;
			for (int shift = 0; shift < 32; shift += 8) {
				const float from = static_cast<float>((a >> shift) & 0xFFu);
				const float to = static_cast<float>((b >> shift) & 0xFFu);
				const float mixed = from + (to - from) * t;
				out |= static_cast<uint32_t>(std::lround(clamp01(mixed / 255.0f) * 255.0f)) << shift;
			}
			return out;
		}
	}

	float ease(Easing curve, float t) {
		t = clamp01(t);
		switch (curve) {
		case Easing::Out:   return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
		case Easing::In:    return t * t * t;
		case Easing::InOut: return t < 0.5f ? 4.0f * t * t * t
		                                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
		default:            return t;
		}
	}

	float shape(const Pace& pace, float t) {
		t = clamp01(t);
		if (!pace.custom) return ease(pace.named, t);

		// x(u) for the cubic through (0,0), (x1,y1), (x2,y2), (1,1).
		const auto axis = [](float a, float b, float u) {
			const float inv = 1.0f - u;
			return 3.0f * inv * inv * u * a + 3.0f * inv * u * u * b + u * u * u;
		};
		const auto slope = [](float a, float b, float u) {
			const float inv = 1.0f - u;
			return 3.0f * inv * inv * a + 6.0f * inv * u * (b - a) + 3.0f * u * u * (1.0f - b);
		};

		// Newton first, because it lands in two or three steps for the curves anybody
		// writes. Bisection when the slope goes flat, which is exactly where a pace that
		// holds still for a moment puts it -- and where Newton would step to infinity.
		float u = t;
		for (int i = 0; i < 8; ++i) {
			const float error = axis(pace.x1, pace.x2, u) - t;
			if (std::fabs(error) < 1e-5f) return axis(pace.y1, pace.y2, u);
			const float d = slope(pace.x1, pace.x2, u);
			if (std::fabs(d) < 1e-6f) break;
			u -= error / d;
			if (u < 0.0f || u > 1.0f) break;
		}
		float low = 0.0f, high = 1.0f;
		u = t;
		for (int i = 0; i < 24; ++i) {
			const float x = axis(pace.x1, pace.x2, u);
			if (std::fabs(x - t) < 1e-5f) break;
			if (x < t) low = u; else high = u;
			u = (low + high) * 0.5f;
		}
		return axis(pace.y1, pace.y2, u);
	}

	Pace paceFrom(const std::string& text, Pace fallback) {
		if (text.empty()) return fallback;
		if (text == "linear") return Pace{ Easing::Linear, false, 0, 0, 1, 1 };
		if (text == "out")    return Pace{ Easing::Out,    false, 0, 0, 1, 1 };
		if (text == "in")     return Pace{ Easing::In,     false, 0, 0, 1, 1 };
		if (text == "inOut" || text == "inout") {
			return Pace{ Easing::InOut, false, 0, 0, 1, 1 };
		}
		const size_t open = text.find('(');
		if (text.compare(0, 13, "cubic-bezier(") != 0 || open == std::string::npos) {
			return fallback;
		}
		float v[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		const char* at = text.c_str() + open + 1;
		for (int i = 0; i < 4; ++i) {
			char* stop = nullptr;
			const float parsed = std::strtof(at, &stop);
			if (stop == at) return fallback;
			v[i] = parsed;
			at = stop;
			while (*at == ',' || *at == ' ') ++at;
		}
		Pace out;
		out.custom = true;
		// x is time and time does not run backwards; y is free, and a y past 1 is the
		// overshoot that makes something arrive by settling into place.
		out.x1 = clamp01(v[0]);
		out.y1 = v[1];
		out.x2 = clamp01(v[2]);
		out.y2 = v[3];
		return out;
	}

	size_t Motion::step(float dt) {
		if (dt > kMaxStep) dt = kMaxStep;
		if (dt < 0.0f) dt = 0.0f;
		mMoving = 0;
		for (auto& entry : mValues) {
			Value& value = entry.second;
			if (value.settled) continue;
			value.elapsed += dt;
			if (value.elapsed >= value.duration) {
				value.elapsed = value.duration;
				value.settled = true;
			} else {
				++mMoving;
			}
		}
		return mMoving;
	}

	void Motion::forget() {
		for (auto it = mValues.begin(); it != mValues.end();) {
			// Nobody asked for this on a frame they could have: the widget is gone, hidden
			// or scrolled away. Kept briefly, then dropped, so the table is the size of
			// what is on screen rather than of everything that ever was.
			if (++it->second.idle > kIdleFrames) it = mValues.erase(it);
			else ++it;
		}
	}

	double Motion::position(const Value& value) {
		if (value.settled) return value.to;
		const float t = ease(value.curve, value.elapsed / value.duration);
		if (value.packedColour) {
			return static_cast<double>(mixColour(static_cast<uint32_t>(value.from),
			                                     static_cast<uint32_t>(value.to), t));
		}
		return value.from + (value.to - value.from) * t;
	}

	Motion::Value& Motion::track(uint32_t key, double target, float seconds, Easing curve,
	                             bool packedColour) {
		auto found = mValues.find(key);
		if (found == mValues.end()) {
			// First sight of this value: it starts where it was asked to be. Animating from
			// zero would mean every button fading up from black on the frame it appears.
			Value fresh;
			fresh.from = target;
			fresh.to = target;
			fresh.elapsed = 0.0f;
			fresh.duration = (std::max)(seconds, 0.0001f);
			fresh.curve = curve;
			fresh.settled = true;
			fresh.packedColour = packedColour;
			found = mValues.emplace(key, fresh).first;
		}
		Value& value = found->second;
		value.idle = 0;

		if (target != value.to) {
			// Somewhere else to go. It leaves from where it is now rather than from where
			// the last trip started, which is what makes an interrupted animation continue
			// instead of jumping back.
			//
			// Asked before the new curve and kind are adopted below, because where it has
			// got to is a fact about the journey it is on, not the one it is about to
			// start.
			value.from = position(value);
			value.to = target;
			value.elapsed = 0.0f;
			value.duration = (std::max)(seconds, 0.0001f);
			value.settled = seconds <= 0.0f;
			if (value.settled) value.from = target;
			// Counted here and not only in step(), because step() runs before any widget
			// has asked for anything. The frame that *starts* an animation is the one
			// that has to know it started -- otherwise the window sees nothing moving,
			// skips its next frame, and the animation never gets a second frame to
			// advance in. It would arrive instantly, one frame late.
			else ++mMoving;
		}
		value.curve = curve;
		value.packedColour = packedColour;
		return value;
	}

	void Motion::reset(uint32_t key, float value) {
		Value& stored = mValues[key];
		stored.from = value;
		stored.to = value;
		stored.elapsed = 0.0f;
		stored.duration = 0.0001f;
		stored.settled = true;
		stored.idle = 0;
	}

	float Motion::value(uint32_t key, float target, float seconds, Easing curve) {
		if (seconds <= 0.0f) return target; // no animation asked for, and none stored
		const Value& value = track(key, static_cast<double>(target), seconds, curve, false);
		if (value.settled) return static_cast<float>(value.to);
		return static_cast<float>(
			value.from + (value.to - value.from) * ease(value.curve, value.elapsed / value.duration));
	}

	float Motion::value(uint32_t key, float target, float seconds, const Pace& pace) {
		if (seconds <= 0.0f) return target;
		// Stored linear and shaped here, because the stored curve is one of four names and
		// a pace may be any cubic. The entry is only keeping where the journey started and
		// how far through it is; what that means is this function's business.
		const Value& value = track(key, static_cast<double>(target), seconds,
		                           Easing::Linear, false);
		if (value.settled) return static_cast<float>(value.to);
		const float t = shape(pace, value.elapsed / value.duration);
		return static_cast<float>(value.from + (value.to - value.from) * t);
	}

	glm::vec2 Motion::along(uint32_t key, glm::vec2 target, float seconds, const Pace& pace,
	                        const Route::Shape* route) {
		if (seconds <= 0.0f) return target;
		const Value& x = track(key + 0u, static_cast<double>(target.x), seconds,
		                       Easing::Linear, false);
		const Value& y = track(key + 1u, static_cast<double>(target.y), seconds,
		                       Easing::Linear, false);
		if (x.settled && y.settled) return target;

		const glm::vec2 from{ static_cast<float>(x.from), static_cast<float>(y.from) };
		const glm::vec2 to{ static_cast<float>(x.to), static_cast<float>(y.to) };
		// One clock for both halves: they were started together and given the same
		// duration, so either answers for the journey.
		const float t = shape(pace, x.elapsed / x.duration);

		const glm::vec2 straight = from + (to - from) * t;
		if (!route) return straight;

		float rx = 0.0f, ry = 0.0f;
		Route::chord(*route, rx, ry);
		const float routeSquared = rx * rx + ry * ry;
		const glm::vec2 move = to - from;
		if (routeSquared < 1e-9f || (move.x * move.x + move.y * move.y) < 1e-9f) {
			// A route whose ends meet has no direction to be turned to, and a journey
			// that goes nowhere has none to turn it to. Either way there is nothing to
			// fit, and a straight line is the honest answer.
			return straight;
		}

		// The turn and the scale that carry the route's chord onto the journey's, as one
		// complex division -- rotating and scaling being what multiplying by a complex
		// number is. Doing it this way is not a trick for its own sake: separating them
		// into an angle and a length means an atan2 and a pair of trig calls per frame per
		// widget, for the same two numbers.
		const float a = (move.x * rx + move.y * ry) / routeSquared;
		const float b = (move.y * rx - move.x * ry) / routeSquared;

		float px = 0.0f, py = 0.0f;
		Route::from(*route, px, py);
		float qx = 0.0f, qy = 0.0f;
		Route::at(*route, t, qx, qy);
		const float ox = qx - px, oy = qy - py;
		return { from.x + a * ox - b * oy, from.y + b * ox + a * oy };
	}

	uint32_t Motion::colour(uint32_t key, uint32_t target, float seconds, Easing curve) {
		if (seconds <= 0.0f) return target;
		// Stored as the packed word in `to`, and mixed per channel on the way out: the
		// float only ever holds an exact 32-bit value, never a blend of two.
		auto found = mValues.find(key);
		const bool fresh = found == mValues.end();
		Value& value = track(key, static_cast<double>(target), seconds, curve, true);
		if (fresh || value.settled) return target;
		const uint32_t from = static_cast<uint32_t>(value.from);
		return mixColour(from, target, ease(value.curve, value.elapsed / value.duration));
	}
}
