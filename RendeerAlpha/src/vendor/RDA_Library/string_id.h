#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>

// An interned string id.
//
// The whole point is to stop comparing text at runtime. A string_id is a plain uint32_t, so
// looking something up is one integer compare instead of hashing the string and then comparing
// the characters to settle a bucket collision. Handy for anything keyed by a fixed vocabulary,
// theme variants, style names, event names, asset ids, that sort of thing.
//
// The id is the hash itself, not a table index, which means it can be computed at compile time
// and it's the same value in every run and every module:
//
//	using namespace RDL::string_id_literals;
//	constexpr RDL::string_id primary = "primary"_sid;   // done by the compiler, nothing at runtime
//	if (variant == primary) { ... }                     // one uint32 compare
//
//	switch (variant.value) {
//		case ("primary"_sid).value: break;              // it works in a switch too
//		case ("danger"_sid).value:  break;
//	}
//
// The string_table below is optional. You only need it if you want the text back for logging,
// or if you want the collision check. Comparing ids never touches it.

namespace RDL {

	// FNV-1a, 32 bit. Cheap, decent spread, and short enough to run at compile time
	constexpr uint32_t fnv1a_32(const char* pText, size_t pLength) {
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < pLength; ++i) {
			hash ^= static_cast<uint32_t>(static_cast<unsigned char>(pText[i]));
			hash *= 16777619u;
		}
		return hash;
	}

	constexpr size_t constexpr_strlen(const char* pText) {
		size_t length = 0;
		while (pText[length]) ++length;
		return length;
	}

	struct string_id {
		// 0 is kept for "no id", an empty string gets it and a real string never does
		uint32_t value = 0;

		constexpr string_id() = default;

		// Wraps a value you already have, from a file or from the network.
		// It's a named function and not a constructor because string_id(0) would be ambiguous,
		// 0 is just as good a const char* as it is a uint32_t
		static constexpr string_id from_value(uint32_t pValue) {
			string_id id;
			id.value = pValue;
			return id;
		}

		// Implicit on purpose, so a function taking a string_id can be called with a literal
		// and the compiler folds it into a constant.
		// A std::string needs to go through string_view: string_id(std::string_view(s))
		constexpr string_id(const char* pText) : value(hash(pText, pText ? constexpr_strlen(pText) : 0)) {}
		constexpr string_id(std::string_view pText) : value(hash(pText.data(), pText.size())) {}

		constexpr bool valid() const { return value != 0; }
		constexpr explicit operator bool() const { return value != 0; }

		friend constexpr bool operator==(string_id a, string_id b) { return a.value == b.value; }
		friend constexpr bool operator!=(string_id a, string_id b) { return a.value != b.value; }
		friend constexpr bool operator<(string_id a, string_id b) { return a.value < b.value; }
		friend constexpr bool operator>(string_id a, string_id b) { return a.value > b.value; }
		friend constexpr bool operator<=(string_id a, string_id b) { return a.value <= b.value; }
		friend constexpr bool operator>=(string_id a, string_id b) { return a.value >= b.value; }

	private:
		static constexpr uint32_t hash(const char* pText, size_t pLength) {
			if (!pText || pLength == 0) return 0;
			uint32_t result = fnv1a_32(pText, pLength);
			// don't let a real string land on the "no id" value
			return result ? result : 1u;
		}
	};

	namespace string_id_literals {
		// consteval, so this one can never sneak into runtime
		consteval string_id operator""_sid(const char* pText, size_t pLength) {
			return string_id(std::string_view(pText, pLength));
		}
	}

	// Keeps the text behind the ids so you can print them back, and yells if two different
	// strings ever end up with the same id.
	//
	// You don't need this to compare ids, it's a debugging and reverse lookup thing. Registering
	// is cheap enough to do at startup for the whole vocabulary and then never touch again.
	// Not thread safe, intern from one thread or guard it yourself
	class string_table {
	public:
		string_table() = default;

		~string_table() {
			releaseArena();
			delete[] mBuckets;
		}

		// It owns the text it copied, so no copying it around by accident
		string_table(const string_table&) = delete;
		string_table& operator=(const string_table&) = delete;

		// Registers the text and gives back its id. Interning the same text twice is a no-op
		string_id intern(std::string_view pText) {
			string_id id(pText);
			if (!id.valid()) return id;

			if (!mBuckets) grow(64);
			else if ((mCount + 1) * 4 >= mBucketCount * 3) grow(mBucketCount * 2);

			Entry& slot = findSlot(mBuckets, mBucketCount, id);
			if (slot.id.valid()) {
				// same id already registered, either it's the same string or we hit a collision
				if (pText.size() != slot.length || memcmp(slot.text, pText.data(), pText.size()) != 0) {
					++mCollisions;
				}
				return id;
			}

			slot.id = id;
			slot.text = copyIntoArena(pText);
			slot.length = pText.size();
			++mCount;
			return id;
		}

		// The id of an already registered text, or an invalid id. Doesn't register anything
		string_id find(std::string_view pText) const {
			string_id id(pText);
			return registered(id) ? id : string_id();
		}

		// The text behind an id, or nullptr if it was never interned here.
		// The pointer stays valid until the table is cleared or destroyed, the arena never moves
		const char* text(string_id pId) const {
			if (!pId.valid() || !mBuckets) return nullptr;
			const Entry& slot = findSlot(mBuckets, mBucketCount, pId);
			return slot.id.valid() ? slot.text : nullptr;
		}

		bool registered(string_id pId) const { return text(pId) != nullptr; }

		size_t size() const { return mCount; }
		// How many times two different strings wanted the same id. Anything but 0 is a problem
		size_t collisions() const { return mCollisions; }

		void clear() {
			releaseArena();
			for (size_t i = 0; i < mBucketCount; ++i) mBuckets[i] = Entry();
			mCount = 0;
			mCollisions = 0;
		}

	private:
		struct Entry {
			string_id id;
			const char* text = nullptr;
			size_t length = 0;
		};

		// The text lives in blocks that are never reallocated, that's what makes text() safe to keep
		struct ArenaBlock {
			ArenaBlock* next = nullptr;
			char* data = nullptr;
			size_t used = 0;
			size_t size = 0;
		};

		static constexpr size_t arena_block_size = 4096;

		// Open addressing with linear probing. The id is already a hash so we just mask it
		static Entry& findSlot(Entry* pBuckets, size_t pBucketCount, string_id pId) {
			size_t mask = pBucketCount - 1;
			size_t index = pId.value & mask;
			while (pBuckets[index].id.valid() && pBuckets[index].id != pId) {
				index = (index + 1) & mask;
			}
			return pBuckets[index];
		}

		static const Entry& findSlot(const Entry* pBuckets, size_t pBucketCount, string_id pId) {
			return findSlot(const_cast<Entry*>(pBuckets), pBucketCount, pId);
		}

		void grow(size_t pBucketCount) {
			Entry* buckets = new Entry[pBucketCount]();
			for (size_t i = 0; i < mBucketCount; ++i) {
				if (!mBuckets[i].id.valid()) continue;
				findSlot(buckets, pBucketCount, mBuckets[i].id) = mBuckets[i];
			}
			delete[] mBuckets;
			mBuckets = buckets;
			mBucketCount = pBucketCount;
		}

		const char* copyIntoArena(std::string_view pText) {
			size_t needed = pText.size() + 1; // keep it null terminated so text() can hand out a const char*
			if (!mArena || mArena->used + needed > mArena->size) {
				size_t size = needed > arena_block_size ? needed : arena_block_size;
				ArenaBlock* block = new ArenaBlock();
				block->data = new char[size];
				block->size = size;
				block->next = mArena;
				mArena = block;
			}
			char* target = mArena->data + mArena->used;
			memcpy(target, pText.data(), pText.size());
			target[pText.size()] = '\0';
			mArena->used += needed;
			return target;
		}

		void releaseArena() {
			while (mArena) {
				ArenaBlock* next = mArena->next;
				delete[] mArena->data;
				delete mArena;
				mArena = next;
			}
		}

		Entry* mBuckets = nullptr;
		size_t mBucketCount = 0;
		size_t mCount = 0;
		size_t mCollisions = 0;
		ArenaBlock* mArena = nullptr;
	};

	// The table most code will want. Only needed for the reverse lookup, ids work without it
	inline string_table& global_string_table() {
		static string_table table;
		return table;
	}

	inline string_id intern(std::string_view pText) { return global_string_table().intern(pText); }
	// Gives back the text if it was interned, otherwise a placeholder instead of nullptr,
	// this one is meant to be dropped straight into a log line
	inline const char* to_cstring(string_id pId) {
		const char* text = global_string_table().text(pId);
		return text ? text : "<unregistered string_id>";
	}

}

// So string_id can be a key in the std containers without writing a hasher every time
namespace std {
	template<>
	struct hash<RDL::string_id> {
		size_t operator()(RDL::string_id pId) const noexcept { return static_cast<size_t>(pId.value); }
	};
}
