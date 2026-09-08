#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <cstdint>
#include <unordered_map>

// Values that move to where they are going instead of arriving there.
//
// The whole of "fluid" is this: a button's fill does not jump from one colour to another,
// a panel that has to move slides, and a thing that appears grows into place. None of that
// is a new kind of state -- it is the same value, arrived at over a few frames.
//
// So nothing here decides anything. A widget says what it wants to be and how long it may
// take; this remembers where it currently is and hands back the value for *this* frame.
// The widget is still written as if the value changed at once, which is what keeps every
// paint function readable.
//
// The rule that makes this affordable: an animation only exists while it is moving. A
// settled value is dropped, and a Gui with nothing in motion is a Gui that costs nothing
// and asks for no frames -- which is what an on-demand redraw loop needs to stay honest.
// A window with a hover fading out earns frames until it has faded, and then stops.
namespace RDA {

	// `t` from 0 to 1, in and out. Easing lives in GuiTypes.h, so a theme can name a
	// curve without depending on the thing that applies it.
	float ease(Easing curve, float t);

	// One animated value per key. Scalars and colours are the same table -- a colour is
	// interpolated per channel, but it settles and is evicted the same way.
	class Motion {
	public:
		// Advances every value by `dt`. Returns how many are still moving, which is what
		// decides whether this window needs another frame.
		size_t step(float dt);

		// Ages every value by one frame and drops the ones nobody asked for.
		//
		// Separate from step(), and called only on the frames the widget tree is actually
		// walked, because those are the only frames anyone *could* ask. A retained GUI
		// walks on change and reuses otherwise, so ageing on every frame forgot a button's
		// colour while it sat there -- and the pointer arriving then found no history to
		// animate from, which looked exactly like animation not working.
		void forget();

		// Where `key` is now, on its way to `target`. `seconds` is how long the whole trip
		// takes; zero means no animation at all, which is what a theme that says nothing
		// gets and what every widget did before this existed.
		//
		// A target that changes mid-flight is not a restart: the value keeps its current
		// position and heads somewhere else, so a pointer sweeping across a row of buttons
		// leaves them fading from wherever each of them had reached.
		float value(uint32_t key, float target, float seconds, Easing curve = Easing::Out);

		// Puts a value where it is told, settled, with no journey.
		//
		// For starting a transition rather than following one: something that has just
		// begun has to be at zero *now*, and asking to travel to zero would animate it
		// backwards through the transition it is about to play forwards.
		void reset(uint32_t key, float value);
		uint32_t colour(uint32_t key, uint32_t target, float seconds, Easing curve = Easing::Out);

		// True while anything is moving. The retained cache asks this: a frame that would
		// otherwise be skipped has to be walked while a colour is still on its way.
		bool moving() const { return mMoving > 0; }
		size_t count() const { return mValues.size(); }
		void clear() { mValues.clear(); mMoving = 0; }

	private:
		// Everything an animated value needs, and nothing else. `from` is where the trip
		// started, so the curve is applied to the whole journey rather than to each frame
		// -- easing a per-frame step would make every curve look like the same decay.
		struct Value {
			// double, not float: a colour is stored here as its packed 32-bit word, and a
			// float has 24 bits of mantissa -- it would not round-trip, and the "has the
			// target changed" comparison below would be fuzzy on the values it matters
			// most for. A double holds every uint32_t exactly.
			double  from = 0.0;
			double  to = 0.0;
			float   elapsed = 0.0f;
			float   duration = 0.0f;
			Easing  curve = Easing::Out;
			uint8_t idle = 0;    // frames since anyone asked; evicted once nobody does
			bool    settled = false;
			// Whether these two are packed colours rather than plain numbers. Only
			// position() cares, and it is the whole reason it exists.
			bool    packedColour = false;
		};

		// Where a value is *right now*, part-way along. One line for a number and not for
		// a colour: a packed colour is a base-256 number, so interpolating the word does
		// give each channel the right value -- but as a fraction, and the fraction of one
		// channel is worth up to 255 of the channel below it. Truncating that back to a
		// word pours green into red. Interrupt a fade with that as its new starting point
		// and the button flashes a colour from nowhere before settling.
		static double position(const Value& value);

		Value& track(uint32_t key, double target, float seconds, Easing curve,
		             bool packedColour);

		std::unordered_map<uint32_t, Value> mValues;
		size_t mMoving = 0;
	};
}
