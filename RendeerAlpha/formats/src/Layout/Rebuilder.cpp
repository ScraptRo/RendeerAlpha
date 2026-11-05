#include <Layout/Rebuilder.h>

namespace RDA::Layout {

	namespace {
		// Filled once at bring-up, on the thread that then runs every watcher.
		const Rebuilder* gRebuilder = nullptr;
	}

	const Rebuilder* rebuilder() { return gRebuilder; }

	void setRebuilder(const Rebuilder* tools) { gRebuilder = tools; }
}
