// `rda prototype` and `rda shot`, which this program only hands on.
//
// They need Vulkan and a window, and everything else here needs neither, so they are a program of
// their own: see preview/. They are listed here because they are what people type. What is handed
// over is the line as it was typed, the options that apply to every command included:
// `rda-preview` takes them out itself, and a build it does first needs --no-runtime-code as much
// as `rda build` does.
#include "Command.h"
#include "Forward.h"
#include "PreviewUsage.h"

namespace Cli {

	namespace {
		int handOver(const Args&) { return forwardToPreview(commandLine()); }

		const Command kPrototype{ __FILE__, "prototype", Section::Run, 0, kPrototypeUsage, handOver };
		const Command kShot{ __FILE__, "shot", Section::Run, 0, kShotUsage, handOver };
	}
}
