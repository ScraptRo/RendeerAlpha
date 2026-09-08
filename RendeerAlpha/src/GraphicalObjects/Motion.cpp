#include <GraphicalObjects/Motion.h>

#include <algorithm>
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
