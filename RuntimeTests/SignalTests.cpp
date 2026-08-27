#include "TestHarness.h"
#include <Core/Signals.h>

#include <algorithm>

// What a signal promises.
//
// These are the rules a compiled binding will lean on, so they are worth pinning down
// before anything depends on them. Two of them are the whole reason signals beat
// re-running a component: a write that changes nothing notifies nobody, and a write that
// does change something notifies only what actually reads it.
using namespace RDA;

namespace {
	// Each case starts from an empty table. The signals are process-wide, the way the
	// scene is, so a test that left state behind would be read by the next one.
	struct Fixture {
		Fixture()  { signals().clear(); }
		~Fixture() { signals().clear(); }
		Signals& operator()() const { return signals(); }
	};

	bool queued(ObserverId observer) {
		const std::vector<ObserverId>& dirty = signals().dirty();
		return std::find(dirty.begin(), dirty.end(), observer) != dirty.end();
	}

	size_t timesQueued(ObserverId observer) {
		const std::vector<ObserverId>& dirty = signals().dirty();
		return static_cast<size_t>(std::count(dirty.begin(), dirty.end(), observer));
	}
}

TEST(a_signal_can_be_defined_and_read_back) {
	Fixture fx;
	const uint32_t count = fx().define("count", 3.0);
	const uint32_t ready = fx().define("ready", true);
	const uint32_t label = fx().define("label", std::string_view("hello"));

	CHECK(fx().valid(count));
	CHECK_EQ(fx().number(count), 3.0);
	CHECK(fx().boolean(ready));
	CHECK(fx().text(label) == "hello");
	CHECK(fx().type(count) == SignalType::Number);
	CHECK(fx().type(label) == SignalType::Text);
	CHECK_EQ(fx().count(), size_t(3));
}

TEST(defining_the_same_name_twice_returns_the_same_signal) {
	Fixture fx;
	const uint32_t first = fx().define("count", 1.0);
	const uint32_t again = fx().define("count", 9.0);
	CHECK_EQ(first, again);
	CHECK_EQ(fx().count(), size_t(1));
	CHECK_EQ(fx().number(first), 9.0);
}

TEST(redefining_with_a_different_type_keeps_the_original) {
	Fixture fx;
	const uint32_t count = fx().define("count", 1.0);
	// A name that means two things is a bug; the first meaning wins and the value is
	// left alone rather than being reinterpreted as something it is not.
	const uint32_t same = fx().define("count", std::string_view("not a number"));
	CHECK_EQ(count, same);
	CHECK(fx().type(count) == SignalType::Number);
	CHECK_EQ(fx().number(count), 1.0);
}

TEST(an_unknown_name_is_not_found) {
	Fixture fx;
	fx().define("count", 1.0);
	CHECK_EQ(fx().find("count"), uint32_t(0));
	CHECK(fx().find("nothing") == kNoSignal);
	CHECK(!fx().valid(kNoSignal));
}

TEST(writing_a_signal_marks_its_observers) {
	Fixture fx;
	const uint32_t count = fx().define("count", 0.0);
	fx().observe(count, 7);

	CHECK(!fx().anyDirty());
	fx().set(count, 1.0);
	CHECK(fx().anyDirty());
	CHECK(queued(7));
	CHECK_EQ(fx().number(count), 1.0);
}

TEST(writing_the_same_value_is_not_a_change) {
	Fixture fx;
	const uint32_t count = fx().define("count", 5.0);
	fx().observe(count, 1);

	// The property that stops an interface rebuilding itself every frame: assigning what
	// is already there notifies nobody.
	fx().set(count, 5.0);
	CHECK(!fx().anyDirty());

	fx().set(count, 6.0);
	CHECK(fx().anyDirty());
	fx().clearDirty();
	fx().set(count, 6.0);
	CHECK(!fx().anyDirty());
}

TEST(only_the_observers_of_the_written_signal_are_marked) {
	Fixture fx;
	const uint32_t a = fx().define("a", 0.0);
	const uint32_t b = fx().define("b", 0.0);
	fx().observe(a, 1);
	fx().observe(b, 2);

	fx().set(a, 1.0);
	CHECK(queued(1));
	CHECK(!queued(2));
	CHECK_EQ(fx().dirty().size(), size_t(1));
}

TEST(an_observer_of_two_changed_signals_is_queued_once) {
	Fixture fx;
	const uint32_t a = fx().define("a", 0.0);
	const uint32_t b = fx().define("b", 0.0);
	fx().observe(a, 4);
	fx().observe(b, 4);

	fx().set(a, 1.0);
	fx().set(b, 1.0);
	// Two dependencies moved; the binding still only has to be evaluated once.
	CHECK_EQ(timesQueued(4), size_t(1));
}

TEST(observing_the_same_signal_twice_is_one_edge) {
	Fixture fx;
	const uint32_t a = fx().define("a", 0.0);
	fx().observe(a, 3);
	fx().observe(a, 3);

	fx().set(a, 1.0);
	CHECK_EQ(timesQueued(3), size_t(1));
}

TEST(forgetting_an_observer_stops_it_being_notified) {
	Fixture fx;
	const uint32_t a = fx().define("a", 0.0);
	fx().observe(a, 5);
	fx().forget(5);

	fx().set(a, 1.0);
	CHECK(!queued(5));
	CHECK(!fx().anyDirty());
}

TEST(forgetting_an_observer_removes_it_from_the_pending_list) {
	Fixture fx;
	const uint32_t a = fx().define("a", 0.0);
	fx().observe(a, 6);
	fx().set(a, 1.0);
	CHECK(queued(6));

	// The widget went away between the write and the drain. Evaluating its binding now
	// is exactly the use-after-free this is here to prevent.
	fx().forget(6);
	CHECK(!queued(6));
}

TEST(clearing_the_dirty_list_lets_the_next_change_through) {
	Fixture fx;
	const uint32_t a = fx().define("a", 0.0);
	fx().observe(a, 2);

	fx().set(a, 1.0);
	CHECK(queued(2));
	fx().clearDirty();
	CHECK(!fx().anyDirty());

	fx().set(a, 2.0);
	CHECK(queued(2));
}

TEST(a_text_signal_notifies_on_content_not_identity) {
	Fixture fx;
	const uint32_t label = fx().define("label", std::string_view("a"));
	fx().observe(label, 1);

	fx().set(label, std::string_view("a"));
	CHECK(!fx().anyDirty());
	fx().set(label, std::string_view("b"));
	CHECK(fx().anyDirty());
	CHECK(fx().text(label) == "b");
}

TEST(writing_through_the_wrong_type_does_nothing) {
	Fixture fx;
	const uint32_t count = fx().define("count", 1.0);
	fx().observe(count, 1);

	fx().set(count, std::string_view("two"));
	CHECK_EQ(fx().number(count), 1.0);
	CHECK(!fx().anyDirty());
}
