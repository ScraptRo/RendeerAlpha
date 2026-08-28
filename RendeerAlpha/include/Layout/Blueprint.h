#pragma once
#include <Layout/Expression.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A compiled layout: the structure of a widget tree, flattened into one buffer.
//
// This is the ABI between the layout compiler and the runtime, and the reason the
// authoring language never has to exist at runtime. A .tsx file is TypeScript with JSX
// in it; by the time it reaches an application it is this — an array of PODs and a
// string table, loaded by reading bytes rather than by parsing anything.
//
// Nodes are stored breadth-first, which buys two things at once. A node's children are
// contiguous, so a range is (firstChild, childCount) rather than a list; and a node's
// parent always appears before it, so instantiating the tree is a single forward loop
// with no recursion and no fixup pass.
//
// Everything that could have been a string is an index into the table instead, so the
// node array stays fixed-stride and the whole buffer can be memory-mapped later without
// changing anything here.
namespace RDA::Layout {

	// 'RDAB', as it appears in a hex dump on a little-endian machine.
	inline constexpr uint32_t kBlueprintMagic   = 0x42414452u;
	// Bumped whenever the layout of anything below changes. A blueprint that does not
	// match is refused rather than reinterpreted: it was produced by a different
	// compiler than the one this binary agrees with.
	inline constexpr uint32_t kBlueprintVersion = 2u;

	enum class PropKind : uint32_t {
		String = 0,
		Number = 1,
		Bool   = 2,
	};

	struct BlueprintHeader {
		uint32_t magic;
		uint32_t version;
		uint32_t nodeCount;
		uint32_t propCount;
		uint32_t bindingCount;
		uint32_t instructionCount;
		uint32_t refCount;      // pooled string indices, for binding texts and signals
		uint32_t numberCount;   // pooled expression constants
		uint32_t stringCount;
		uint32_t stringBytes;
	};

	struct BlueprintNode {
		uint32_t type;        // string index: "panel", "label", "button", ...
		uint32_t id;          // string index; the compiler gives every node one
		uint32_t parent;      // node index, or kNoParent for the root
		uint32_t firstChild;  // node index; children are contiguous
		uint32_t childCount;
		uint32_t firstProp;   // prop index; a node's props are contiguous
		uint32_t propCount;
	};

	inline constexpr uint32_t kNoParent = 0xFFFFFFFFu;

	struct BlueprintProp {
		uint32_t key;     // string index
		uint32_t kind;    // PropKind
		float    number;  // Number, and Bool as 0 or 1
		uint32_t text;    // String: index into the table. Unused otherwise.
	};

	struct BlueprintString {
		uint32_t offset;  // into the byte blob
		uint32_t length;  // not counting the terminator the writer still emits
	};

	// What a binding is for. A value binding computes a property; an event binding runs
	// when something happens. They are the same machine pointed in opposite directions:
	// one reads signals and produces a value, the other reads them and writes them back.
	enum class BindingKind : uint32_t {
		Value = 0,
		Event = 1,
	};

	// One compiled binding. The program's pieces live in shared pools rather than inside
	// the record, so a blueprint stays a fixed-stride array of PODs; `programFor` copies
	// them back out into a Program when a binding is instantiated, which happens once.
	struct BlueprintBinding {
		uint32_t node;         // which node it belongs to
		uint32_t key;          // string index: the property, or the event name
		uint32_t kind;         // BindingKind
		uint32_t firstCode;    // into the instruction pool
		uint32_t codeCount;
		uint32_t firstNumber;  // into the number pool
		uint32_t numberCount;
		uint32_t firstText;    // into the reference pool; each entry a string index
		uint32_t textCount;
		uint32_t firstSignal;  // into the reference pool; each entry a string index
		uint32_t signalCount;
	};

	// The opcode is widened to 32 bits so the record needs no padding and the whole
	// section stays four-byte aligned wherever it lands in the file.
	struct BlueprintInstruction {
		uint32_t op;
		uint32_t operand;
	};

	// A blueprint held in memory, with the arrays pointing into the buffer it owns.
	//
	// parse() validates every offset and index before any of the accessors below can be
	// reached, so a caller that got `true` can index freely. That check happens once,
	// here, rather than being repeated at every use — which is the only way a format
	// like this stays both safe and free at the point of use.
	class Blueprint {
	public:
		// Takes ownership of `bytes`. On failure `error` says what was wrong and the
		// blueprint is left invalid.
		bool parse(std::vector<uint8_t> bytes, std::string& error);
		void reset();

		bool   valid() const { return mHeader != nullptr; }
		size_t nodeCount() const { return mHeader ? mHeader->nodeCount : 0; }
		size_t propCount() const { return mHeader ? mHeader->propCount : 0; }
		size_t byteSize()  const { return mBytes.size(); }

		const BlueprintNode& node(size_t index) const { return mNodes[index]; }
		const BlueprintProp& prop(size_t index) const { return mProps[index]; }

		size_t bindingCount() const { return mHeader ? mHeader->bindingCount : 0; }
		const BlueprintBinding& binding(size_t index) const { return mBindings[index]; }

		// Rebuilds the runnable program for a binding. Called once per binding when a
		// layout is instantiated, never while it is running.
		Program programFor(const BlueprintBinding& binding) const;

		// The text behind a string index. Always valid on a parsed blueprint.
		std::string_view string(uint32_t index) const;

		// A node's prop by name, or nullptr. Linear over that node's own props, which
		// number a handful — a map would cost more to build than it ever saved.
		const BlueprintProp* find(const BlueprintNode& node, std::string_view key) const;

		// Typed reads with a fallback, for the loader.
		std::string_view text(const BlueprintNode& node, std::string_view key,
		                      std::string_view fallback = {}) const;
		float number(const BlueprintNode& node, std::string_view key, float fallback = 0.0f) const;
		bool  boolean(const BlueprintNode& node, std::string_view key, bool fallback = false) const;

	private:
		std::vector<uint8_t>   mBytes;
		const BlueprintHeader*      mHeader       = nullptr;
		const BlueprintNode*        mNodes        = nullptr;
		const BlueprintProp*        mProps        = nullptr;
		const BlueprintBinding*     mBindings     = nullptr;
		const BlueprintInstruction* mInstructions = nullptr;
		const uint32_t*             mRefs         = nullptr;
		const uint8_t*              mNumbers      = nullptr; // read with memcpy: not aligned
		const BlueprintString*      mStrings      = nullptr;
		const char*                 mText         = nullptr;
	};

	// Builds the buffer the class above reads.
	//
	// Used by the compiler, and deliberately kept in the same header as the reader: the
	// two halves of a binary format drift apart the moment they stop being written
	// next to each other.
	class BlueprintBuilder {
	public:
		// Interns `text`, returning its string index. Repeated strings are stored once,
		// which for a layout — where "fill", "default" and every widget type repeat on
		// nearly every node — is most of the table.
		uint32_t intern(std::string_view text);

		// Adds a node under `parent` (kNoParent for the root). Nodes must be added in
		// breadth-first order; addChildRange() records where a node's children landed.
		uint32_t addNode(uint32_t typeString, uint32_t idString, uint32_t parent);
		void     setChildRange(uint32_t node, uint32_t firstChild, uint32_t childCount);
		void     addProp(uint32_t node, uint32_t keyString, PropKind kind,
		                 float number, uint32_t textString);

		// Appends a compiled program, pooling its code and constants. `keyString` is the
		// property it drives, or the event it answers.
		void addBinding(uint32_t node, uint32_t keyString, BindingKind kind,
		                const Program& program);

		std::vector<uint8_t> finish() const;

		size_t nodeCount() const { return mNodes.size(); }
		size_t bindingCount() const { return mBindings.size(); }

	private:
		std::vector<BlueprintNode>        mNodes;
		std::vector<BlueprintProp>        mProps;
		std::vector<BlueprintBinding>     mBindings;
		std::vector<BlueprintInstruction> mInstructions;
		std::vector<uint32_t>             mRefs;
		std::vector<double>               mNumbers;
		std::vector<BlueprintString>      mStrings;
		std::vector<char>                 mText;
		std::vector<std::string>          mInterned; // parallel to mStrings, for lookup
	};

	// Convenience: read a whole file, then parse it.
	bool loadBlueprintFile(const std::string& path, Blueprint& out, std::string& error);
}
