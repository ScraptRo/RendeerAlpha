#include "TextEdit.h"
#include <Core/Utf8.h>
#include <algorithm>

// What a text field tells the accessibility collector: its text geometry, for a screen
// reader that asks where a character is.
namespace RDA::TextEdit {

	void reportText(Frame& f) {
		// For a screen reader: where the caret and the selection are, where each line
		// starts, and where each character is on the lines on screen -- in characters,
		// which is what a screen reader counts in. Only while one is listening.
		if (f.gui.accessibilityWanted() && f.font) {
			AccessibleText info;
			info.present = true;
			// Bytes to characters, once, in one pass: every offset asked for below is
			// a byte offset this walk passes.
			std::vector<int> charAt(f.text.size() + 1, 0);
			int characters = 0;
			for (size_t b = 0; b < f.text.size(); ++b) {
				charAt[b] = characters;
				if ((static_cast<unsigned char>(f.text[b]) & 0xC0) != 0x80) ++characters;
			}
			charAt[f.text.size()] = characters;
			auto chars = [&](int bytes) { return charAt[static_cast<size_t>(std::clamp(bytes, 0, static_cast<int>(f.text.size())))]; };
			info.caret = chars(f.st.caret);
			info.anchor = f.st.selectAnchor >= 0 ? chars(f.st.selectAnchor) : info.caret;
			info.lineStarts.reserve(f.starts.size());
			for (const int start : f.starts) info.lineStarts.push_back(chars(start));
			const int firstShown = (std::max)(0, static_cast<int>(f.st.scrollY / f.lineH));
			const int lastShown = (std::min)(f.lineCount - 1, firstShown + static_cast<int>(f.rect.h / f.lineH) + 1);
			for (int ln = firstShown; ln <= lastShown; ++ln) {
				AccessibleText::Line line;
				line.first = chars(f.starts[static_cast<size_t>(ln)]);
				line.y = f.contentTop + static_cast<float>(ln) * f.lineH - f.st.scrollY;
				line.h = f.lineH;
				float x = f.contentLeft - f.st.scrollX;
				line.x.push_back(x);
				const int begin = f.starts[static_cast<size_t>(ln)];
				const int end = begin + lineLength(f.starts, f.text, ln);
				for (int b = begin; b < end;) {
					uint32_t cp = 0;
					const size_t step = Utf8::decode(f.text.data() + b, f.text.data() + end, cp);
					if (step == 0) break;
					x += f.font->advance(cp);
					line.x.push_back(x);
					b += static_cast<int>(step);
				}
				info.lines.push_back(std::move(line));
			}
			f.gui.noteAccessibleText(f.wid, std::move(info));
		}
	}
}
