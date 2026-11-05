#pragma once
#include <GraphicalObjects/Docking.h>

// The frame a panel wears when it is not in a pane: its rectangle, its bar, its close
// box and its resize grip.
//
// A floating panel and a tile are the same thing wearing the same frame -- a rectangle
// with a bar you pick it up by. What differs is where the rectangle comes from, and that
// is decided before anything here is called. The frame used to be written out twice, once
// for each arrangement, and the two copies held the same numbers; it lives here so that a
// change to how a panel looks is made in one place.
//
// Private to the dock files. A pane's tab strip borrows the pieces that are the same in a
// strip (the close box, the grip's marks, a panel's own header) and nothing else.
namespace RDA {
	namespace DockChrome {

		// ---- arriving ------------------------------------------------------------------
		// How far along a panel's arrival has to be before it counts as having arrived.
		// Short of this it is drawn faded and a little small; at or past it, it is simply
		// there.
		inline constexpr float kArrived = 0.999f;
		inline bool arriving(float arrival) { return arrival < kArrived; }

		// Steps a panel's arrival and says how far along it is. A panel that has never been
		// drawn starts from the beginning, so it fades and grows into place instead of
		// simply being there; this is also what clears `fresh`.
		float arrive(Gui& gui, DockContainer& panel, const DockStyle& style);

		// `r` scaled about its own middle. A panel grows from slightly small as it arrives,
		// and shrinks toward the same middle as it goes.
		Rect scaledAbout(const Rect& r, float scale);

		// ---- geometry ------------------------------------------------------------------
		// The close box in the top-right of a floating panel or a tile, `barHeight` tall.
		Rect closeRect(const Rect& panel, float barHeight);
		// The resize grip in the bottom-right corner.
		Rect gripRect(const Rect& panel);

		// ---- drawing -------------------------------------------------------------------
		// The "x" in `box`, in the colour the pointer being over it asks for. Returns
		// whether the pointer is over it, since that is what the caller needs to know next.
		bool drawClose(Gui& gui, const DockContainer& panel, const DockStyle& style,
		               const Rect& box);

		// The stair of three marks that says a corner can be taken hold of. `corner` is
		// where the two edges meet; `first` is how far in the nearest mark is and `inset`
		// how far the marks sit from the edges.
		void drawGripMarks(Gui& gui, glm::vec2 corner, float first, float inset,
		                   uint32_t colour);

		// A panel's own header, drawn in `band`, and whether the band itself was taken hold
		// of -- which is the press that picks the panel up and carries it somewhere else.
		//
		// The answer is "nothing inside it claimed the press", asked after the contents have
		// painted rather than before. A button on the bar runs later and hot is
		// last-writer-wins, so asking first would hand every press to the bar and a close
		// button on a title bar would drag the window instead of closing it.
		bool drawHeaderBand(Gui& gui, DockContainer* panel, const Rect& band, bool busy);

		// What drawing a panel's frame tells whoever asked.
		struct Framed {
			bool grabbed = false;  // the panel's own header was taken hold of
			Rect shown{};          // where it was drawn, which is not always where it was asked to be
		};

		// The whole frame: the ground, the bar (the panel's header, or the engine's title
		// and close box), the contents, and the grip. `arrival` is what arrive() answered;
		// below kArrived the frame is drawn grown from small and faded, and the contents
		// fade with it. `closeBarHeight` is how tall the close box is, and the caller
		// decides it: a floating panel takes it from the default dock style and a tile from
		// its own variant's, and the two have always differed when the variants do.
		//
		// What a grab of the header means -- carrying a floating panel, carrying a tile --
		// is the arrangement's business, so it is reported rather than acted on.
		Framed drawPanel(Gui& gui, DockContainer& panel, const Rect& wanted, float arrival,
		                 bool active, float closeBarHeight, bool busy);
	}
}
