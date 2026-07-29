#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

// A bump allocator for things that live exactly one frame.
//
// The idea is that a lot of what a frame allocates is thrown away at the end of it anyway,
// so there's no reason to pay malloc/free for any of it. You hand out memory by moving a
// pointer forward, and at the end of the frame you move that pointer back to the start and
// the whole lot is "freed" at once. An allocation is an add and a compare, and reset() is
// two stores.
//
//	RDL::frame_arena arena;
//	// ... every frame:
//	arena.reset();                                  // last frame's memory is up for grabs again
//	Rect* rects = arena.allocate<Rect>(count);      // no malloc, no free, no bookkeeping
//
// reset() keeps the blocks it already got from the heap, which is the whole point: after a
// few frames the arena has grown to whatever the busiest frame needed and it never touches
// the heap again. release() is there for when you actually want the memory back.
//
// The one rule: THE ARENA DOES NOT RUN DESTRUCTORS. reset() just rewinds a pointer. So only
// put things in it that don't own anything, which is what per-frame scratch usually is
// (numbers, handles, rects, vertices, pointers into things that outlive the frame). create<T>()
// enforces that with a static_assert so it can't be got wrong by accident. If you need
// something with a destructor, it doesn't belong in a frame arena.

namespace RDL {

	class frame_arena {
	public:
		// Anything the arena hands out is at least this aligned, same as malloc, so small
		// types never have to pay for a manual alignment step
		static constexpr size_t default_alignment = alignof(std::max_align_t);

		explicit frame_arena(size_t pBlockSize = 64 * 1024) : mBlockSize(pBlockSize) {}

		~frame_arena() { release(); }

		// It owns its blocks, and copying it would mean copying live pointers that the
		// other one is still handing out
		frame_arena(const frame_arena&) = delete;
		frame_arena& operator=(const frame_arena&) = delete;

		frame_arena(frame_arena&& pOther) noexcept { moveFrom(pOther); }
		frame_arena& operator=(frame_arena&& pOther) noexcept {
			if (this != &pOther) { release(); moveFrom(pOther); }
			return *this;
		}

		// Raw bytes, aligned as asked. Returns nullptr only if the heap said no
		void* allocate_bytes(size_t pSize, size_t pAlignment = default_alignment) {
			if (pSize == 0) return nullptr;

			// Round the cursor up to the alignment the caller wants
			if (mCurrent) {
				size_t offset = alignUp(mCurrent->used, pAlignment);
				if (offset + pSize <= mCurrent->size) {
					unsigned char* result = mCurrent->data + offset;
					mCurrent->used = offset + pSize;
					mUsed += pSize;
					if (mUsed > mHighWater) mHighWater = mUsed;
					return result;
				}
			}

			// This block is full. Try the next one we already own before going to the heap,
			// that's what makes the steady state allocation free
			if (!moveToNextBlock(pSize, pAlignment) && !addBlock(pSize, pAlignment)) {
				return nullptr;
			}
			size_t offset = alignUp(mCurrent->used, pAlignment);
			unsigned char* result = mCurrent->data + offset;
			mCurrent->used = offset + pSize;
			mUsed += pSize;
			if (mUsed > mHighWater) mHighWater = mUsed;
			return result;
		}

		// Room for pCount T's, uninitialised. For POD scratch this is usually what you want
		template<typename T>
		T* allocate(size_t pCount = 1) {
			return static_cast<T*>(allocate_bytes(sizeof(T) * pCount, alignof(T)));
		}

		// Builds a T in the arena. Restricted to types that don't need destroying, because
		// nothing will ever call their destructor
		template<typename T, typename... Args>
		T* create(Args&&... args) {
			static_assert(std::is_trivially_destructible<T>::value,
			              "frame_arena never runs destructors, so it only accepts trivially destructible types");
			void* memory = allocate_bytes(sizeof(T), alignof(T));
			return memory ? ::new (memory) T(std::forward<Args>(args)...) : nullptr;
		}

		// Copies pCount T's in and gives back the copy, handy for freezing something that's
		// about to change while you still need this frame's version of it
		template<typename T>
		T* clone(const T* pSource, size_t pCount) {
			static_assert(std::is_trivially_copyable<T>::value,
			              "frame_arena::clone copies bytes, so the type has to be trivially copyable");
			if (!pSource || pCount == 0) return nullptr;
			T* result = allocate<T>(pCount);
			if (result) memcpy(result, pSource, sizeof(T) * pCount);
			return result;
		}

		// Everything handed out since the last reset becomes free again. Blocks are kept,
		// so the next frame allocates out of memory we already have
		void reset() {
			for (Block* block = mHead; block; block = block->next) block->used = 0;
			mCurrent = mHead;
			mUsed = 0;
		}

		// Hands the blocks back to the heap. Only worth calling when the arena is going
		// away or has ballooned for one unusual frame and you want the memory back
		void release() {
			Block* block = mHead;
			while (block) {
				Block* next = block->next;
				delete[] block->data;
				delete block;
				block = next;
			}
			mHead = mCurrent = nullptr;
			mUsed = 0;
			mBlockCount = 0;
			mReserved = 0;
		}

		// ---- a marker, for scratch inside a frame ----
		// Sometimes you want a slice of the frame back before the frame is over. Take a
		// marker, allocate, then rewind to it. Anything allocated after the marker is gone.
		struct marker {
			void* block = nullptr;
			size_t used = 0;
			size_t total = 0;
		};

		marker mark() const {
			marker m;
			m.block = mCurrent;
			m.used = mCurrent ? mCurrent->used : 0;
			m.total = mUsed;
			return m;
		}

		void rewind(const marker& pMarker) {
			Block* target = static_cast<Block*>(pMarker.block);
			if (!target) { reset(); return; }
			// Blocks after the marker's one go back to empty, they were all filled after it
			for (Block* block = target->next; block; block = block->next) block->used = 0;
			target->used = pMarker.used;
			mCurrent = target;
			mUsed = pMarker.total;
		}

		// Rewinds automatically at the end of the scope
		class scope {
		public:
			explicit scope(frame_arena& pArena) : mArena(pArena), mMarker(pArena.mark()) {}
			~scope() { mArena.rewind(mMarker); }
			scope(const scope&) = delete;
			scope& operator=(const scope&) = delete;
		private:
			frame_arena& mArena;
			marker mMarker;
		};

		// ---- what it's doing ----
		size_t used() const { return mUsed; }            // handed out since the last reset
		size_t reserved() const { return mReserved; }    // total block bytes we hold
		size_t block_count() const { return mBlockCount; }
		// The busiest frame so far. Size the block from this and the arena stops growing
		size_t high_water() const { return mHighWater; }
		void reset_high_water() { mHighWater = 0; }

	private:
		struct Block {
			Block* next = nullptr;
			unsigned char* data = nullptr;
			size_t used = 0;
			size_t size = 0;
		};

		static size_t alignUp(size_t pValue, size_t pAlignment) {
			return (pValue + pAlignment - 1) & ~(pAlignment - 1);
		}

		// After a reset the blocks are still there and empty, so a frame that allocates the
		// same way as the last one just walks the same chain again without touching the heap
		bool moveToNextBlock(size_t pSize, size_t pAlignment) {
			for (Block* block = mCurrent ? mCurrent->next : mHead; block; block = block->next) {
				if (alignUp(block->used, pAlignment) + pSize <= block->size) {
					mCurrent = block;
					return true;
				}
			}
			return false;
		}

		bool addBlock(size_t pSize, size_t pAlignment) {
			// Big enough for this request even if it's larger than the normal block, plus
			// the alignment slack so the round-up below can never overrun
			size_t needed = pSize + pAlignment;
			size_t size = needed > mBlockSize ? needed : mBlockSize;

			Block* block = new (std::nothrow) Block();
			if (!block) return false;
			block->data = new (std::nothrow) unsigned char[size];
			if (!block->data) { delete block; return false; }
			block->size = size;

			// Append, so the blocks stay in a stable order for reset()/rewind() to walk
			if (!mHead) mHead = block;
			else {
				Block* last = mHead;
				while (last->next) last = last->next;
				last->next = block;
			}
			mCurrent = block;
			++mBlockCount;
			mReserved += size;
			return true;
		}

		void moveFrom(frame_arena& pOther) {
			mHead = pOther.mHead;
			mCurrent = pOther.mCurrent;
			mBlockSize = pOther.mBlockSize;
			mUsed = pOther.mUsed;
			mReserved = pOther.mReserved;
			mBlockCount = pOther.mBlockCount;
			mHighWater = pOther.mHighWater;
			pOther.mHead = pOther.mCurrent = nullptr;
			pOther.mUsed = pOther.mReserved = pOther.mBlockCount = pOther.mHighWater = 0;
		}

		Block* mHead = nullptr;
		Block* mCurrent = nullptr;
		size_t mBlockSize = 64 * 1024;
		size_t mUsed = 0;
		size_t mReserved = 0;
		size_t mBlockCount = 0;
		size_t mHighWater = 0;
	};

	// An allocator adapter, for when you want a std container to allocate out of the arena.
	// The container still thinks it owns its memory and will call deallocate, which does
	// nothing here, the arena reclaims everything on reset. Only sensible for containers
	// that don't outlive the frame
	template<typename T>
	class arena_allocator {
	public:
		using value_type = T;

		arena_allocator(frame_arena& pArena) noexcept : mArena(&pArena) {}
		template<typename U>
		arena_allocator(const arena_allocator<U>& pOther) noexcept : mArena(pOther.arena()) {}

		T* allocate(size_t pCount) {
			void* memory = mArena->allocate_bytes(sizeof(T) * pCount, alignof(T));
			if (!memory) throw std::bad_alloc();
			return static_cast<T*>(memory);
		}
		void deallocate(T*, size_t) noexcept {} // the arena frees in one go, on reset

		frame_arena* arena() const noexcept { return mArena; }

		template<typename U>
		bool operator==(const arena_allocator<U>& pOther) const noexcept { return mArena == pOther.arena(); }
		template<typename U>
		bool operator!=(const arena_allocator<U>& pOther) const noexcept { return mArena != pOther.arena(); }

	private:
		frame_arena* mArena;
	};

}
