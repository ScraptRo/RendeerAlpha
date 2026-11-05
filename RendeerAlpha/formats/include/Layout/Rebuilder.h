#pragma once
#include <Layout/ThemeSchema.h>
#include <string>
#include <vector>

// The one way the running engine reaches a compiler.
//
// Hot reload turns an edit into a new artifact while the application runs: a .tsx into a
// .rdab, a mesh into a .rdam, a theme into a .rdth. What does that is build tooling --
// esbuild, a JavaScript engine evaluating the source, the checks -- while the watchers
// that ask for it are part of every application. So the watchers include no compiler:
// they ask through this table, which is empty unless something that links the compilers
// filled it. A build without hot reload never does, contains no compiler, and its watchers
// re-read the artifacts something else wrote.
namespace RDA::Layout {

	struct Rebuilder {
		// A layout, .tsx to .rdab. False with the reason when it does not compile; nothing
		// is written then, so what is on screen stays what last compiled.
		bool (*layout)(const std::string& source, const std::string& blueprint,
		               std::string& error) = nullptr;

		// A mesh, .ts to .rdam.
		bool (*mesh)(const std::string& source, const std::string& compiled,
		             std::string& error) = nullptr;

		// A theme, .ts to .rdth, checked against the engine's schema plus what the meshes
		// declare -- `meshParams`, already namespaced ("tab.seam").
		bool (*theme)(const std::string& source, const std::string& compiled,
		              const std::vector<MeshThemeParam>& meshParams, std::string& error) = nullptr;
	};

	// What is installed, or nullptr: this build cannot compile, and watching means
	// watching the artifacts.
	const Rebuilder* rebuilder();

	// Fills the slot with the toolchain's compilers. Defined beside them, so only a build
	// that links the compilers can call it; the engine does when hot reload is on, before
	// anything is opened.
	void installRebuilder();

	// Puts a different table in, or empties the slot with nullptr.
	void setRebuilder(const Rebuilder* tools);
}
