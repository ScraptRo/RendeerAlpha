// What more than one `rda` subcommand needs: the options that apply to all of them, the
// summaries they print, the staleness test the build and the preview share, and the one
// place a mesh's declaration becomes a theme field.
//
// Kept apart from the commands so that no command includes another. A command that wants
// something here asks for it; a command that wants a different command goes through
// Commands.h.
#pragma once

#include <Layout/LayoutCompiler.h>
#include <Layout/MeshCompiler.h>
#include <Layout/StateSchema.h>
#include <Layout/ThemeSchema.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Cli {

	namespace fs = std::filesystem;

	// The command line, after the options that apply to every command have been taken out.
	// [0] is the command, as it always was.
	using Args = std::vector<std::string>;

	// Positional argument `index`, or empty when the line is not that long -- which every
	// command treats the same as an argument left out.
	inline std::string argument(const Args& args, size_t index) {
		return index < args.size() ? args[index] : std::string();
	}

	// ---- options that belong to no command ----------------------------------------------

	struct GlobalOptions {
		// --no-runtime-code, anywhere on the line: a layout that would need run-time code is
		// a build error instead, naming the binding. For an application that wants "a
		// shipped binary evaluates no JavaScript" to stay true.
		bool noRuntimeCode = false;

		// --mesh <file.ts>, repeatable: meshes whose parameters `theme` and `types` accept.
		std::vector<std::string> meshes;
	};

	const GlobalOptions& globalOptions();

	// Takes both out of the line before anything positional is read, so they can be written
	// anywhere on it and the commands that ignore them do not have to know they exist.
	void takeGlobalOptions(Args& args);

	RDA::Layout::CompileOptions compileOptions();

	// ---- numbers off the command line ---------------------------------------------------

	// Whatever std::stof accepts, so "12px" is 12 -- which is how every flag here has
	// always read a number.
	bool number(const std::string& text, float& into);

	// "X,Y", two numbers either side of the first comma, which is how every option that
	// names a point on the screen writes it.
	bool coordinates(const std::string& text, float& x, float& y);

	// ---- the files a project is made of --------------------------------------------------

	// A path as it is printed: normalised, forward slashes, the same on every platform.
	std::string shown(const fs::path& path);

	// Whether `output` has to be made: missing, or older than any of its inputs, or older
	// than this tool -- a blueprint written by an older compiler is stale even when its
	// source has not moved, and that is the case nobody remembers. Times are RDA::fileStamp's.
	bool stale(const fs::path& output, const std::vector<fs::path>& inputs, int64_t toolStamp);

	// The files in a directory with this extension, alphabetical; none for a directory that
	// is not there.
	std::vector<fs::path> filesIn(const fs::path& directory, const std::string& extension);

	// ---- meshes and the theme schema ----------------------------------------------------

	// What a mesh declares, appended to the list the theme schema is merged with. A
	// declaration is a theme field named "<mesh>.<param>", a colour or a number.
	void addMeshParams(std::vector<RDA::Layout::MeshThemeParam>& into,
	                   const std::vector<RDA::Layout::MeshParamDecl>& declared);

	// ---- state.ts -----------------------------------------------------------------------

	// state.ts's code, compiled into the schema a generated module is written from -- the
	// module carries it. A declaration with no code needs nothing; one whose code does not
	// compile is said, and false.
	bool withStateCode(RDA::Layout::StateSchema& schema, const std::string& source);

	// A declaration read and its code compiled, which is what every generator and the
	// preview start from. Says what went wrong and returns false when either step fails.
	bool loadStateWithCode(const std::string& source, RDA::Layout::StateSchema& schema);

	// ", 2 computed, 1 action, 3412 bytes of code", or nothing for a declaration with none.
	std::string stateSummary(const RDA::Layout::StateSchema& schema);

	// ", 3 run-time bindings, 2 functions, 4 KB of bytecode", or nothing for a layout with none.
	std::string runtimeSummary(const RDA::Layout::CompileResult& result);
}
