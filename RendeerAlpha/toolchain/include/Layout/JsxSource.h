#pragma once
#include <Layout/WidgetSchema.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// A layout's source, as something that can be changed without being rewritten.
//
// The editor in `rda prototype` changes .tsx files that a person also edits by hand, so what it
// must never do is reformat one: a comment, a blank line, an author's own way of breaking a long
// tag all have to come through untouched. So this does not parse a layout into a tree and print
// the tree back. It finds where each element and attribute is in the text, and an edit is the
// smallest replacement that says what was asked, in the file's own indentation and line endings.
//
// It reads the shape of JSX and nothing more. What a layout means is the compiler's business, and
// that is who checks the result: the file is saved and hot reload compiles it, so an edit that
// came out wrong is a compile error with the running interface left alone.
//
// Elements only exist here if they are written out as JSX. A layout built from objects has none,
// and an element that code makes -- inside a `.map()`, or a `row={...}` -- is marked as generated,
// because one piece of text is then any number of widgets.
namespace RDA::Layout::Jsx {

	struct Attribute {
		enum class Kind { Bare, Text, Expression, Spread };

		std::string name;       // empty for a spread
		Kind        kind = Kind::Bare;
		size_t      begin = 0;  // where its name starts
		size_t      end = 0;    // one past its value
		size_t      valueBegin = 0; // inside the quotes or the braces
		size_t      valueEnd = 0;
	};

	struct Element {
		std::string tag;            // empty for a fragment
		size_t      begin = 0;      // the `<`
		size_t      nameEnd = 0;    // one past the tag's name
		size_t      openEnd = 0;    // one past the `>` that ends the opening tag
		size_t      closeBegin = 0; // the `<` of the closing tag; equal to openEnd with none
		size_t      end = 0;        // one past the whole element
		bool        selfClosing = false;
		bool        generated = false;
		int         parent = -1;
		std::vector<Attribute> attributes;
		std::vector<int>       children; // written directly inside it, in order
	};

	// What an attribute says, for showing to a person.
	struct Value {
		bool        present = false;
		// Worked out from state, or anything else only the compiler can read: it can be
		// looked at and not edited, because a word typed over it would lose the expression.
		bool        computed = false;
		std::string text;
	};

	class Document {
	public:
		explicit Document(std::string text);

		const std::string&          text() const { return mText; }
		const std::vector<Element>& elements() const { return mElements; }

		// The element a compiled id names, or -1 with `why` saying what stopped it.
		//
		// `path` is the id a blueprint has, root/left/save: each segment is an id, or the tag
		// of an element that has none, with #2 on a repeat. A component's elements are in the
		// path under the call site, so what is asked of an element's ancestors is that they
		// appear in the path in order, and not that they are all of it.
		int find(std::string_view path, std::string_view type, std::string* why = nullptr) const;

		// The one element of this type marked `editable` that a function returns, or -1. What a
		// component returns takes the id of the call site, so a path cannot name it: this is how
		// the root of a component in another file is found.
		int editableRoot(std::string_view type) const;

		const Attribute* attribute(int element, std::string_view name) const;
		Value            value(int element, std::string_view name) const;

		// An id nothing in this file uses yet: `base1`, `base2`.
		std::string uniqueId(std::string_view base) const;

		// `value` is what comes after the `=`, quotes or braces included: see formatValue.
		void setAttribute(int element, std::string_view name, std::string_view value);
		void removeAttribute(int element, std::string_view name);

		// `child` is the source of one element, which may be several lines. It goes before the
		// child at `index`, and at the end for an index past the last.
		void insertChild(int element, std::string_view child, size_t index);
		void removeElement(int element);

	private:
		void parse();
		void replace(size_t from, size_t to, std::string_view with);

		size_t      lineStart(size_t at) const;
		bool        firstOnLine(size_t at) const;
		bool        lastOnLine(size_t at) const;
		std::string indentAt(size_t at) const;
		std::string newline() const;
		std::string indentStep() const;

		std::string          mText;
		std::vector<Element> mElements;
	};

	// What a person typed for a property of this type, as what goes after the `=`. False, with
	// `why` saying what it wants, for something that is not one.
	bool formatValue(PropType type, std::string_view typed, std::string& out, std::string& why);
}
