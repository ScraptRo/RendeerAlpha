#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// A filter over a picture, written as the few lines that are actually the filter.
//
// Applying a compute shader to an image in Vulkan is perhaps two hundred lines that are
// the same every time -- a descriptor set layout, a pool, a set, a pipeline layout, a
// pipeline, two image barriers, a dispatch, a fence -- wrapped around about four that say
// what the filter does. None of those two hundred lines is a decision anybody wants to
// make twice, and getting the barriers wrong is a validation error rather than a wrong
// picture, so it is not even a productive mistake.
//
// So the engine owns all of it and an effect is its shader. The contract is fixed:
//
//   src        a sampler2D of the first input
//   tap(i, at) the i'th input sampled at `at`, i in 0..3
//   dst        a writeonly image2D of the output
//   uv()       this invocation's position, 0..1
//   coord()    the same as integer pixels
//   size()     the output's size in pixels
//   param(i)   one of eight floats the caller passed, i in 0..7
//   store(c)   write the result for this invocation
//
// Up to four inputs. A filter with one uses `src` and never mentions the rest; one that
// blends two reads `tap(0, uv())` and `tap(1, uv())`. The output is the size of the
// **first** input, and the others are sampled in 0..1, so mixing sizes is a resize rather
// than a mistake -- which is what you want when one of them is a mask or a lookup.
//
// which means a greyscale filter is:
//
//   void main() {
//       vec4 c = texture(src, uv());
//       store(vec4(vec3(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722))), c.a));
//   }
//
// Source that already begins with `#version` is taken as a whole shader and nothing is
// prepended, for anyone who wants the bindings themselves. The declarations above are
// what the prelude provides, so such a shader has to declare them the same way.
namespace RDA {

	class Texture;

	// The eight floats an effect can be handed. Fixed length on purpose: it fits in the
	// push-constant block every Vulkan device is required to have, so there is no buffer
	// to allocate, no descriptor to update, and no size to get wrong.
	struct EffectParams {
		float value[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	};

	// How many pictures one filter may read. Four because a blend takes two, a masked
	// blend three, and beyond that the thing being described is a pipeline rather than a
	// filter -- and a pipeline is several effects chained, which already works.
	inline constexpr int kMaxEffectInputs = 4;

	class Effects {
	public:
		~Effects();

		// Compiles `glsl` and keeps it under `name`. Returns false and puts the compiler's
		// own message in the log if it will not build -- the message names the line, which
		// is the only thing that makes a shader error worth reading.
		//
		// Defining a name again replaces it. Must run on the loop thread.
		bool define(const std::string& name, const std::string& glsl);

		// Runs it over `sources` into `into`, all streams by name. `into` is made if it is
		// not there and re-made when the first source's size changes, so the caller never
		// has to size it.
		//
		// One to four sources. The destination may not be one of them: a compute shader
		// that reads and writes one image reads whatever its neighbours have already
		// written this dispatch, which is a different picture on every run. Refused rather
		// than left to surprise somebody.
		bool apply(const std::string& name, const std::vector<std::string>& sources,
		           const std::string& into, const EffectParams& params);

		// The one-source form, which is most of them.
		bool apply(const std::string& name, const std::string& source,
		           const std::string& into, const EffectParams& params) {
			return apply(name, std::vector<std::string>{ source }, into, params);
		}

		void forget(const std::string& name);
		bool has(const std::string& name) const { return mByName.count(name) != 0; }

		// Every pipeline released, while there is still a device to release them with.
		void clear();

	private:
		struct Effect;
		std::unordered_map<std::string, std::unique_ptr<Effect>> mByName;
	};

	Effects& effects();
}
