#include <Layout/Blueprint.h>
#include <cstring>
#include <fstream>

namespace RDA::Layout {

	// The buffer is read by casting into it, so its shape has to be exactly what the
	// writer produced. Every member is a uint32 or a float, so none of these ever need
	// padding — but that is a property worth failing the build over rather than
	// assuming, since a change that broke it would corrupt silently.
	static_assert(sizeof(BlueprintHeader)      == 40, "blueprint header layout changed");
	static_assert(sizeof(BlueprintNode)        == 28, "blueprint node layout changed");
	static_assert(sizeof(BlueprintProp)        == 16, "blueprint prop layout changed");
	static_assert(sizeof(BlueprintBinding)     == 44, "blueprint binding layout changed");
	static_assert(sizeof(BlueprintInstruction) ==  8, "blueprint instruction layout changed");
	static_assert(sizeof(BlueprintString)      ==  8, "blueprint string layout changed");

	namespace {
		// Sections follow the header in this order, each tightly packed.
		struct Sections {
			size_t nodes, props, bindings, instructions, refs, numbers, strings, text, total;
		};

		Sections sectionsFor(const BlueprintHeader& h) {
			Sections s{};
			s.nodes        = sizeof(BlueprintHeader);
			s.props        = s.nodes        + sizeof(BlueprintNode)        * h.nodeCount;
			s.bindings     = s.props        + sizeof(BlueprintProp)        * h.propCount;
			s.instructions = s.bindings     + sizeof(BlueprintBinding)     * h.bindingCount;
			s.refs         = s.instructions + sizeof(BlueprintInstruction) * h.instructionCount;
			s.numbers      = s.refs         + sizeof(uint32_t)             * h.refCount;
			// The number pool holds doubles at a four-byte-aligned offset, so it is read
			// with memcpy rather than by pointing at it. Bindings are materialised once,
			// so the copy costs nothing worth avoiding.
			s.strings      = s.numbers      + sizeof(double)               * h.numberCount;
			s.text         = s.strings      + sizeof(BlueprintString)      * h.stringCount;
			s.total        = s.text         + h.stringBytes;
			return s;
		}
	}

	void Blueprint::reset() {
		mBytes.clear();
		mHeader = nullptr; mNodes = nullptr; mProps = nullptr;
		mBindings = nullptr; mInstructions = nullptr; mRefs = nullptr; mNumbers = nullptr;
		mStrings = nullptr; mText = nullptr;
	}

	bool Blueprint::parse(std::vector<uint8_t> bytes, std::string& error) {
		reset();

		if (bytes.size() < sizeof(BlueprintHeader)) {
			error = "too small to hold a header";
			return false;
		}

		BlueprintHeader header{};
		std::memcpy(&header, bytes.data(), sizeof(header));

		if (header.magic != kBlueprintMagic) {
			error = "not a blueprint (bad magic)";
			return false;
		}
		if (header.version != kBlueprintVersion) {
			error = "blueprint version " + std::to_string(header.version) +
			        ", this build reads version " + std::to_string(kBlueprintVersion);
			return false;
		}

		const Sections at = sectionsFor(header);
		if (at.total != bytes.size()) {
			error = "size mismatch: header describes " + std::to_string(at.total) +
			        " bytes, file has " + std::to_string(bytes.size());
			return false;
		}

		mBytes  = std::move(bytes);
		const uint8_t* base = mBytes.data();
		mHeader       = reinterpret_cast<const BlueprintHeader*>(base);
		mNodes        = reinterpret_cast<const BlueprintNode*>(base + at.nodes);
		mProps        = reinterpret_cast<const BlueprintProp*>(base + at.props);
		mBindings     = reinterpret_cast<const BlueprintBinding*>(base + at.bindings);
		mInstructions = reinterpret_cast<const BlueprintInstruction*>(base + at.instructions);
		mRefs         = reinterpret_cast<const uint32_t*>(base + at.refs);
		mNumbers      = base + at.numbers;
		mStrings      = reinterpret_cast<const BlueprintString*>(base + at.strings);
		mText         = reinterpret_cast<const char*>(base + at.text);

		// Everything below is checked once, here, so nothing downstream has to. A
		// caller that got true back may index freely without checking again.
		for (uint32_t i = 0; i < header.stringCount; ++i) {
			const BlueprintString& s = mStrings[i];
			if (static_cast<size_t>(s.offset) + s.length > header.stringBytes) {
				error = "string " + std::to_string(i) + " runs past the table";
				reset();
				return false;
			}
		}
		for (uint32_t i = 0; i < header.propCount; ++i) {
			const BlueprintProp& p = mProps[i];
			if (p.key >= header.stringCount ||
			    p.kind > static_cast<uint32_t>(PropKind::Bool) ||
			    (p.kind == static_cast<uint32_t>(PropKind::String) && p.text >= header.stringCount)) {
				error = "prop " + std::to_string(i) + " names something that is not there";
				reset();
				return false;
			}
		}
		for (uint32_t i = 0; i < header.refCount; ++i) {
			if (mRefs[i] >= header.stringCount) {
				error = "binding reference " + std::to_string(i) + " names no string";
				reset();
				return false;
			}
		}
		for (uint32_t i = 0; i < header.bindingCount; ++i) {
			const BlueprintBinding& b = mBindings[i];
			const bool bad =
				b.node >= header.nodeCount ||
				b.key >= header.stringCount ||
				b.kind > static_cast<uint32_t>(BindingKind::Event) ||
				static_cast<size_t>(b.firstCode)   + b.codeCount   > header.instructionCount ||
				static_cast<size_t>(b.firstNumber) + b.numberCount > header.numberCount ||
				static_cast<size_t>(b.firstText)   + b.textCount   > header.refCount ||
				static_cast<size_t>(b.firstSignal) + b.signalCount > header.refCount;
			if (bad) {
				error = "binding " + std::to_string(i) + " is not well formed";
				reset();
				return false;
			}
		}
		for (uint32_t i = 0; i < header.nodeCount; ++i) {
			const BlueprintNode& n = mNodes[i];
			const bool badStrings  = n.type >= header.stringCount || n.id >= header.stringCount;
			const bool badChildren = static_cast<size_t>(n.firstChild) + n.childCount > header.nodeCount;
			const bool badProps    = static_cast<size_t>(n.firstProp) + n.propCount > header.propCount;
			// Breadth-first means a parent is always earlier in the array than its
			// children. The loader leans on that to build the tree in one forward pass,
			// so it is checked here rather than trusted.
			//
			// Several roots are allowed. A layout has one, but a compiled theme is a list
			// of variants with no common parent, and the ordering guarantee is the same
			// either way: whatever a node's parent is, it came first.
			const bool badParent = (n.parent != kNoParent) && (n.parent >= i);
			if (badStrings || badChildren || badProps || badParent) {
				error = "node " + std::to_string(i) + " is not well formed";
				reset();
				return false;
			}
		}

		return true;
	}

	Program Blueprint::programFor(const BlueprintBinding& binding) const {
		Program program;
		program.code.reserve(binding.codeCount);
		for (uint32_t i = 0; i < binding.codeCount; ++i) {
			const BlueprintInstruction& raw = mInstructions[binding.firstCode + i];
			program.code.push_back({ static_cast<Op>(raw.op), raw.operand });
		}
		program.numbers.resize(binding.numberCount);
		if (binding.numberCount > 0) {
			std::memcpy(program.numbers.data(),
			            mNumbers + sizeof(double) * binding.firstNumber,
			            sizeof(double) * binding.numberCount);
		}
		program.texts.reserve(binding.textCount);
		for (uint32_t i = 0; i < binding.textCount; ++i) {
			program.texts.emplace_back(string(mRefs[binding.firstText + i]));
		}
		program.signals.reserve(binding.signalCount);
		for (uint32_t i = 0; i < binding.signalCount; ++i) {
			program.signals.emplace_back(string(mRefs[binding.firstSignal + i]));
		}
		return program;
	}

	std::string_view Blueprint::string(uint32_t index) const {
		if (!mHeader || index >= mHeader->stringCount) return {};
		const BlueprintString& s = mStrings[index];
		return std::string_view(mText + s.offset, s.length);
	}

	const BlueprintProp* Blueprint::find(const BlueprintNode& node, std::string_view key) const {
		for (uint32_t i = 0; i < node.propCount; ++i) {
			const BlueprintProp& p = mProps[node.firstProp + i];
			if (string(p.key) == key) return &p;
		}
		return nullptr;
	}

	std::string_view Blueprint::text(const BlueprintNode& node, std::string_view key,
	                                 std::string_view fallback) const {
		const BlueprintProp* p = find(node, key);
		if (!p || p->kind != static_cast<uint32_t>(PropKind::String)) return fallback;
		return string(p->text);
	}

	float Blueprint::number(const BlueprintNode& node, std::string_view key, float fallback) const {
		const BlueprintProp* p = find(node, key);
		if (!p || p->kind == static_cast<uint32_t>(PropKind::String)) return fallback;
		return p->number;
	}

	bool Blueprint::boolean(const BlueprintNode& node, std::string_view key, bool fallback) const {
		const BlueprintProp* p = find(node, key);
		if (!p || p->kind == static_cast<uint32_t>(PropKind::String)) return fallback;
		return p->number != 0.0f;
	}

	// ---- building --------------------------------------------------------------------

	uint32_t BlueprintBuilder::intern(std::string_view text) {
		for (size_t i = 0; i < mInterned.size(); ++i) {
			if (mInterned[i] == text) return static_cast<uint32_t>(i);
		}
		BlueprintString entry{};
		entry.offset = static_cast<uint32_t>(mText.size());
		entry.length = static_cast<uint32_t>(text.size());
		mText.insert(mText.end(), text.begin(), text.end());
		mText.push_back(0); // not counted in length; there so a debugger can print it
		mStrings.push_back(entry);
		mInterned.emplace_back(text);
		return static_cast<uint32_t>(mStrings.size() - 1);
	}

	uint32_t BlueprintBuilder::addNode(uint32_t typeString, uint32_t idString, uint32_t parent) {
		BlueprintNode node{};
		node.type   = typeString;
		node.id     = idString;
		node.parent = parent;
		mNodes.push_back(node);
		return static_cast<uint32_t>(mNodes.size() - 1);
	}

	void BlueprintBuilder::setChildRange(uint32_t node, uint32_t firstChild, uint32_t childCount) {
		mNodes[node].firstChild = firstChild;
		mNodes[node].childCount = childCount;
	}

	void BlueprintBuilder::addProp(uint32_t node, uint32_t keyString, PropKind kind,
	                               float number, uint32_t textString) {
		BlueprintNode& owner = mNodes[node];
		// A node keeps its props contiguous, so they are written while that node is the
		// one at the tail of the prop array. The compiler fills one node at a time,
		// which satisfies this by construction; anything else would need a relocation
		// pass that has no caller.
		if (owner.propCount == 0) owner.firstProp = static_cast<uint32_t>(mProps.size());
		else if (owner.firstProp + owner.propCount != mProps.size()) return;

		BlueprintProp prop{};
		prop.key    = keyString;
		prop.kind   = static_cast<uint32_t>(kind);
		prop.number = number;
		prop.text   = textString;
		mProps.push_back(prop);
		++owner.propCount;
	}

	void BlueprintBuilder::addBinding(uint32_t node, uint32_t keyString, BindingKind kind,
	                                  const Program& program) {
		BlueprintBinding record{};
		record.node = node;
		record.key  = keyString;
		record.kind = static_cast<uint32_t>(kind);

		record.firstCode = static_cast<uint32_t>(mInstructions.size());
		record.codeCount = static_cast<uint32_t>(program.code.size());
		for (const Instruction& instruction : program.code) {
			mInstructions.push_back({ static_cast<uint32_t>(instruction.op), instruction.operand });
		}

		record.firstNumber = static_cast<uint32_t>(mNumbers.size());
		record.numberCount = static_cast<uint32_t>(program.numbers.size());
		mNumbers.insert(mNumbers.end(), program.numbers.begin(), program.numbers.end());

		// Texts and signal names go through the same string table as everything else, so
		// a name that also appears as a widget id or a property is stored once. The
		// reference pool is what keeps a program's list of them contiguous.
		record.firstText = static_cast<uint32_t>(mRefs.size());
		record.textCount = static_cast<uint32_t>(program.texts.size());
		for (const std::string& text : program.texts) mRefs.push_back(intern(text));

		record.firstSignal = static_cast<uint32_t>(mRefs.size());
		record.signalCount = static_cast<uint32_t>(program.signals.size());
		for (const std::string& name : program.signals) mRefs.push_back(intern(name));

		mBindings.push_back(record);
	}

	std::vector<uint8_t> BlueprintBuilder::finish() const {
		BlueprintHeader header{};
		header.magic       = kBlueprintMagic;
		header.version     = kBlueprintVersion;
		header.nodeCount        = static_cast<uint32_t>(mNodes.size());
		header.propCount        = static_cast<uint32_t>(mProps.size());
		header.bindingCount     = static_cast<uint32_t>(mBindings.size());
		header.instructionCount = static_cast<uint32_t>(mInstructions.size());
		header.refCount         = static_cast<uint32_t>(mRefs.size());
		header.numberCount      = static_cast<uint32_t>(mNumbers.size());
		header.stringCount      = static_cast<uint32_t>(mStrings.size());
		header.stringBytes      = static_cast<uint32_t>(mText.size());

		const Sections at = sectionsFor(header);
		std::vector<uint8_t> out(at.total);
		uint8_t* base = out.data();
		std::memcpy(base, &header, sizeof(header));
		if (!mNodes.empty())
			std::memcpy(base + at.nodes, mNodes.data(), sizeof(BlueprintNode) * mNodes.size());
		if (!mProps.empty())
			std::memcpy(base + at.props, mProps.data(), sizeof(BlueprintProp) * mProps.size());
		if (!mBindings.empty())
			std::memcpy(base + at.bindings, mBindings.data(), sizeof(BlueprintBinding) * mBindings.size());
		if (!mInstructions.empty())
			std::memcpy(base + at.instructions, mInstructions.data(), sizeof(BlueprintInstruction) * mInstructions.size());
		if (!mRefs.empty())
			std::memcpy(base + at.refs, mRefs.data(), sizeof(uint32_t) * mRefs.size());
		if (!mNumbers.empty())
			std::memcpy(base + at.numbers, mNumbers.data(), sizeof(double) * mNumbers.size());
		if (!mStrings.empty())
			std::memcpy(base + at.strings, mStrings.data(), sizeof(BlueprintString) * mStrings.size());
		if (!mText.empty())
			std::memcpy(base + at.text, mText.data(), mText.size());
		return out;
	}

	bool loadBlueprintFile(const std::string& path, Blueprint& out, std::string& error) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) {
			error = "cannot open " + path;
			return false;
		}
		const std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
			error = "cannot read " + path;
			return false;
		}
		return out.parse(std::move(bytes), error);
	}
}
