#include <GraphicalObjects/Accessible.h>
#include <algorithm>
#include <cstdio>

namespace RDA {

	const char* accessibleRoleName(AccessibleRole role) {
		switch (role) {
		case AccessibleRole::None:     return "None";
		case AccessibleRole::Window:   return "Window";
		case AccessibleRole::Group:    return "Group";
		case AccessibleRole::Pane:     return "Pane";
		case AccessibleRole::Text:     return "Text";
		case AccessibleRole::Button:   return "Button";
		case AccessibleRole::CheckBox: return "CheckBox";
		case AccessibleRole::Slider:   return "Slider";
		case AccessibleRole::Edit:     return "Edit";
		case AccessibleRole::Document: return "Document";
		case AccessibleRole::ComboBox: return "ComboBox";
		case AccessibleRole::List:     return "List";
		case AccessibleRole::ListItem: return "ListItem";
		case AccessibleRole::Tree:     return "Tree";
		case AccessibleRole::TreeItem: return "TreeItem";
		case AccessibleRole::TabList:  return "TabList";
		case AccessibleRole::TabItem:  return "TabItem";
		case AccessibleRole::Image:    return "Image";
		case AccessibleRole::Menu:     return "Menu";
		}
		return "?";
	}

	namespace {
		void writeNode(const AccessibleTree& tree, int index, int depth, std::string& out) {
			const AccessibleNode& node = tree.nodes[static_cast<size_t>(index)];
			const AccessibleInfo& info = node.info;
			out.append(static_cast<size_t>(depth) * 2, ' ');
			out += accessibleRoleName(info.role);
			out += " '" + info.name + "'";
			if (!info.value.empty() || info.role == AccessibleRole::Edit) out += " value='" + info.value + "'";
			if (info.toggled >= 0)  out += info.toggled ? " on" : " off";
			if (info.expanded >= 0) out += info.expanded ? " open" : " shut";
			if (info.selected > 0)  out += " selected";
			if (info.multiSelectable) out += " multi-select";
			if (info.level > 0)     out += " level " + std::to_string(info.level);
			if (info.readOnly)      out += " read-only";
			if (info.offscreen)     out += " off-screen";
			if (info.live == 1)     out += " live";
			if (info.live == 2)     out += " live!";
			if (node.focused)       out += " focused";
			if (node.text.present && node.text.caret >= 0) {
				out += " caret " + std::to_string(node.text.caret);
				if (node.text.anchor >= 0 && node.text.anchor != node.text.caret)
					out += " selected " + std::to_string((std::min)(node.text.caret, node.text.anchor)) + ".." +
					       std::to_string((std::max)(node.text.caret, node.text.anchor));
			}
			if (info.hasRange) {
				char range[96];
				std::snprintf(range, sizeof(range), " %g [%g..%g]", info.rangeValue, info.rangeMin, info.rangeMax);
				out += range;
			}
			char where[96];
			std::snprintf(where, sizeof(where), "  @%g,%g %gx%g", node.bounds.x, node.bounds.y,
			              node.bounds.w, node.bounds.h);
			out += where;
			out += "  #" + node.automationId + "\n";
			for (const int child : node.children) writeNode(tree, child, depth + 1, out);
		}
	}

	std::string formatAccessibleTree(const AccessibleTree& tree) {
		std::string out;
		for (const int root : tree.roots) writeNode(tree, root, 0, out);
		return out;
	}

	// ---- moving through text ---------------------------------------------------------------

	std::vector<uint32_t> textCodepoints(const std::string& utf8) {
		std::vector<uint32_t> out;
		out.reserve(utf8.size());
		for (size_t i = 0; i < utf8.size();) {
			const unsigned char c = static_cast<unsigned char>(utf8[i]);
			const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
			uint32_t cp = extra == 0 ? c : (c & (0x3Fu >> extra));
			size_t k = 1;
			for (; k <= static_cast<size_t>(extra) && i + k < utf8.size(); ++k)
				cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
			out.push_back(cp);
			i += k;
		}
		return out;
	}

	std::string textSlice(const std::vector<uint32_t>& text, int from, int to) {
		const int size = static_cast<int>(text.size());
		from = std::clamp(from, 0, size);
		to = std::clamp(to, from, size);
		std::string out;
		for (int i = from; i < to; ++i) {
			const uint32_t cp = text[static_cast<size_t>(i)];
			if (cp < 0x80) out += static_cast<char>(cp);
			else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
			else if (cp < 0x10000) {
				out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			} else {
				out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
			}
		}
		return out;
	}

	namespace {
		enum class Kind { Space, Word, Punctuation, Ideograph };

		Kind kindOf(uint32_t cp) {
			if (cp <= 0x20 || cp == 0xA0 || cp == 0x3000 || (cp >= 0x2000 && cp <= 0x200B)) return Kind::Space;
			if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
			    (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
			    (cp >= 0x20000 && cp <= 0x2FA1F)) return Kind::Ideograph;
			if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_')
				return Kind::Word;
			if (cp < 0x80) return Kind::Punctuation;
			// General and CJK punctuation; everything else above ASCII -- accented Latin,
			// Greek, Cyrillic, Hangul -- is letters.
			if ((cp >= 0x2010 && cp <= 0x206F) || (cp >= 0x3001 && cp <= 0x303F) ||
			    (cp >= 0xFF01 && cp <= 0xFF0F)) return Kind::Punctuation;
			return Kind::Word;
		}

		bool wordStartsAt(const std::vector<uint32_t>& text, int i) {
			const Kind here = kindOf(text[static_cast<size_t>(i)]);
			if (here == Kind::Space) return false;
			if (i == 0) return true;
			if (here == Kind::Ideograph) return true;
			return kindOf(text[static_cast<size_t>(i - 1)]) != here;
		}

		// Every boundary of a unit, ascending, from 0 to the end inclusive. Fine for a field:
		// the longest a person types into one is a few pages.
		std::vector<int> boundaries(const std::vector<uint32_t>& text, const AccessibleText& info, TextUnit unit) {
			const int size = static_cast<int>(text.size());
			std::vector<int> out{ 0 };
			switch (unit) {
			case TextUnit::Character:
				for (int i = 1; i <= size; ++i) out.push_back(i);
				return out;
			case TextUnit::Word:
				for (int i = 1; i < size; ++i) if (wordStartsAt(text, i)) out.push_back(i);
				break;
			case TextUnit::Line:
			case TextUnit::Paragraph:
				for (const int start : info.lineStarts) if (start > 0 && start < size) out.push_back(start);
				break;
			case TextUnit::Document:
				break;
			}
			if (out.back() != size) out.push_back(size);
			return out;
		}
	}

	int textNext(const std::vector<uint32_t>& text, const AccessibleText& info, TextUnit unit, int at) {
		const std::vector<int> b = boundaries(text, info, unit);
		for (const int one : b) if (one > at) return one;
		return static_cast<int>(text.size());
	}

	int textPrevious(const std::vector<uint32_t>& text, const AccessibleText& info, TextUnit unit, int at) {
		const std::vector<int> b = boundaries(text, info, unit);
		int found = 0;
		for (const int one : b) { if (one >= at) break; found = one; }
		return found;
	}

	void textEnclosing(const std::vector<uint32_t>& text, const AccessibleText& info, TextUnit unit,
	                   int at, int& start, int& end) {
		const int size = static_cast<int>(text.size());
		at = std::clamp(at, 0, size);
		const std::vector<int> b = boundaries(text, info, unit);
		start = 0;
		end = size;
		for (size_t i = 0; i < b.size(); ++i) {
			if (b[i] <= at) start = b[i];
			if (b[i] > at) { end = b[i]; break; }
		}
		// At the very end there is nothing after: the unit is the last one.
		if (start == size && size > 0 && unit != TextUnit::Document) {
			start = textPrevious(text, info, unit, size);
			end = size;
		}
	}

	bool textCharacterBox(const AccessibleText& info, int index, Rect& out) {
		for (const AccessibleText::Line& line : info.lines) {
			const int count = static_cast<int>(line.x.size()) - 1;
			if (count < 0 || index < line.first || index > line.first + count) continue;
			const int k = index - line.first;
			const float left = line.x[static_cast<size_t>(k)];
			const float right = (k < count) ? line.x[static_cast<size_t>(k + 1)] : left;
			out = Rect{ left, line.y, right - left, line.h };
			return true;
		}
		return false;
	}
}
