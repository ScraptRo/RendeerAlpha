#include "TestHarness.h"
#include <Core/RetireQueue.h>

#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

// The list a texture waits in until the GPU is done with it, written once.
//
// What a queue of this kind has to get right is small, and all of it is about who is allowed
// to destroy what and when: nothing is destroyed by the queue itself, nothing retired is ever
// lost, and what comes out comes out in the order it went in. A texture is the one thing that
// uses it today, but a texture needs a device, so these use things that do not.
using namespace RDA;

namespace {

	// Counts its own destruction, so a test can tell when -- and by whom -- something was
	// destroyed. Move-only like the textures it stands for.
	struct Tracked {
		int               id = 0;
		std::atomic<int>* destroyed = nullptr;

		Tracked(int id, std::atomic<int>* destroyed) : id(id), destroyed(destroyed) {}
		Tracked(Tracked&& other) noexcept : id(other.id), destroyed(other.destroyed) {
			other.destroyed = nullptr;
		}
		Tracked& operator=(Tracked&& other) noexcept {
			if (this != &other) {
				if (destroyed) destroyed->fetch_add(1);
				id = other.id;
				destroyed = other.destroyed;
				other.destroyed = nullptr;
			}
			return *this;
		}
		Tracked(const Tracked&) = delete;
		Tracked& operator=(const Tracked&) = delete;
		~Tracked() { if (destroyed) destroyed->fetch_add(1); }
	};
}

TEST(a_retire_queue_gives_things_back_oldest_first) {
	RetireQueue<int> queue;
	queue.retire(3);
	queue.retire(1);
	queue.retire(2);

	// The order they were retired in, not any order they could be sorted into.
	const std::vector<int> taken = queue.take();
	CHECK_EQ(taken.size(), size_t(3));
	if (taken.size() == 3) {
		CHECK_EQ(taken[0], 3);
		CHECK_EQ(taken[1], 1);
		CHECK_EQ(taken[2], 2);
	}
}

TEST(taking_from_a_retire_queue_empties_it_and_it_keeps_working) {
	RetireQueue<int> queue;
	CHECK(queue.take().empty());

	queue.retire(1);
	queue.retire(2);
	CHECK_EQ(queue.take().size(), size_t(2));

	// Nothing is handed out twice.
	CHECK(queue.take().empty());

	// And what is retired after a take is the next take's, not lost with the last one.
	queue.retire(3);
	const std::vector<int> next = queue.take();
	CHECK_EQ(next.size(), size_t(1));
	if (!next.empty()) CHECK_EQ(next[0], 3);
	CHECK(queue.take().empty());
}

TEST(a_retire_queue_holds_things_that_cannot_be_copied) {
	// A texture is held by unique_ptr precisely so its address stays put while it waits, and
	// the queue must not need a copy of what it holds to do its job.
	RetireQueue<std::unique_ptr<int>> queue;
	queue.retire(std::make_unique<int>(10));
	queue.retire(std::make_unique<int>(20));

	std::vector<std::unique_ptr<int>> taken = queue.take();
	CHECK_EQ(taken.size(), size_t(2));
	if (taken.size() == 2) {
		CHECK(taken[0] != nullptr && taken[1] != nullptr);
		if (taken[0] && taken[1]) {
			CHECK_EQ(*taken[0], 10);
			CHECK_EQ(*taken[1], 20);
		}
	}
}

TEST(a_retire_queue_never_destroys_what_it_holds) {
	// The whole point of the thing: whoever takes decides when, after waiting for the GPU,
	// and the queue does not get a say. Destroying one while it waits is the bug it exists
	// to prevent.
	std::atomic<int> destroyed{ 0 };
	{
		RetireQueue<Tracked> queue;
		queue.retire(Tracked(1, &destroyed));
		queue.retire(Tracked(2, &destroyed));
		queue.retire(Tracked(3, &destroyed));
		CHECK_EQ(destroyed.load(), 0);

		std::vector<Tracked> taken = queue.take();
		CHECK_EQ(taken.size(), size_t(3));
		// Taking hands them over alive: it is the caller's to destroy now.
		CHECK_EQ(destroyed.load(), 0);

		taken.clear();
		CHECK_EQ(destroyed.load(), 3);

		// The queue is empty, so letting it go destroys nothing more.
		queue.retire(Tracked(4, &destroyed));
		queue.take();
		CHECK_EQ(destroyed.load(), 4);
	}
	CHECK_EQ(destroyed.load(), 4);
}

TEST(two_threads_can_retire_into_one_queue_at_once) {
	// Each thread retires a numbered run. Nothing may be lost, and the two runs may be
	// interleaved however the threads happened to go but each must still be in the order its
	// own thread produced it.
	constexpr int kPerThread = 20000;
	RetireQueue<int> queue;

	// Thread 0 retires 0..N-1, thread 1 retires N..2N-1, so the values say who sent them.
	std::thread first([&] { for (int i = 0; i < kPerThread; ++i) queue.retire(i); });
	std::thread second([&] { for (int i = 0; i < kPerThread; ++i) queue.retire(kPerThread + i); });
	first.join();
	second.join();

	const std::vector<int> taken = queue.take();
	CHECK_EQ(taken.size(), size_t(2 * kPerThread));

	int nextFromFirst = 0, nextFromSecond = kPerThread;
	bool inOrder = true;
	for (int value : taken) {
		if (value < kPerThread) {
			if (value != nextFromFirst++) inOrder = false;
		} else {
			if (value != nextFromSecond++) inOrder = false;
		}
	}
	CHECK(inOrder);
	CHECK_EQ(nextFromFirst, kPerThread);
	CHECK_EQ(nextFromSecond, 2 * kPerThread);
	CHECK(queue.take().empty());
}

TEST(taking_while_two_threads_retire_loses_nothing_and_repeats_nothing) {
	// The shape it is used in: the loop takes every frame while widgets, wherever they are
	// torn down, go on retiring. Every item must come out of exactly one take, and each
	// thread's items in the order they went in, across takes as well as within one.
	constexpr int kPerThread = 20000;
	RetireQueue<int> queue;
	std::atomic<int> finished{ 0 };

	std::vector<int> received;
	std::thread taker([&] {
		for (;;) {
			// Asked before the take, so a queue that is empty after both producers said they
			// were done is empty for good.
			const bool done = finished.load() == 2;
			std::vector<int> batch = queue.take();
			received.insert(received.end(), batch.begin(), batch.end());
			if (batch.empty()) {
				if (done) break;
				std::this_thread::yield();
			}
		}
	});

	std::thread first([&] {
		for (int i = 0; i < kPerThread; ++i) queue.retire(i);
		finished.fetch_add(1);
	});
	std::thread second([&] {
		for (int i = 0; i < kPerThread; ++i) queue.retire(kPerThread + i);
		finished.fetch_add(1);
	});

	first.join();
	second.join();
	taker.join();

	CHECK_EQ(received.size(), size_t(2 * kPerThread));
	int nextFromFirst = 0, nextFromSecond = kPerThread;
	bool inOrder = true;
	for (int value : received) {
		if (value < kPerThread) {
			if (value != nextFromFirst++) inOrder = false;
		} else {
			if (value != nextFromSecond++) inOrder = false;
		}
	}
	CHECK(inOrder);
	CHECK_EQ(nextFromFirst, kPerThread);
	CHECK_EQ(nextFromSecond, 2 * kPerThread);
}
