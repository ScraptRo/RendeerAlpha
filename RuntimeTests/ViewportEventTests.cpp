#include "TestHarness.h"

#include <GraphicalObjects/ViewportEvents.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

// What happens to a viewport, as a backend sees it: summed between looks, never queued.
//
// What these protect is the promise a pan, an orbit and a redraw are written against --
// that the movement taken is all the movement there was, however rarely somebody looked;
// that a click is not lost because a move came after it; and that a resize is neither lost
// nor allowed to move the pointer.
namespace {

	using namespace RDA;

	ViewportEvent moved(float x, float y, float dx, float dy, uint32_t held = 1) {
		ViewportEvent p;
		p.x = x; p.y = y; p.dx = dx; p.dy = dy;
		p.held = held;
		p.inside = true;
		p.hasPointer = true;
		return p;
	}

	ViewportEvent sized(float w, float h) {
		ViewportEvent e;
		e.width = w;
		e.height = h;
		e.hasSize = true;
		e.resized = true;
		return e;
	}
}

TEST(movement_and_the_wheel_add_up_until_taken) {
	ViewportEventQueue queue;
	queue.report("graph", moved(10, 10, 3, 1));
	queue.report("graph", moved(14, 12, 4, 2));
	ViewportEvent wheel;
	wheel.x = 14; wheel.y = 12; wheel.wheel = 1.0f; wheel.inside = true; wheel.hasPointer = true;
	queue.report("graph", wheel);
	queue.report("graph", wheel);

	ViewportEvent out;
	CHECK(queue.take("graph", out));
	CHECK_EQ(out.dx, 7.0f);
	CHECK_EQ(out.dy, 3.0f);
	CHECK_EQ(out.wheel, 2.0f);
	CHECK_EQ(out.x, 14.0f);
	CHECK_EQ(out.y, 12.0f);
}

TEST(taking_twice_gives_nothing_new_but_still_says_where_it_is) {
	ViewportEventQueue queue;
	queue.report("graph", moved(20, 30, 5, 5));
	ViewportEvent out;
	CHECK(queue.take("graph", out));

	CHECK(!queue.take("graph", out));
	CHECK_EQ(out.x, 20.0f);
	CHECK_EQ(out.y, 30.0f);
	CHECK_EQ(out.dx, 0.0f);
	CHECK_EQ(out.wheel, 0.0f);
	CHECK_EQ(out.held, 1u); // still held: that is a state, not an event
	CHECK_EQ(out.pressed, 0u);
}

TEST(a_click_survives_the_moves_that_follow_it) {
	ViewportEventQueue queue;
	ViewportEvent press;
	press.pressed = 1; press.held = 1; press.clicks = 2; press.inside = true; press.hasPointer = true;
	queue.report("graph", press);
	ViewportEvent release;
	release.released = 1; release.inside = true; release.hasPointer = true;
	queue.report("graph", release);
	queue.report("graph", moved(40, 40, 0, 0, 0));

	ViewportEvent out;
	CHECK(queue.take("graph", out));
	CHECK_EQ(out.pressed, 1u);
	CHECK_EQ(out.released, 1u);
	CHECK_EQ(out.clicks, 2); // a move after a double click is still a double click
	CHECK_EQ(out.held, 0u);
}

TEST(a_viewport_nobody_touched_has_nothing) {
	ViewportEventQueue queue;
	ViewportEvent out;
	out.x = 99.0f;
	CHECK(!queue.take("never", out));
	CHECK_EQ(out.x, 0.0f);
	CHECK(!out.inside);
}

TEST(waiting_names_the_viewport_and_times_out_when_quiet) {
	ViewportEventQueue queue;
	CHECK(queue.wait(0).empty());

	queue.report("cube", moved(1, 1, 1, 1));
	CHECK_STR(queue.wait(0), "cube");
	ViewportEvent out;
	queue.take("cube", out);
	CHECK(queue.wait(5).empty());
}

TEST(two_busy_viewports_are_answered_in_turn) {
	ViewportEventQueue queue;
	queue.report("a", moved(1, 1, 1, 1));
	queue.report("b", moved(1, 1, 1, 1));
	// Neither is taken, so both stay pending -- and the answer still alternates.
	const std::string first = queue.wait(0);
	const std::string second = queue.wait(0);
	CHECK(first != second);
}

TEST(a_waiting_thread_wakes_for_a_report_and_for_a_clear) {
	ViewportEventQueue queue;
	std::string answer = "unset";
	std::thread waiter([&] { answer = queue.wait(-1); });
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	queue.report("graph", moved(1, 1, 1, 1));
	waiter.join();
	CHECK_STR(answer, "graph");

	ViewportEvent out;
	queue.take("graph", out);
	std::atomic<bool> done{ false };
	std::thread forever([&] { answer = queue.wait(-1); done = true; });
	// Cleared until it lets go: a clear that lands before the thread has started waiting
	// is not the case under test, and must not turn into a test that hangs.
	while (!done) {
		queue.clear(); // shutdown: nobody is left waiting on a window that is gone
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	forever.join();
	CHECK(answer.empty());
}

TEST(a_resize_says_so_until_taken_and_keeps_the_latest_size) {
	ViewportEventQueue queue;
	queue.report("cube", sized(400, 300));
	queue.report("cube", sized(520, 300));   // a splitter dragged: only the last size matters
	ViewportEvent out;
	CHECK(queue.take("cube", out));
	CHECK(out.resized);
	CHECK_EQ(out.width, 520.0f);
	CHECK_EQ(out.height, 300.0f);

	// Taken: the size is still known, the news of it is not news any more.
	CHECK(!queue.take("cube", out));
	CHECK(!out.resized);
	CHECK_EQ(out.width, 520.0f);
}

TEST(a_resize_does_not_move_the_pointer) {
	ViewportEventQueue queue;
	queue.report("graph", moved(120, 80, 0, 0, 0));
	ViewportEvent out;
	queue.take("graph", out);

	queue.report("graph", sized(800, 600));
	CHECK(queue.take("graph", out));
	CHECK(out.resized);
	CHECK_EQ(out.x, 120.0f);   // where the pointer was, not 0, 0
	CHECK_EQ(out.y, 80.0f);
	CHECK(out.inside);

	// And the other way round: a pointer report keeps the size it was given.
	queue.report("graph", moved(10, 10, 0, 0, 0));
	CHECK(queue.take("graph", out));
	CHECK(!out.resized);
	CHECK_EQ(out.width, 800.0f);
}

TEST(a_resize_wakes_a_waiting_thread_like_the_pointer_does) {
	ViewportEventQueue queue;
	std::string answer = "unset";
	std::thread waiter([&] { answer = queue.wait(-1); });
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	queue.report("cube", sized(300, 200));
	waiter.join();
	CHECK_STR(answer, "cube");
}
