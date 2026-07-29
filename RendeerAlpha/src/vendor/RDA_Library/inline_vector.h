#pragma once
#include <cstddef>
#include <new>
#include <utility>

namespace RDL {

	// One slot of the inline_vector.
	template<typename T>
	struct InlineVectorSlot {
		static constexpr size_t storage_size = sizeof(T) > sizeof(void*) ? sizeof(T) : sizeof(void*);
		static constexpr size_t storage_align = alignof(T) > alignof(void*) ? alignof(T) : alignof(void*);

		InlineVectorSlot() = default;

		~InlineVectorSlot() {
			destroy();
		}

		// The vector copies its slots by hand, it has to rebuild the free list afterwards anyway
		InlineVectorSlot(const InlineVectorSlot&) = delete;
		InlineVectorSlot& operator=(const InlineVectorSlot&) = delete;

		template<typename... Args>
		T* construct(Args&&... args) {
			::new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
			contains = true;
			return ptr();
		}

		void destroy() {
			if (contains) {
				ptr()->~T();
				contains = false;
			}
		}

		T* ptr() { return reinterpret_cast<T*>(storage); }
		const T* ptr() const { return reinterpret_cast<const T*>(storage); }

		// Only means anything while the slot is empty, it shares the bytes with the object
		InlineVectorSlot*& nextFree() { return *reinterpret_cast<InlineVectorSlot**>(storage); }

		bool contains = false;
		alignas(storage_align) unsigned char storage[storage_size];
	};

	// A pool that starts inside the object and dumps into the heap when it runs out of room
	template<typename T, size_t N>
	struct inline_vector {
		static_assert(N > 0, "inline_vector needs at least one slot");

		using slot_type = InlineVectorSlot<T>;

		static constexpr size_t npos = static_cast<size_t>(-1);

		// A heap chunk holds the same N slots as the inline block, and gets linked at the end
		// so the index of everything already in the vector stays where it was
		struct Chunk {
			Chunk* next = nullptr;
			slot_type slots[N];
		};

		inline_vector() {
			pushBlockOnFreeList(inline_slots);
		}

		inline_vector(const inline_vector& other) {
			pushBlockOnFreeList(inline_slots);
			copyFrom(other);
		}

		inline_vector& operator=(const inline_vector& other) {
			if (this == &other) return *this;
			destroyAll();
			copyFrom(other);
			return *this;
		}

		~inline_vector() {
			Chunk* chunk = overflow;
			while (chunk) {
				Chunk* next = chunk->next;
				delete chunk; // the slots it owns destroy whatever is still living in them
				chunk = next;
			}
		}

		template<typename SlotType, typename ChunkType, typename ValueType>
		class IteratorBase {
		public:
			IteratorBase(SlotType* pSlots, ChunkType* pNextChunk, size_t pIndex)
				: slots(pSlots), nextChunk(pNextChunk), index(pIndex) {
				skipEmpty();
			}

			ValueType& operator*() const { return *slots[index].ptr(); }
			ValueType* operator->() const { return slots[index].ptr(); }

			IteratorBase& operator++() {
				++index;
				skipEmpty();
				return *this;
			}

			bool operator==(const IteratorBase& other) const { return slots == other.slots && index == other.index; }
			bool operator!=(const IteratorBase& other) const { return !(*this == other); }

		private:
			void skipEmpty() {
				while (slots) {
					while (index < N && !slots[index].contains) ++index;
					if (index < N) return;
					// this block is done, hop on the next one
					slots = nextChunk ? nextChunk->slots : nullptr;
					nextChunk = nextChunk ? nextChunk->next : nullptr;
					index = 0;
				}
				index = 0; // an exhausted iterator has to compare equal to end()
			}

			SlotType* slots;
			ChunkType* nextChunk;
			size_t index;
		};

		using Iterator = IteratorBase<slot_type, Chunk, T>;
		using ConstIterator = IteratorBase<const slot_type, const Chunk, const T>;

		T* insert(const T& pData) { return emplace(pData); }
		T* insert(T&& pData) { return emplace(std::move(pData)); }

		// Same as insert but builds T in place, so no temporary and no copy
		template<typename... Args>
		T* emplace(Args&&... args) {
			if (!free_slot) addChunk(); // out of inline room, this is where we dump into the heap

			slot_type* slot = free_slot;
			slot_type* next = slot->nextFree(); // has to be read before construct(), they share the bytes
			free_slot = next;

			T* result = nullptr;
			try {
				result = slot->construct(std::forward<Args>(args)...);
			}
			catch (...) {
				slot->nextFree() = free_slot;
				free_slot = slot;
				throw;
			}
			++used_slots;
			return result;
		}

		// Takes a pointer handed out by insert/emplace. Anything else (a foreign pointer,
		// a pointer into the middle of an element, an already removed one) is refused
		bool remove(T* pData) {
			return removeAt(indexOf(pData));
		}

		bool removeAt(size_t pIndex) {
			slot_type* slot = slotAt(pIndex);
			if (!slot || !slot->contains) return false;

			slot->destroy();
			slot->nextFree() = free_slot;
			free_slot = slot;
			--used_slots;
			return true;
		}

		// Kills every element but keeps the chunks around, they are the expensive part.
		// Call shrink() after it if you actually want the memory back
		void clear() {
			destroyAll();
			rebuildFreeList();
			used_slots = 0;
		}

		// Gives back the heap chunks that don't hold anything anymore, from the last one backwards.
		// Returns how many chunks it managed to free
		size_t shrink() {
			size_t freed = 0;
			while (overflow) {
				Chunk* previous = nullptr;
				Chunk* last = overflow;
				while (last->next) {
					previous = last;
					last = last->next;
				}
				if (!blockEmpty(last->slots)) break;

				if (previous) previous->next = nullptr;
				else overflow = nullptr;
				delete last;
				--block_count;
				++freed;
			}
			if (freed) rebuildFreeList();
			return freed;
		}

		// The index of an element, or npos if that pointer isn't one of ours
		size_t indexOf(const T* pData) const {
			if (!pData || !used_slots) return npos;

			const unsigned char* target = reinterpret_cast<const unsigned char*>(pData);
			const Chunk* cursor = overflow;
			size_t base = 0;
			for (const slot_type* blockSlots = inline_slots; blockSlots; blockSlots = nextBlock(cursor)) {
				const unsigned char* first = reinterpret_cast<const unsigned char*>(blockSlots);
				if (target >= first && target < first + N * sizeof(slot_type)) {
					size_t index = static_cast<size_t>(target - first) / sizeof(slot_type);
					// it has to be the start of a live element, not an address somewhere inside one
					if (!blockSlots[index].contains || blockSlots[index].ptr() != pData) return npos;
					return base + index;
				}
				base += N;
			}
			return npos;
		}

		bool owns(const T* pData) const { return indexOf(pData) != npos; }

		// Element at a given index, nullptr when that slot is a hole or the index is out of range
		T* at(size_t pIndex) {
			slot_type* slot = slotAt(pIndex);
			return (slot && slot->contains) ? slot->ptr() : nullptr;
		}

		const T* at(size_t pIndex) const {
			const slot_type* slot = slotAt(pIndex);
			return (slot && slot->contains) ? slot->ptr() : nullptr;
		}

		size_t size() const { return used_slots; }
		// Everything we can hold right now, inline block and chunks together
		size_t capacity() const { return block_count * N; }
		size_t inline_capacity() const { return N; }
		bool empty() const { return used_slots == 0; }
		// True when the next insert has to allocate a chunk
		bool full() const { return free_slot == nullptr; }
		// True once we've spilled into the heap
		bool spilled() const { return overflow != nullptr; }
		size_t chunk_count() const { return block_count - 1; }

		Iterator begin() { return Iterator(inline_slots, overflow, 0); }
		Iterator end() { return Iterator(nullptr, nullptr, 0); }
		ConstIterator begin() const { return ConstIterator(inline_slots, overflow, 0); }
		ConstIterator end() const { return ConstIterator(nullptr, nullptr, 0); }

	private:
		slot_type inline_slots[N];
		Chunk* overflow = nullptr;
		slot_type* free_slot = nullptr;
		size_t used_slots = 0;
		size_t block_count = 1; // the inline block counts as one

		static slot_type* nextBlock(Chunk*& pCursor) {
			if (!pCursor) return nullptr;
			slot_type* slots = pCursor->slots;
			pCursor = pCursor->next;
			return slots;
		}

		static const slot_type* nextBlock(const Chunk*& pCursor) {
			if (!pCursor) return nullptr;
			const slot_type* slots = pCursor->slots;
			pCursor = pCursor->next;
			return slots;
		}

		slot_type* slotAt(size_t pIndex) {
			if (pIndex >= block_count * N) return nullptr;
			if (pIndex < N) return &inline_slots[pIndex];

			Chunk* chunk = overflow;
			for (size_t block = pIndex / N; block > 1; --block) chunk = chunk->next;
			return &chunk->slots[pIndex % N];
		}

		const slot_type* slotAt(size_t pIndex) const {
			if (pIndex >= block_count * N) return nullptr;
			if (pIndex < N) return &inline_slots[pIndex];

			const Chunk* chunk = overflow;
			for (size_t block = pIndex / N; block > 1; --block) chunk = chunk->next;
			return &chunk->slots[pIndex % N];
		}

		void addChunk() {
			Chunk* chunk = new Chunk; // throws before anything else changed, so we stay consistent
			if (overflow) {
				Chunk* last = overflow;
				while (last->next) last = last->next;
				last->next = chunk;
			}
			else {
				overflow = chunk;
			}
			++block_count;
			pushBlockOnFreeList(chunk->slots);
		}

		// Chains a whole block into the free list, lowest index first so it gets used in order
		void pushBlockOnFreeList(slot_type* pSlots) {
			for (size_t i = N; i > 0; --i) {
				pSlots[i - 1].nextFree() = free_slot;
				free_slot = &pSlots[i - 1];
			}
		}

		void rebuildFreeList() {
			free_slot = nullptr;
			slot_type* tail = nullptr;
			Chunk* cursor = overflow;
			for (slot_type* blockSlots = inline_slots; blockSlots; blockSlots = nextBlock(cursor)) {
				for (size_t i = 0; i < N; ++i) {
					if (blockSlots[i].contains) continue;
					// appending instead of pushing keeps the list in index order
					if (tail) tail->nextFree() = &blockSlots[i];
					else free_slot = &blockSlots[i];
					tail = &blockSlots[i];
				}
			}
			if (tail) tail->nextFree() = nullptr;
		}

		void destroyAll() {
			Chunk* cursor = overflow;
			for (slot_type* blockSlots = inline_slots; blockSlots; blockSlots = nextBlock(cursor)) {
				for (size_t i = 0; i < N; ++i) blockSlots[i].destroy();
			}
		}

		static bool blockEmpty(const slot_type* pSlots) {
			for (size_t i = 0; i < N; ++i) {
				if (pSlots[i].contains) return false;
			}
			return true;
		}

		// Rebuilds us as a copy of other, elements land on the same indices they had over there
		void copyFrom(const inline_vector& other) {
			while (block_count < other.block_count) addChunk();

			const Chunk* srcCursor = other.overflow;
			Chunk* dstCursor = overflow;
			const slot_type* src = other.inline_slots;
			slot_type* dst = inline_slots;
			while (src) {
				for (size_t i = 0; i < N; ++i) {
					if (src[i].contains) dst[i].construct(*src[i].ptr());
				}
				src = nextBlock(srcCursor);
				dst = nextBlock(dstCursor);
			}
			used_slots = other.used_slots;
			rebuildFreeList();
		}
	};

}
