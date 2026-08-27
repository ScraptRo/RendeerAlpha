// Sandbox — the smallest complete RendeerAlpha application.
//
// This is the first file to read. It does one thing: it puts an interface on screen
// that was written in res/layouts/hello.tsx, and gives one of its buttons something to
// do.
//
// The split it demonstrates is the whole idea of the framework:
//
//   hello.tsx   what the interface *is*   — compiled ahead of time into a flat file
//   this file   what it *does*            — plain C++, no markup, no layout arithmetic
//
// Nothing here parses anything, and this program contains no compiler. The build turned
// hello.tsx into res/layouts/hello.rdab; what runs at start-up reads that file and makes
// widgets from it. Because nothing in here references the layout compiler, the linker
// leaves it -- and the JavaScript engine behind it -- out of this binary entirely.
//
// Try this first: open res/layouts/hello.tsx, change the text of a <label>, and build.
#include <RendeerAlpha.h>
#include <Layout/LayoutLoader.h>
#include <Logger/Logger.h>

#include <string>

namespace {

	const std::string kLayoutBlueprint = "res/layouts/hello.rdab";

	int gClicks = 0;

	// Builds the interface. Called once, when the window exists and before the first
	// frame.
	void buildInterface() {
		RDA::Gui& gui = getMainWindow()->gui();

		RDA::Layout::Blueprint layout;
		std::string error;
		if (!RDA::Layout::loadBlueprintFile(kLayoutBlueprint, layout, error)) {
			RDA_LOG_WARNING("layout: " << error);
			RDA::Label* message = gui.retained().add<RDA::Label>("error",
				"No layout found - build again; the build compiles hello.tsx.");
			message->rect = { 20.0f, 20.0f, 0.0f, 0.0f };
			return;
		}

		// The blueprint hangs off the retained root, which fills the window.
		RDA::Layout::instantiate(layout, gui.retained());

		// ---- behaviour ---------------------------------------------------------------
		// Widgets are found by the id the layout gave them. An id is the path to the
		// node, so a button inside a row is named for the row it is in — which is what
		// keeps three buttons made by the same helper from behaving as one.
		//
		// Wiring handlers by hand is temporary. Compiled event bindings will let the
		// layout name the handler directly; until then this is where behaviour lives.
		if (auto* button = dynamic_cast<RDA::Button*>(gui.retained().find("root/row-primary/pick"))) {
			button->onClick = []() {
				++gClicks;
				RDA::Gui& g = getMainWindow()->gui();
				if (auto* title = dynamic_cast<RDA::Label*>(g.retained().find("root/title"))) {
					title->text = "Clicked " + std::to_string(gClicks) + " time"
					            + (gClicks == 1 ? "" : "s");
				}
				// The window only redraws when something asks it to, so say so.
				rendeerRequestRedraw();
			};
		}
	}
}

int main() {
	RDA::AppConfig config;
	config.app.name = "Sandbox";
	config.app.appVersion = 1;

	config.gui.enabled = true;
	config.gui.themePath = "res/themes/sandbox.xml";

	// Nothing animates here, so frames are drawn only when something changes. An idle
	// window costs effectively no CPU.
	config.redrawMode = RDA::RedrawMode::OnDemand;
	config.viewportMode = RDA::ViewportMode::Fullscreen;
	config.threadMode = RDA::ThreadMode::Caller;

	config.onStart = []() { buildInterface(); };

	rendeerRun(config);
	return 0;
}
