#include "TestHarness.h"
#include <Core/FileStamp.h>
#include <Core/Mailbox.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Two small things that were each written more than once and are now written once: asking
// whether a file changed, and a bounded queue that drops the oldest.
//
// The point of both is that every place using them behaves as it did, so the tests say what
// that behaviour is rather than how it happens to be built.
using namespace RDA;

namespace {

	namespace fs = std::filesystem;
	using namespace std::chrono_literals;

	// A file in the temp directory that is gone again when the test is, however it ends.
	struct Scratch {
		fs::path path;

		explicit Scratch(const char* leaf) : path(fs::temp_directory_path() / leaf) { remove(); }
		~Scratch() { remove(); }

		void remove() const {
			std::error_code ec;
			fs::remove(path, ec);
		}
		void write(const std::string& text) const {
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out << text;
		}
		std::string name() const { return path.string(); }
	};

	// Writes the file again until its stamp is not `before`. Filesystems do not agree on how
	// fine a timestamp is -- NTFS keeps a tenth of a microsecond, ext4 a nanosecond, and a
	// FAT volume two whole seconds -- so a test that writes twice and expects two different
	// times is a test of the disk. Twenty milliseconds apart is plenty where the clock is
	// fine; where it is coarse this keeps going until the clock ticks, which is bounded so a
	// stamp that really never moves fails instead of hanging.
	int64_t rewriteUntilMoved(const Scratch& file, int64_t before) {
		int64_t now = fileStamp(file.name());
		for (int attempt = 0; attempt < 150 && now == before; ++attempt) {
			std::this_thread::sleep_for(20ms);
			file.write("written again, attempt " + std::to_string(attempt));
			now = fileStamp(file.name());
		}
		return now;
	}

	// Sets a file's time outright, for the tests where the exact relation between two files'
	// times is the thing being checked. Whole multiples of ten seconds, which every
	// filesystem can hold.
	void setTime(const Scratch& file, fs::file_time_type when) {
		std::error_code ec;
		fs::last_write_time(file.path, when, ec);
	}
}

// ---- Mailbox ------------------------------------------------------------------------------

TEST(a_mailbox_gives_things_back_oldest_first) {
	Mailbox<int> box(8);
	CHECK(!box.push(1));
	CHECK(!box.push(2));
	CHECK(!box.push(3));
	CHECK_EQ(box.size(), size_t(3));

	int out = 0;
	CHECK(box.pop(out));
	CHECK_EQ(out, 1);
	CHECK(box.pop(out));
	CHECK_EQ(out, 2);

	// Pushing between pops goes to the back of what is left.
	CHECK(!box.push(4));
	CHECK(box.pop(out));
	CHECK_EQ(out, 3);
	CHECK(box.pop(out));
	CHECK_EQ(out, 4);
	CHECK_EQ(box.size(), size_t(0));
}

TEST(a_full_mailbox_drops_the_oldest_and_says_so) {
	Mailbox<int> box(3);
	CHECK(!box.push(1));
	CHECK(!box.push(2));
	CHECK(!box.push(3));

	// Exactly at the bound is still room for what is there; it is the next one that costs.
	CHECK_EQ(box.size(), size_t(3));
	CHECK(box.push(4));
	CHECK_EQ(box.size(), size_t(3));
	CHECK(box.push(5));
	CHECK_EQ(box.size(), size_t(3));

	// 1 and 2 are gone; the most recent intent is what is kept.
	int out = 0;
	CHECK(box.pop(out));
	CHECK_EQ(out, 3);
	CHECK(box.pop(out));
	CHECK_EQ(out, 4);
	CHECK(box.pop(out));
	CHECK_EQ(out, 5);
	CHECK(!box.pop(out));

	// Taking something out makes room again, so there is nothing to report.
	CHECK(!box.push(6));
}

TEST(popping_an_empty_mailbox_says_so_and_leaves_the_target_alone) {
	Mailbox<std::string> box(4);
	std::string out = "untouched";
	CHECK(!box.pop(out));
	CHECK_STR(out, "untouched");

	box.push("one");
	CHECK(box.pop(out));
	CHECK_STR(out, "one");
	CHECK(!box.pop(out));
	CHECK_STR(out, "one");
}

TEST(clearing_a_mailbox_empties_it_and_it_keeps_working) {
	Mailbox<int> box(3);
	box.push(1);
	box.push(2);
	box.push(3);
	box.clear();
	CHECK_EQ(box.size(), size_t(0));

	int out = 0;
	CHECK(!box.pop(out));

	// And a cleared box is not still full: the next push has room and reports no drop.
	CHECK(!box.push(7));
	CHECK(box.pop(out));
	CHECK_EQ(out, 7);
}

TEST(a_mailbox_with_no_room_at_all_keeps_the_newest_one) {
	// A bound of nothing cannot mean "keep nothing" and still be a queue; it keeps one.
	Mailbox<int> box(0);
	CHECK(!box.push(1));
	CHECK(box.push(2));
	int out = 0;
	CHECK(box.pop(out));
	CHECK_EQ(out, 2);
	CHECK(!box.pop(out));
}

TEST(a_mailbox_holds_things_that_cannot_be_copied) {
	Mailbox<std::unique_ptr<int>> box(2);
	box.push(std::make_unique<int>(10));
	box.push(std::make_unique<int>(20));
	CHECK(box.push(std::make_unique<int>(30)));

	std::unique_ptr<int> out;
	CHECK(box.pop(out));
	CHECK(out != nullptr);
	if (out) CHECK_EQ(*out, 20);
}

TEST(two_threads_can_fill_and_empty_a_mailbox_at_once) {
	// A smoke test, not a proof: one thread pushes a numbered run faster than the other can
	// take it, through a box far too small to hold it. Whatever happens, what comes out must
	// still be in the order it went in, nothing may be both delivered and reported dropped,
	// and the last thing pushed is never the one that is dropped.
	constexpr int kPushed = 20000;
	Mailbox<int> box(64);
	std::atomic<bool> finished{ false };
	std::atomic<int>  dropped{ 0 };

	std::vector<int> received;
	std::thread consumer([&] {
		int value = 0;
		for (;;) {
			// Asked before the pop, so an empty box after the producer said it was done is
			// empty for good.
			const bool done = finished.load();
			if (box.pop(value)) { received.push_back(value); continue; }
			if (done) break;
			std::this_thread::yield();
		}
	});

	std::thread producer([&] {
		for (int i = 1; i <= kPushed; ++i) {
			if (box.push(i)) dropped.fetch_add(1);
		}
		finished.store(true);
	});

	producer.join();
	consumer.join();

	CHECK_EQ(static_cast<int>(received.size()) + dropped.load(), kPushed);
	bool ascending = true;
	for (size_t i = 1; i < received.size(); ++i) {
		if (received[i] <= received[i - 1]) { ascending = false; break; }
	}
	CHECK(ascending);
	CHECK(!received.empty());
	if (!received.empty()) CHECK_EQ(received.back(), kPushed);
	CHECK_EQ(box.size(), size_t(0));
}

// ---- fileStamp ----------------------------------------------------------------------------

TEST(a_file_that_is_not_there_has_the_stamp_zero) {
	Scratch missing("rda_stamp_test_missing.txt");
	CHECK_EQ(fileStamp(missing.name()), int64_t(0));
	CHECK_EQ(fileStamp(""), int64_t(0));

	// A directory that does not exist along the way is the same answer, not an error.
	CHECK_EQ(fileStamp((missing.path.parent_path() / "rda_no_such_directory" / "x.txt").string()),
	         int64_t(0));
}

TEST(a_file_that_is_there_has_a_stamp_and_writing_it_changes_the_stamp) {
	Scratch file("rda_stamp_test_written.txt");
	file.write("first");
	const int64_t first = fileStamp(file.name());
	CHECK(first != 0);

	// Unchanged means the same number, however often it is asked.
	CHECK_EQ(fileStamp(file.name()), first);

	const int64_t second = rewriteUntilMoved(file, first);
	CHECK(second != first);
	CHECK(second != 0);
}

TEST(a_file_that_appears_later_reads_as_a_change) {
	// This is why zero is a fine "no file": nothing real is zero, so appearing moves it.
	Scratch file("rda_stamp_test_appears.txt");
	const int64_t before = fileStamp(file.name());
	CHECK_EQ(before, int64_t(0));
	file.write("here now");
	CHECK(fileStamp(file.name()) != before);
}

TEST(a_set_of_stamps_is_mixed_not_summed) {
	Scratch a("rda_stamps_test_a.txt");
	Scratch b("rda_stamps_test_b.txt");
	a.write("a");
	b.write("b");
	const std::vector<std::string> both = { a.name(), b.name() };
	const std::vector<std::string> swapped = { b.name(), a.name() };

	const fs::file_time_type base = fs::file_time_type::clock::now() - std::chrono::hours(1);

	// One file's time goes up by what the other's goes down by, so the sum of the two is
	// the same in both states. Summed, nothing would look like it had happened.
	setTime(a, base);
	setTime(b, base + 100s);
	const int64_t before = fileStamps(both);
	CHECK(fileStamp(a.name()) != fileStamp(b.name()));

	setTime(a, base + 50s);
	setTime(b, base + 50s);
	CHECK(fileStamps(both) != before);

	// And the order is part of the answer, as the count is.
	setTime(a, base);
	setTime(b, base + 100s);
	CHECK_EQ(fileStamps(both), before);
	CHECK(fileStamps(swapped) != before);
	CHECK(fileStamps({ a.name() }) != before);
	CHECK(fileStamps({}) != before);
}

TEST(an_empty_set_has_a_stamp_that_does_not_move) {
	CHECK_EQ(fileStamps({}), fileStamps({}));
	CHECK_EQ(fileStamps({}), int64_t(0));
}

TEST(a_file_added_to_a_set_changes_its_stamp_even_when_it_is_missing) {
	Scratch a("rda_stamps_test_member.txt");
	Scratch b("rda_stamps_test_absent.txt");
	a.write("a");
	const int64_t one = fileStamps({ a.name() });
	const int64_t two = fileStamps({ a.name(), b.name() });
	CHECK(one != two);
}

// ---- FileWatch ----------------------------------------------------------------------------

TEST(a_file_watch_reports_a_change_once_and_then_not_again) {
	Scratch file("rda_filewatch_test_once.txt");
	file.write("first");

	FileWatch watch;
	watch.watch({ file.name() });
	CHECK_EQ(watch.paths().size(), size_t(1));

	// Taken when it started watching, so "never looked at" is not a change.
	CHECK(!watch.changed());
	CHECK(!watch.changed());

	const int64_t before = fileStamp(file.name());
	rewriteUntilMoved(file, before);
	CHECK(watch.changed());

	// Once. The new stamp was recorded by being asked.
	CHECK(!watch.changed());
	CHECK(!watch.changed());

	// And a second edit is a second change.
	rewriteUntilMoved(file, fileStamp(file.name()));
	CHECK(watch.changed());
	CHECK(!watch.changed());
}

TEST(a_file_watch_over_several_files_notices_any_of_them) {
	Scratch a("rda_filewatch_test_a.txt");
	Scratch b("rda_filewatch_test_b.txt");
	a.write("a");
	b.write("b");

	FileWatch watch;
	watch.watch({ a.name(), b.name() });
	CHECK(!watch.changed());

	// The second one only.
	rewriteUntilMoved(b, fileStamp(b.name()));
	CHECK(watch.changed());
	CHECK(!watch.changed());

	// The first one only.
	rewriteUntilMoved(a, fileStamp(a.name()));
	CHECK(watch.changed());
	CHECK(!watch.changed());
}

TEST(a_file_watch_notices_a_file_appearing) {
	Scratch file("rda_filewatch_test_appears.txt");

	FileWatch watch;
	watch.watch({ file.name() });
	CHECK(!watch.changed());

	file.write("here now");
	CHECK(watch.changed());
	CHECK(!watch.changed());

	// And going away is a change too, once.
	file.remove();
	CHECK(watch.changed());
	CHECK(!watch.changed());
}

TEST(restamping_a_file_watch_swallows_a_write_the_caller_made_itself) {
	Scratch file("rda_filewatch_test_restamp.txt");
	file.write("first");

	FileWatch watch;
	watch.watch({ file.name() });

	rewriteUntilMoved(file, fileStamp(file.name()));
	watch.restamp();
	CHECK(!watch.changed());

	// It still watches: the next edit is reported.
	rewriteUntilMoved(file, fileStamp(file.name()));
	CHECK(watch.changed());
}

TEST(watching_a_new_set_starts_from_the_files_as_they_are_now) {
	Scratch a("rda_filewatch_test_set_a.txt");
	Scratch b("rda_filewatch_test_set_b.txt");
	a.write("a");
	b.write("b");

	// A layout that gains an import is found again after the edit that added it, and the
	// new set must not report that same edit a second time.
	FileWatch watch;
	watch.watch({ a.name() });
	rewriteUntilMoved(a, fileStamp(a.name()));
	CHECK(watch.changed());

	watch.watch({ a.name(), b.name() });
	CHECK_EQ(watch.paths().size(), size_t(2));
	CHECK(!watch.changed());

	rewriteUntilMoved(b, fileStamp(b.name()));
	CHECK(watch.changed());
}

TEST(a_file_watch_with_nothing_to_watch_never_changes) {
	FileWatch watch;
	CHECK(watch.paths().empty());
	CHECK(!watch.changed());
	watch.watch({});
	CHECK(!watch.changed());
	watch.restamp();
	CHECK(!watch.changed());
}
