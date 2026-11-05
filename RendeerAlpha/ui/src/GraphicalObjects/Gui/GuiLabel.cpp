#include <GraphicalObjects/Gui.h>
#include <algorithm>

// Gui, labels: a run of text at a position, in one colour or in several.
namespace RDA {

	void Gui::label(const char* text, glm::vec2 pos, uint32_t color, TextStyle font) {
		if (!mCanvas.font()) return;
		mCanvas.addText(pos.x, pos.y + mCanvas.baseline(font), text, color, font);
	}

	void Gui::labelSpans(const char* text, int begin, int count, glm::vec2 pos,
	                     uint32_t base, const TextSpan* spans, size_t spanCount,
	                     TextStyle font) {
		if (!mCanvas.font() || !text || count <= 0) return;
		float pen = pos.x;
		const float baseY = pos.y + mCanvas.baseline(font);
		int at = begin;
		const int end = begin + count;

		// Walk the slice, drawing whatever is between here and the next boundary. The
		// spans are sorted and disjoint, so this is one pass over both -- no search per
		// character, and no substring anywhere.
		size_t next = 0;
		while (next < spanCount && spans[next].start + spans[next].length <= at) ++next;

		while (at < end) {
			if (next < spanCount && spans[next].start <= at) {
				// Inside a span: draw to whichever comes first, its end or the slice's.
				const int stop = (std::min)(end, spans[next].start + spans[next].length);
				pen = mCanvas.addTextRange(pen, baseY, text + at, stop - at, spans[next].color, font);
				at = stop;
				if (at >= spans[next].start + spans[next].length) ++next;
				continue;
			}
			// Outside one: draw to where the next begins, or to the end.
			const int stop = (next < spanCount) ? (std::min)(end, spans[next].start) : end;
			pen = mCanvas.addTextRange(pen, baseY, text + at, stop - at, base, font);
			at = stop;
		}
	}
}
