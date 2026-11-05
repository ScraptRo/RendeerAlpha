#include "TestHarness.h"

#include <GraphicalSrc/FontAtlas.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// A size nobody baked.
//
// A theme is the only place a size is written, and most of the languages a theme ships with
// cannot reach the C++ setting that lists what is baked at startup. So the atlas has to give a
// theme the size it wrote, whatever was baked, and these are what that has to mean: the same
// text as a baked size would be, drawn into the atlas, and a limit that says so when it is met.
//
// None of this needs a device. An atlas is a bitmap until something uploads it.
using namespace RDA;

namespace {

	const std::vector<float> kStartup = { 18.0f, 12.0f, 15.0f, 22.0f, 28.0f, 36.0f };

	bool bakeInto(FontAtlas& atlas, const std::vector<float>& sizes, float scale = 1.0f) {
		atlas.setScale(scale);
		return atlas.bake(RDA_TEST_FONT, sizes);
	}

	// Is anything drawn inside this quad's box? A glyph that was measured and never rasterised
	// has a box full of nothing.
	bool hasCoverage(const FontAtlas& atlas, const GlyphQuad& quad) {
		const int width = static_cast<int>(atlas.atlasWidth());
		const int height = static_cast<int>(atlas.atlasHeight());
		const int x0 = static_cast<int>(quad.u0 * width + 0.5f);
		const int x1 = static_cast<int>(quad.u1 * width + 0.5f);
		const int y0 = static_cast<int>(quad.v0 * height + 0.5f);
		const int y1 = static_cast<int>(quad.v1 * height + 0.5f);
		for (int y = y0; y < y1; ++y) {
			for (int x = x0; x < x1; ++x) {
				if (atlas.pixels()[static_cast<size_t>(y) * width + x] != 0) return true;
			}
		}
		return false;
	}
}

TEST(a_size_nobody_baked_is_the_size_that_was_asked_for) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));

	const int index = atlas.indexForSize(16.0f);
	CHECK(atlas.pixelHeight(index) == 16.0f);
	// And it is one size, however many times it is asked for.
	CHECK_EQ(atlas.indexForSize(16.0f), index);
}

TEST(a_baked_size_is_found_rather_than_added) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));

	const uint64_t before = atlas.revision();
	CHECK_EQ(atlas.indexForSize(22.0f), 3);
	CHECK_EQ(atlas.indexForSize(0.0f), 0);
	CHECK_EQ(atlas.indexForSize(-4.0f), 0);
	CHECK_EQ(atlas.indexForSize(std::numeric_limits<float>::quiet_NaN()), 0);
	CHECK_EQ(atlas.revision(), before);
}

TEST(text_at_an_added_size_is_the_text_a_baked_size_would_have_been) {
	FontAtlas added;
	CHECK(bakeInto(added, kStartup));
	FontAtlas baked;
	CHECK(bakeInto(baked, { 18.0f, 16.0f }));

	const int a = added.indexForSize(16.0f);
	const int b = baked.indexForSize(16.0f);
	CHECK_EQ(b, 1);

	CHECK(added.ascent(a) == baked.ascent(b));
	CHECK(added.lineAdvance(a) == baked.lineAdvance(b));
	const std::string text = "Ana are mere, b\xC4\x83iete";
	CHECK(added.textWidth(text, a) == baked.textWidth(text, b));
}

TEST(a_space_at_an_added_size_advances_and_draws_nothing) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));
	const int index = atlas.indexForSize(16.0f);

	float pen = 0.0f;
	GlyphQuad quad{};
	CHECK(!atlas.quadFor(static_cast<uint32_t>(' '), pen, 0.0f, quad, index));
	CHECK(pen > 0.0f);
}

TEST(an_added_size_is_drawn_into_the_atlas_and_asks_to_be_sent) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));
	const int index = atlas.indexForSize(16.0f);

	// The box is drawn when the size is made; that is not what this is asking about.
	(void)atlas.takeDirtyRows();
	const uint64_t before = atlas.revision();

	float pen = 0.0f;
	GlyphQuad quad{};
	CHECK(atlas.quadFor(static_cast<uint32_t>('W'), pen, 20.0f, quad, index));
	CHECK(quad.x1 > quad.x0);
	CHECK(quad.y1 > quad.y0);
	CHECK(hasCoverage(atlas, quad));

	// The GUI keeps last frame's geometry unless the revision moves, and the image on the GPU
	// stays as it was unless the rows are taken.
	CHECK(atlas.revision() > before);
	CHECK(!atlas.takeDirtyRows().empty());

	// The second time is a lookup.
	const uint64_t baked = atlas.revision();
	pen = 0.0f;
	CHECK(atlas.quadFor(static_cast<uint32_t>('W'), pen, 20.0f, quad, index));
	CHECK_EQ(atlas.revision(), baked);
	CHECK(atlas.takeDirtyRows().empty());
}

TEST(an_added_size_on_a_scaled_display_is_measured_in_units) {
	FontAtlas added;
	CHECK(bakeInto(added, kStartup, 1.25f));
	FontAtlas baked;
	CHECK(bakeInto(baked, { 18.0f, 16.0f }, 1.25f));

	const int a = added.indexForSize(16.0f);
	const int b = baked.indexForSize(16.0f);
	CHECK(added.pixelHeight(a) == 16.0f);
	CHECK(added.ascent(a) == baked.ascent(b));
	CHECK(added.textWidth("Theme", a) == baked.textWidth("Theme", b));
}

TEST(a_character_nothing_can_draw_is_the_box_at_an_added_size_too) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));
	const int index = atlas.indexForSize(16.0f);

	float pen = 0.0f;
	GlyphQuad quad{};
	CHECK(atlas.quadFor(0x10FFFDu, pen, 20.0f, quad, index)); // the last private-use codepoint
	CHECK(quad.x1 > quad.x0);
	CHECK(hasCoverage(atlas, quad));
	CHECK(pen > 0.0f);
}

TEST(a_size_too_tall_to_add_is_drawn_at_the_nearest_one) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));
	CHECK(atlas.pixelHeight(atlas.indexForSize(400.0f)) == 36.0f);
	// Every size is equally far from this one, so the base size is as near as any.
	CHECK_EQ(atlas.indexForSize(std::numeric_limits<float>::infinity()), 0);
	// And the limit is in pixels, because that is what the spare rows are measured in.
	FontAtlas scaled;
	CHECK(bakeInto(scaled, kStartup, 2.0f));
	CHECK(scaled.pixelHeight(scaled.indexForSize(120.0f)) == 36.0f);
	CHECK(scaled.pixelHeight(scaled.indexForSize(90.0f)) == 90.0f);
}

TEST(a_size_that_keeps_changing_runs_out_rather_than_filling_the_atlas) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));

	int added = 0;
	for (int step = 0; step < 100; ++step) {
		const float size = 40.0f + step * 0.5f;
		if (atlas.pixelHeight(atlas.indexForSize(size)) == size) ++added;
	}
	CHECK_EQ(added, FontAtlas::kMaxAddedSizes);

	// Everything already here still draws, and so does a character it has not met yet.
	float pen = 0.0f;
	GlyphQuad quad{};
	CHECK(atlas.quadFor(static_cast<uint32_t>('W'), pen, 20.0f, quad, atlas.indexForSize(40.0f)));
	CHECK(hasCoverage(atlas, quad));
}

TEST(what_is_held_of_a_size_survives_another_being_added) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));

	const std::vector<BakedGlyph>& base = atlas.glyphs(0);
	for (int step = 0; step < 32; ++step) atlas.indexForSize(40.0f + step);
	CHECK(&atlas.glyphs(0) == &base);
}

TEST(baking_again_forgets_what_was_added) {
	FontAtlas atlas;
	CHECK(bakeInto(atlas, kStartup));
	for (int step = 0; step < 40; ++step) atlas.indexForSize(40.0f + step);

	CHECK(bakeInto(atlas, kStartup));
	CHECK(atlas.pixelHeight(atlas.indexForSize(16.0f)) == 16.0f);
}
