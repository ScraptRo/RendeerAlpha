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
#include <set>
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

		// --strict, anywhere on the line: a name the widget schema does not know is a build
		// error instead of a warning. `build` and `layout` read it; it is global only so that
		// it can be written where the person is already typing.
		bool strict = false;

		// --mesh <file.ts>, repeatable: meshes whose parameters `theme` and `types` accept.
		std::vector<std::string> meshes;
	};

	const GlobalOptions& globalOptions();

	// Takes both out of the line before anything positional is read, so they can be written
	// anywhere on it and the commands that ignore them do not have to know they exist.
	void takeGlobalOptions(Args& args);

	RDA::Layout::CompileOptions compileOptions();

	// A layout's misspelt names, one `rda: warning:` line each, for a compile that went through.
	// Under --strict they are the error instead, and there is nothing to print here.
	void printNameWarnings(const std::string& source, const RDA::Layout::CompileResult& result);

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

	// Which of these files are imported by another one of them.
	//
	// A folder of layouts holds screens and the components they are built from, and a
	// folder of themes holds the theme and whatever palette it reads. Only the ones
	// nothing imports are compiled: a component has props and no meaning on its own, and
	// a palette styles nothing, so compiling either produces an error about a file that
	// was never wrong. Nobody should have to name their shared file specially to be left
	// alone by a build. `add screen` asks the same question to know which layouts are screens.
	std::set<fs::path> importedByAnother(const std::vector<fs::path>& candidates);
	bool isImported(const std::set<fs::path>& imported, const fs::path& candidate);

	// ---- which backend a project has ----------------------------------------------------

	// What the project root says about the language of the backend, each the file that said
	// so -- empty when it did not. More than one may be set: a project with an app.py and a
	// package.json is two backends until somebody says otherwise, and guessing between them
	// would be the one wrong answer.
	struct Languages {
		std::string python;   // app.py, any other *.py
		std::string node;     // package.json, any *.mjs
		std::string csharp;   // a *.csproj, an existing state.cs
		std::string cpp;      // CMakeLists.txt

		bool any() const { return !python.empty() || !node.empty() || !csharp.empty() || !cpp.empty(); }
		// Whether anything but C++ was found: C++ is a project's build system's to generate for.
		bool wantsModule() const { return !python.empty() || !node.empty() || !csharp.empty(); }
		// "Python (app.py), Node (package.json)".
		std::string described() const;
	};

	// Looks at the project root only, never below it: res/ and node_modules/ hold plenty
	// that would answer and none of it is the backend.
	Languages detectLanguages(const fs::path& project);

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
