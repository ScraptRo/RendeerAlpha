#pragma once
#include <string>
#include <vector>

// What a TypeScript module pulls in, read off its import lines.
//
// Both sides need it: the build, to know what to rebuild when a file changes, and the
// running engine, to know what to watch. It is a scan of the text and nothing more -- no
// esbuild, no JavaScript engine -- which is why it is not with the compilers: watching a
// file does not take a compiler, and a build without one still watches.
namespace RDA::Layout {

	// Every file `entry` imports, transitively, with `entry` first. What hot reload
	// watches: since a module may be split across files, watching only the one that
	// was named would miss an edit to any of the others.
	std::vector<std::string> importedFiles(const std::string& entry);
}
