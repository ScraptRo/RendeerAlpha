#include "TestHarness.h"
#include <GraphicalObjects/Widget.h>
#include <Layout/ThemeSchema.h>
#include <string_view>

// How a stack decides what a child asks for, with nothing measured.
//
// The measuring itself needs a device, a font and a window, and none of that is what goes
// wrong. What goes wrong is the rule: which of the four answers a declaration means. That
// part is arithmetic over a SizeSpec, so it is tested here as arithmetic.
using namespace RDA;
using namespace RDA::Layout;

namespace {
	SizeSpec unstated() { return SizeSpec{}; } // what a widget has when the layout said nothing
}

TEST(a_declared_size_is_taken_as_written) {
	CHECK(mainAxisSource(SizeSpec::fixed(110.0f), 0.0f) == SizeSource::Declared);
	CHECK(mainAxisSource(SizeSpec::fixed(110.0f), 40.0f) == SizeSource::Declared);
}

TEST(a_widget_with_no_declaration_falls_back_to_its_rect) {
	CHECK(mainAxisSource(unstated(), 40.0f) == SizeSource::Rect);
}

TEST(saying_nothing_measures_content_rather_than_asking_for_none) {
	// The regression: a row of labels that declared no width each asked for zero, so the
	// cursor never advanced and every one of them was drawn at the same x.
	CHECK(mainAxisSource(unstated(), 0.0f) == SizeSource::Content);
}

TEST(content_is_measured_whatever_the_rect_says) {
	CHECK(mainAxisSource(SizeSpec::content(), 0.0f) == SizeSource::Content);
	CHECK(mainAxisSource(SizeSpec::content(), 90.0f) == SizeSource::Content);
}

TEST(fill_asks_for_nothing_and_takes_what_is_left) {
	CHECK(mainAxisSource(SizeSpec::fill(), 0.0f) == SizeSource::Fill);
	CHECK(mainAxisSource(SizeSpec::fill(), 90.0f) == SizeSource::Fill);
}

TEST(across_the_axis_saying_nothing_means_stretch_me) {
	// The opposite of the main axis, and deliberately so: the container hands out the
	// line's width, so an unstated cross size is not measured.
	CHECK(crossAxisSource(unstated()) == SizeSource::Declared);
	CHECK(crossAxisSource(SizeSpec::content()) == SizeSource::Content);
	CHECK(crossAxisSource(SizeSpec::fill()) == SizeSource::Fill);
}

TEST(bounds_still_apply_to_whatever_was_chosen) {
	const SizeSpec bounded = SizeSpec::content(20.0f, 60.0f);
	CHECK(bounded.clamp(5.0f, 500.0f) == 20.0f);
	CHECK(bounded.clamp(90.0f, 500.0f) == 60.0f);
	CHECK(bounded.clamp(40.0f, 500.0f) == 40.0f);
}

// ---- alignment ---------------------------------------------------------------------
//
// Where a thing sits inside the room it was given. Same arithmetic for a child inside a
// container and for text inside a widget, which is why the two spell it the same way.

namespace {
	RDA::Alignment at(RDA::Align word, float offset = 0.0f) { return RDA::Alignment{ word, offset }; }
}

TEST(a_child_that_named_no_alignment_takes_its_containers) {
	CHECK(resolveAlign(at(Align::Center), at(Align::Auto)).word == Align::Center);
	CHECK(resolveAlign(at(Align::Stretch), at(Align::Auto)).word == Align::Stretch);
}

TEST(a_child_that_named_one_overrides_its_container) {
	// The whole reason hAlignSelf exists: "all of them stretched, except that one".
	CHECK(resolveAlign(at(Align::Stretch), at(Align::Center)).word == Align::Center);
	CHECK(resolveAlign(at(Align::Center), at(Align::Stretch)).word == Align::Stretch);
}

TEST(a_childs_nudge_travels_with_its_word) {
	// The word and the number are one answer: a child that overrides its container
	// brings its own nudge, and does not inherit the container's.
	CHECK_EQ(resolveAlign(at(Align::Center, 10.0f), at(Align::Auto)).offset, 10.0f);
	CHECK_EQ(resolveAlign(at(Align::Center, 10.0f), at(Align::Start, 4.0f)).offset, 4.0f);
	CHECK_EQ(resolveAlign(at(Align::Center, 10.0f), at(Align::Start)).offset, 0.0f);
}

TEST(alignment_divides_the_room_left_over) {
	CHECK(crossOffsetFor(at(Align::Start), 200.0f, 60.0f) == 0.0f);
	CHECK(crossOffsetFor(at(Align::Center), 200.0f, 60.0f) == 70.0f);
	CHECK(crossOffsetFor(at(Align::End), 200.0f, 60.0f) == 140.0f);
}

TEST(a_stretched_child_is_never_moved) {
	// It is already the size of its container, so there is nothing left to divide -- and
	// a child that declined the stretch sits at the start rather than being centred.
	CHECK(crossOffsetFor(at(Align::Stretch), 200.0f, 60.0f) == 0.0f);
	CHECK(crossOffsetFor(at(Align::Stretch), 200.0f, 200.0f) == 0.0f);
}

TEST(something_too_big_starts_at_the_edge_rather_than_before_it) {
	// A negative offset would push the start out of view to centre the middle. What
	// overflows has to be the end: the beginning is the part worth being able to read.
	CHECK(crossOffsetFor(at(Align::Center), 100.0f, 240.0f) == 0.0f);
	CHECK(crossOffsetFor(at(Align::End), 100.0f, 240.0f) == 0.0f);
	CHECK(alignOffset(TextAlign::Center, 100.0f, 240.0f) == 0.0f);
	CHECK(alignOffset(TextAlign::End, 100.0f, 240.0f) == 0.0f);
}

TEST(text_is_placed_by_the_same_rule_as_a_child) {
	CHECK(alignOffset(TextAlign::Start, 200.0f, 60.0f) == 0.0f);
	CHECK(alignOffset(TextAlign::Center, 200.0f, 60.0f) == 70.0f);
	CHECK(alignOffset(TextAlign::End, 200.0f, 60.0f) == 140.0f);
}

TEST(a_limit_that_depends_on_the_room_wins_over_the_fixed_one) {
	SizeSpec spec = SizeSpec::content(0.0f, 400.0f);
	spec.maxOf = [](float available) { return available * 0.5f; };
	CHECK(spec.clamp(300.0f, 200.0f) == 100.0f);
}

// ---- the theme schema ----------------------------------------------------------------
//
// The names a theme may use, and the suggestion that turns a typo into a fix. The reader
// in GuiTheme.cpp applies these; this is the declaration both it and the type emitter
// agree with, so a case here is a case about all three.

TEST(every_theme_element_has_fields_and_a_description) {
	size_t count = 0;
	const ThemeElementDesc* elements = themeSchema(count);
	CHECK(count > 0);
	for (size_t i = 0; i < count; ++i) {
		CHECK(elements[i].name != nullptr && *elements[i].name != '\0');
		CHECK(elements[i].fieldCount > 0);
		CHECK(elements[i].doc != nullptr && *elements[i].doc != '\0');
	}
}

TEST(a_field_the_reader_applies_is_a_field_the_schema_knows) {
	// A sample from each element, chosen from what GuiTheme.cpp actually reads. A name
	// here that the schema has lost is the drift this table exists to prevent.
	const ThemeElementDesc* button = findThemeElement("button");
	CHECK(button != nullptr);
	CHECK(findThemeField(*button, "normal") != nullptr);
	CHECK(findThemeField(*button, "borderWidth") != nullptr);
	CHECK(findThemeField(*button, "fontSize") != nullptr);
	CHECK(findThemeField(*button, "weight") != nullptr);

	const ThemeElementDesc* field = findThemeElement("textfield");
	CHECK(field != nullptr);
	CHECK(findThemeField(*field, "scrollThumbHover") != nullptr);
	CHECK(findThemeField(*field, "highlightCurrentLine") != nullptr);
	CHECK(findThemeField(*field, "syntax") != nullptr);
	CHECK(findThemeField(*field, "language") != nullptr);
}

TEST(base_is_accepted_on_every_element) {
	size_t count = 0;
	const ThemeElementDesc* elements = themeSchema(count);
	for (size_t i = 0; i < count; ++i) {
		CHECK(findThemeField(elements[i], "base") != nullptr);
	}
}

TEST(a_name_no_element_takes_is_not_found) {
	const ThemeElementDesc* button = findThemeElement("button");
	CHECK(button != nullptr);
	CHECK(findThemeField(*button, "borderWith") == nullptr);
	// A real field of a different element is still not a field of this one, which is the
	// mistake a single flat list of names would let through.
	CHECK(findThemeField(*button, "checkInset") == nullptr);
	CHECK(findThemeElement("buttons") == nullptr);
}

TEST(a_typo_suggests_the_name_it_meant) {
	const ThemeElementDesc* button = findThemeElement("button");
	CHECK(button != nullptr);
	CHECK(nearestThemeField(*button, "borderWith") == "borderWidth");
	CHECK(nearestThemeField(*button, "radius6") == "radius");
	CHECK(nearestThemeField(*button, "hoverd") == "hovered");
	CHECK(nearestThemeElement("labels") == "label");
	CHECK(nearestThemeElement("textfeild") == "textfield");
}

TEST(a_word_that_is_not_a_typo_gets_no_suggestion) {
	// A wrong guess is worse than none: it sends someone to fix the wrong line.
	const ThemeElementDesc* button = findThemeElement("button");
	CHECK(button != nullptr);
	CHECK(nearestThemeField(*button, "elevation").empty());
	CHECK(nearestThemeElement("viewport").empty());
}

TEST(the_syntax_palette_has_its_own_names) {
	size_t count = 0;
	const ThemeFieldDesc* kinds = syntaxFields(count);
	CHECK(count > 0);
	bool foundKeyword = false;
	for (size_t i = 0; i < count; ++i) {
		if (std::string_view(kinds[i].name) == "keyword") foundKeyword = true;
		CHECK(kinds[i].type == ThemeFieldType::Colour);
	}
	CHECK(foundKeyword);

	// And they are not fields of the element that owns the palette: `keyword` belongs to
	// the nested object, so a colour written beside `background` is still a mistake.
	const ThemeElementDesc* field = findThemeElement("textfield");
	CHECK(field != nullptr);
	CHECK(findThemeField(*field, "keyword") == nullptr);
}

// ---- motion ---------------------------------------------------------------------------
//
// Animation with no device: a value going somewhere, stepped by a clock. Everything that
// decides whether an interface feels right is here, and none of it is about pixels.
#include <GraphicalObjects/Motion.h>

namespace {
	// One key is enough for these; the table is keyed by widget id in real use.
	constexpr uint32_t kKey = 1234u;

	// Runs `frames` steps of `dt` and returns where the value ended up.
	float run(Motion& motion, float target, float seconds, float dt, int frames) {
		float last = 0.0f;
		for (int i = 0; i < frames; ++i) {
			last = motion.value(kKey, target, seconds);
			motion.step(dt);
		}
		return last;
	}
}

TEST(a_value_starts_where_it_was_first_asked_for) {
	// Not at zero: a button appearing should be its colour, not fade up from black.
	Motion motion;
	CHECK(motion.value(kKey, 100.0f, 0.2f) == 100.0f);
	CHECK(!motion.moving());
}

TEST(a_new_target_is_moved_toward_rather_than_jumped_to) {
	Motion motion;
	motion.value(kKey, 0.0f, 0.2f);
	motion.step(0.016f);

	// The frame a target is set is the frame the trip starts, so on that frame the value
	// is still where it was: step() advances the clock at the top of a frame, and no time
	// has passed yet. One frame at 60Hz, and the alternative -- crediting a frame's worth
	// of time before it has happened -- makes every duration wrong by a frame.
	CHECK(motion.value(kKey, 100.0f, 0.2f) == 0.0f);

	motion.step(0.016f);
	const float moved = motion.value(kKey, 100.0f, 0.2f);
	CHECK(moved > 0.0f);    // it left
	CHECK(moved < 100.0f);  // and has not arrived
}

TEST(a_starting_animation_counts_as_moving_at_once) {
	// The frame that starts one is the frame that has to know: the window decides whether
	// to draw again after the widgets have painted, and a value that says it is settled
	// there never gets a second frame to move in.
	Motion motion;
	motion.value(kKey, 0.0f, 0.2f);
	motion.step(0.016f);
	CHECK(!motion.moving());

	motion.value(kKey, 100.0f, 0.2f);
	CHECK(motion.moving());
}

TEST(a_value_arrives_and_then_stops_asking_for_frames) {
	Motion motion;
	motion.value(kKey, 0.0f, 0.1f);
	motion.step(0.016f);
	const float ended = run(motion, 100.0f, 0.1f, 0.016f, 12);
	CHECK(ended == 100.0f);
	CHECK(!motion.moving());
}

TEST(zero_seconds_is_no_animation_at_all) {
	// What a theme that says nothing gets, and what every widget did before this existed.
	Motion motion;
	CHECK(motion.value(kKey, 0.0f, 0.0f) == 0.0f);
	CHECK(motion.value(kKey, 100.0f, 0.0f) == 100.0f);
	CHECK(!motion.moving());
	CHECK(motion.count() == 0); // and nothing is stored for it
}

TEST(an_interrupted_move_carries_on_from_where_it_reached) {
	// A pointer sweeping across a row of buttons leaves each fading from wherever it had
	// got to. Restarting from the old start instead would make them jump backwards.
	Motion motion;
	motion.value(kKey, 0.0f, 0.2f);
	motion.step(0.05f);
	const float partway = motion.value(kKey, 100.0f, 0.2f);
	motion.step(0.05f);
	const float underway = motion.value(kKey, 100.0f, 0.2f);
	CHECK(underway > partway);

	// Now send it somewhere else mid-flight. It must not snap back to 0 or to 100.
	const float turned = motion.value(kKey, 50.0f, 0.2f);
	CHECK(turned > 0.0f);
	CHECK(turned <= underway + 0.001f);
}

TEST(easing_out_covers_more_ground_early_than_late) {
	// The shape is the whole point: a response that decelerates reads as a response, and
	// one that moves at a constant speed reads as a machine.
	CHECK(ease(Easing::Out, 0.25f) > 0.25f);
	CHECK(ease(Easing::In, 0.25f) < 0.25f);
	CHECK(ease(Easing::Linear, 0.25f) == 0.25f);

	// Every curve starts at nothing and finishes finished, whatever it does between.
	for (Easing curve : { Easing::Linear, Easing::Out, Easing::In, Easing::InOut }) {
		CHECK(ease(curve, 0.0f) == 0.0f);
		CHECK(ease(curve, 1.0f) == 1.0f);
		CHECK(ease(curve, -1.0f) == 0.0f); // clamped, not extrapolated
		CHECK(ease(curve, 2.0f) == 1.0f);
	}
}

TEST(a_value_nobody_asks_for_is_forgotten) {
	// The table is the size of what is on screen, not of everything that ever was.
	Motion motion;
	motion.value(kKey, 0.0f, 0.2f);
	CHECK(motion.count() == 1);
	for (int i = 0; i < 5; ++i) { motion.step(0.016f); motion.forget(); }
	CHECK(motion.count() == 0);
}

TEST(a_frame_that_asked_nobody_forgets_nothing) {
	// A retained GUI walks its tree when something changes and reuses the geometry
	// otherwise. Ageing on a reused frame forgets the colour of a button that is sitting
	// there perfectly visible -- and then the pointer arriving finds no history to
	// animate from, which looks exactly like animation not working. It was.
	Motion motion;
	motion.value(kKey, 0.0f, 0.2f);
	CHECK(motion.count() == 1);
	for (int i = 0; i < 100; ++i) motion.step(0.016f); // frames, none of them walked
	CHECK(motion.count() == 1);

	// And the value it kept is what the next change animates from.
	motion.step(0.016f);
	CHECK(motion.value(kKey, 100.0f, 0.2f) == 0.0f);
	CHECK(motion.moving());
}

TEST(a_colour_is_mixed_per_channel_and_arrives_exactly) {
	// Packed colours have no meaningful values between them, so the mix is per channel --
	// and the ends have to be the exact colours the theme named, not near them.
	Motion motion;
	const uint32_t from = rgba(0, 0, 0, 255);
	const uint32_t to   = rgba(255, 128, 64, 255);
	CHECK(motion.colour(kKey, from, 0.1f) == from);
	motion.step(0.016f);

	CHECK(motion.colour(kKey, to, 0.1f) == from); // the trip starts here, at its start
	motion.step(0.016f);

	const uint32_t midway = motion.colour(kKey, to, 0.1f);
	CHECK(midway != from);
	CHECK(midway != to);
	CHECK(((midway >> 24) & 0xFFu) == 255u); // alpha is the same at both ends

	for (int i = 0; i < 10; ++i) { motion.step(0.016f); motion.colour(kKey, to, 0.1f); }
	CHECK(motion.colour(kKey, to, 0.1f) == to);
}

TEST(an_interrupted_colour_leaves_from_the_one_it_was_showing) {
	// A pointer moving fast enough that a fade has not finished before the next one
	// starts. Where it had got to was carried over as a *packed* blend of the two words:
	// that does give each channel its right value, but as a fraction, and the fraction of
	// one channel is worth up to 255 of the channel below it. Truncated back to a word it
	// poured green into red -- a grey-blue button flashed bright green for a frame or two
	// and then settled, which is what made it look random rather than broken.
	Motion motion;
	const uint32_t normal  = rgba(58, 62, 72);
	const uint32_t hovered = rgba(80, 86, 100);

	motion.colour(kKey, normal, 0.14f);   // sitting there
	motion.step(0.016f);
	motion.colour(kKey, hovered, 0.14f);  // the pointer arrives
	motion.step(0.05f);

	const uint32_t showing = motion.colour(kKey, hovered, 0.14f);
	// It leaves again before that arrived. The next colour has to be the one that was on
	// the screen a moment ago, not a new one.
	CHECK(motion.colour(kKey, normal, 0.14f) == showing);

	// And on the way back every channel stays between the two the theme named.
	for (int i = 0; i < 12; ++i) {
		const uint32_t now = motion.colour(kKey, normal, 0.14f);
		for (int shift = 0; shift < 24; shift += 8) {
			const uint32_t channel = (now >> shift) & 0xFFu;
			CHECK(channel >= ((normal >> shift) & 0xFFu));
			CHECK(channel <= ((hovered >> shift) & 0xFFu));
		}
		motion.step(0.016f);
	}
	CHECK(motion.colour(kKey, normal, 0.14f) == normal);
}

// ---- syntax highlighting --------------------------------------------------------------
//
// One data-driven lexer serves every language, and every language but "python" is data
// in a file. Data can be wrong in ways a compiler cannot see, and the way it went wrong
// was invisible: a field asking for a language nobody had registered fell back to plain
// text and said nothing, which looks exactly like a language with no keywords.
#include <GraphicalObjects/Syntax.h>

namespace {
	// The kind assigned to the first token beginning at `at`, or Plain if none does.
	TokenKind kindAt(const std::vector<Token>& tokens, int at) {
		for (const Token& token : tokens) {
			if (token.begin == at) return token.kind;
		}
		return TokenKind::Plain;
	}
}

TEST(python_is_the_only_language_a_fresh_registry_has) {
	SyntaxRegistry registry;
	CHECK(registry.has("python"));
	CHECK(!registry.has("cpp"));
	CHECK(!registry.has("javascript"));
}

TEST(a_field_asking_for_a_language_nobody_registered_gets_nothing) {
	// Nothing to highlight with is the honest answer; the warning that goes with it is
	// what stops this reading as "this language has no keywords". Asked twice because
	// the complaint is once per name and the answer must not change with it.
	SyntaxRegistry registry;
	CHECK(registry.forField("cpp") == nullptr);
	CHECK(registry.forField("cpp") == nullptr);
	CHECK(registry.forField("") == nullptr);
	CHECK(registry.forField("python") != nullptr);
}

TEST(the_languages_the_engine_ships_load_and_highlight) {
	SyntaxRegistry registry;
	CHECK(registry.loadFromFile(std::string(RDA_ENGINE_RES_DIR) + "/themes/languages.xml"));

	// Every language the file declares, including the two derived with base=.
	CHECK(registry.has("cpp"));
	CHECK(registry.has("glsl"));
	CHECK(registry.has("javascript"));
	CHECK(registry.has("lua"));

	const Language* cpp = registry.forField("cpp");
	CHECK(cpp != nullptr);

	//                     0123456789...
	const std::string line = "int main() { return 0; } // done";
	std::vector<Token> tokens;
	SyntaxRegistry::tokenize(*cpp, line, tokens);
	CHECK(kindAt(tokens, 0) == TokenKind::Type);       // int
	CHECK(kindAt(tokens, 4) == TokenKind::Function);   // main, an identifier being called
	CHECK(kindAt(tokens, 13) == TokenKind::Keyword);   // return
	CHECK(kindAt(tokens, 20) == TokenKind::Number);    // 0
	CHECK(kindAt(tokens, 25) == TokenKind::Comment);   // // done

	// javascript derives from cpp with base=, so it must have inherited the C comment
	// syntax while bringing its own keywords.
	const Language* js = registry.forField("javascript");
	CHECK(js != nullptr);
	CHECK(js->keywords.count("function") == 1);
	CHECK(cpp->keywords.count("function") == 0);
}

TEST(fading_scales_the_alpha_and_leaves_the_colour) {
	const uint32_t solid = rgba(40, 80, 160, 255);
	CHECK(fadeTo(solid, 1.0f) == solid);
	CHECK((fadeTo(solid, 0.0f) & 0x00FFFFFFu) == (solid & 0x00FFFFFFu));
	CHECK(((fadeTo(solid, 0.0f) >> 24) & 0xFFu) == 0u);
	CHECK(((fadeTo(solid, 0.5f) >> 24) & 0xFFu) == 128u);
}

TEST(one_step_cannot_swallow_a_whole_animation) {
	// An on-demand window wakes with seconds of wall-clock on it. Left unclamped, that
	// first step spends the whole duration and the transition never appears -- the
	// animation looks broken when what is wrong is the clock.
	Motion motion;
	motion.value(kKey, 0.0f, 0.2f);
	motion.step(0.016f);
	CHECK(motion.value(kKey, 100.0f, 0.2f) == 0.0f);

	motion.step(3.0f);            // three seconds idle, then a frame
	const float after = motion.value(kKey, 100.0f, 0.2f);
	CHECK(after > 0.0f);
	CHECK(after < 100.0f);        // still on its way, not already arrived
	CHECK(motion.moving());
}

// ---- sizes, and the offset a word may carry --------------------------------------------
//
// "content" and "fill" are not values a binding could do arithmetic on: one is a request
// to measure and the other a claim on room nobody has divided yet, and both are answered
// during layout. The offset rides along with the word instead, and is applied where every
// resolved size already passes through.

namespace {
	RDA::SizeSpec parsed(const char* word) {
		RDA::SizeSpec spec;
		RDA::SizeSpec::parse(word, spec);
		return spec;
	}
}

TEST(a_size_word_parses) {
	CHECK(parsed("content").mode == RDA::SizeSpec::Mode::Content);
	CHECK(parsed("fill").mode == RDA::SizeSpec::Mode::Fill);
	CHECK_EQ(parsed("fill").value, 1.0f);
	CHECK_EQ(parsed("fill:2").value, 2.0f);
}

TEST(a_size_word_may_carry_an_offset) {
	CHECK(parsed("content-23").mode == RDA::SizeSpec::Mode::Content);
	CHECK_EQ(parsed("content-23").offset, -23.0f);
	CHECK_EQ(parsed("content+8").offset, 8.0f);
	CHECK(parsed("fill-40").mode == RDA::SizeSpec::Mode::Fill);
	CHECK_EQ(parsed("fill-40").offset, -40.0f);
	CHECK_EQ(parsed("fill:2-10").value, 2.0f);
	CHECK_EQ(parsed("fill:2-10").offset, -10.0f);
}

TEST(a_size_word_that_means_nothing_is_refused) {
	RDA::SizeSpec spec;
	CHECK(!RDA::SizeSpec::parse("", spec));
	CHECK(!RDA::SizeSpec::parse("wide", spec));
	CHECK(!RDA::SizeSpec::parse("content-", spec));
	CHECK(!RDA::SizeSpec::parse("filling", spec));
	// Refused means "leave what was there alone", which is how an old blueprint keeps
	// laying out under a build that does not know one of its words.
	spec = RDA::SizeSpec::fixed(120.0f);
	CHECK(!RDA::SizeSpec::parse("nonsense", spec));
	CHECK_EQ(spec.value, 120.0f);
}

TEST(the_offset_is_applied_where_the_size_resolves) {
	RDA::SizeSpec spec = parsed("content-23");
	CHECK_EQ(spec.clamp(100.0f, 500.0f), 77.0f);

	// After the offset, not before: min and max bound the answer, which is what a
	// reader of "no smaller than 40" expects.
	spec.min = 90.0f;
	CHECK_EQ(spec.clamp(100.0f, 500.0f), 90.0f);

	// And never below zero, however far the offset reaches.
	RDA::SizeSpec deep = parsed("content-500");
	CHECK_EQ(deep.clamp(100.0f, 500.0f), 0.0f);
}

TEST(a_size_without_an_offset_is_unchanged) {
	CHECK_EQ(parsed("content").offset, 0.0f);
	CHECK_EQ(parsed("content").clamp(60.0f, 500.0f), 60.0f);
}

// ---- alignment is named for the axis ------------------------------------------------------
//
// The point of the rename: what a stack does with hAlign must not depend on which way it
// happens to be running, because `arrange` may be a binding and flip while it runs.

TEST(a_stack_reads_the_axis_the_reader_named) {
	const RDA::Alignment h = at(Align::Center), v = at(Align::End);

	// A column stacks downward, so horizontal is across it and vertical is along it.
	CHECK(RDA::crossAlignOf(true, h, v).word == Align::Center);
	CHECK(RDA::mainAlignOf(true, h, v).word == Align::End);

	// The same pair, flipped: each property keeps its meaning and the two swap roles,
	// which is exactly what the old cross-axis `align` could not do. A stack whose
	// `arrange` is a binding passes through this moment every time it changes shape.
	CHECK(RDA::crossAlignOf(false, h, v).word == Align::End);
	CHECK(RDA::mainAlignOf(false, h, v).word == Align::Center);
}

// ---- a word, and however far past it ----------------------------------------------------
//
// The same rule sizes follow: "center" is where it sits, "center+20" is twenty past that.
// It is also the only way to move a child inside a stack, which decides positions itself.

TEST(an_alignment_word_may_carry_a_nudge) {
	float offset = -1.0f;
	CHECK(RDA::splitAlignOffset("center", offset) == "center");
	CHECK_EQ(offset, 0.0f);

	CHECK(RDA::splitAlignOffset("center+20", offset) == "center");
	CHECK_EQ(offset, 20.0f);

	CHECK(RDA::splitAlignOffset("start-8", offset) == "start");
	CHECK_EQ(offset, -8.0f);

	// Not a number after the sign: the whole thing is the word, which no alignment
	// matches, so the property keeps whatever it had.
	CHECK(RDA::splitAlignOffset("center+", offset) == "center+");
	CHECK_EQ(offset, 0.0f);
}

TEST(the_nudge_moves_a_child_past_where_the_word_put_it) {
	// 100 wide inside 300: centred is 100, and twenty past that is 120.
	CHECK_EQ(RDA::crossOffsetFor(at(Align::Center), 300.0f, 100.0f), 100.0f);
	CHECK_EQ(RDA::crossOffsetFor(at(Align::Center, 20.0f), 300.0f, 100.0f), 120.0f);
	CHECK_EQ(RDA::crossOffsetFor(at(Align::End, -30.0f), 300.0f, 100.0f), 170.0f);

	// A child with no room to move still moves: the word is clamped, the nudge is not.
	CHECK_EQ(RDA::crossOffsetFor(at(Align::Center), 100.0f, 100.0f), 0.0f);
	CHECK_EQ(RDA::crossOffsetFor(at(Align::Center, 12.0f), 100.0f, 100.0f), 12.0f);
}

TEST(text_takes_the_same_nudge) {
	CHECK_EQ(RDA::alignOffset(RDA::TextAlign::Center, 200.0f, 100.0f), 50.0f);
	CHECK_EQ(RDA::alignOffset(RDA::TextAlign::Center, 200.0f, 100.0f, 8.0f), 58.0f);
	// Overflowing text still starts at the near edge, and the nudge still applies.
	CHECK_EQ(RDA::alignOffset(RDA::TextAlign::Center, 50.0f, 100.0f, 6.0f), 6.0f);
}
