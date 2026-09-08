#pragma once
#include <string>

// Emitting the TypeScript definitions a layout is written against.
//
// C++ is the single source of truth for what a widget accepts; this turns that truth
// into a .d.ts so an author gets completion and errors in the editor they already use.
// Nothing here is needed to *run* a layout — it exists so the mistakes happen while
// someone is typing rather than while a user is clicking.
//
// The variant unions come from a theme file, which is what makes `variant="primry"` a
// red squiggle instead of a silent fallback to the default style. Without one the
// variants degrade to `string`, so the definitions are still usable before a theme
// exists.
//
// Authoring only: an application has no reason to describe itself.
namespace RDA::Layout {

	struct TypesResult {
		bool        ok = false;
		std::string error;
		size_t      widgetCount  = 0;
		size_t      variantCount = 0; // names found in the theme, across all widget types
		size_t      stateCount   = 0; // signals declared, when a state schema was given
		size_t      byteSize     = 0;
	};

	// Writes the definitions to `outputPath`. `themePath` is optional; when it is empty
	// or unreadable the variant types are emitted as plain strings and the result still
	// reports ok.
	// `statePath` is the state declaration (see StateSchema.h). Given one, `state` is
	// emitted as a real interface and a misspelled signal becomes an error while typing;
	// without one it stays loosely typed and a misspelling is only caught at load time.
	TypesResult emitTypeDefinitions(const std::string& outputPath,
	                                const std::string& themePath = {},
	                                const std::string& statePath = {});
}
