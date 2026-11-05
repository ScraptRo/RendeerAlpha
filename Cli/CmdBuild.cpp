// `rda build`: a whole project by convention.
//
// A C++ project has CMake to call the other commands one file at a time; a Python, Node
// or C# project has no build system that would, and should not need one to compile an
// interface. So this walks a project's res/ and does what the CMake rules would have --
// meshes, themes, the types, the layouts, and the state in whichever language was asked
// for -- skipping whatever is already newer than its inputs.
//
// It runs as steps over one object because they share a good deal: what discovery found,
// the tally of what was made and what was kept, and the state.ts the types step has
// already read and the module step then reuses.
#include "Commands.h"

#include <Core/FileStamp.h>
#include <Core/Location.h>
#include <Layout/ModuleTransform.h>
#include <Layout/ThemeCompiler.h>
#include <Layout/TypeEmitter.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>

namespace Cli {

	namespace {

		// Which of these files are imported by another one of them.
		//
		// A folder of layouts holds screens and the components they are built from, and a
		// folder of themes holds the theme and whatever palette it reads. Only the ones
		// nothing imports are compiled: a component has props and no meaning on its own, and
		// a palette styles nothing, so compiling either produces an error about a file that
		// was never wrong. Nobody should have to name their shared file specially to be left
		// alone by a build.
		std::set<fs::path> importedByAnother(const std::vector<fs::path>& candidates) {
			std::set<fs::path> imported;
			for (const fs::path& source : candidates) {
				const std::vector<std::string> files = RDA::Layout::importedFiles(source.string());
				// [0] is the file itself; everything after it is something it pulled in.
				for (size_t i = 1; i < files.size(); ++i) {
					std::error_code ec;
					fs::path resolved = fs::weakly_canonical(files[i], ec);
					imported.insert(ec ? fs::path(files[i]) : resolved);
				}
			}
			return imported;
		}

		bool isImported(const std::set<fs::path>& imported, const fs::path& candidate) {
			std::error_code ec;
			const fs::path resolved = fs::weakly_canonical(candidate, ec);
			return imported.count(ec ? candidate : resolved) != 0;
		}

		struct BuildOptions {
			fs::path    project = ".";
			bool        python = false, node = false, csharp = false, force = false;
			std::string cppDir;
		};

		// False, with the reason printed, for an option nobody knows.
		bool parseBuildOptions(const Args& args, BuildOptions& options) {
			for (size_t i = 1; i < args.size(); ++i) {
				const std::string& arg = args[i];
				if      (arg == "--python") options.python = true;
				else if (arg == "--node")   options.node = true;
				else if (arg == "--csharp") options.csharp = true;
				else if (arg == "--force")  options.force = true;
				else if (arg == "--cpp" && i + 1 < args.size()) options.cppDir = args[++i];
				else if (!arg.empty() && arg[0] == '-') {
					std::printf("rda: unknown option %s\n", arg.c_str());
					return false;
				}
				else options.project = arg;
			}
			return true;
		}

		// One state module to write, if it was asked for.
		struct StateModule {
			bool    wanted;
			fs::path out;
			bool  (*emit)(const RDA::Layout::StateSchema&, const std::string&, std::string&);
		};

		class ProjectBuild {
		public:
			explicit ProjectBuild(BuildOptions options) : mOptions(std::move(options)) {}

			int run() {
				if (!discover()) return 1;
				buildMeshes();
				buildThemes();
				buildTypes();
				buildLayouts();
				buildStateModules();
				return report();
			}

		private:
			// What the project holds. Alphabetical in each folder, so the first theme means
			// the same thing everywhere.
			bool discover() {
				const fs::path& project = mOptions.project;
				mRes = project / "res";
				std::error_code ec;
				if (!fs::is_directory(mRes, ec)) {
					std::printf("rda: no res/ directory in %s -- a project keeps its declaration, "
					            "theme and layouts there\n", shown(project).c_str());
					return false;
				}

				mState = mRes / "state.ts";
				mHaveState = fs::is_regular_file(mState, ec);
				mMeshes  = filesIn(mRes / "mesh", ".ts");
				mThemes  = filesIn(mRes / "themes", ".ts");
				mLayouts = filesIn(mRes / "layouts", ".tsx");
				mTool = mOptions.force ? INT64_MAX : RDA::fileStamp(RDA::executablePath().string());

				// A file that another one imports is a part rather than a whole: a component,
				// or a palette. It is compiled as part of whatever imports it and never on its
				// own.
				mImportedMeshes = importedByAnother(mMeshes);
				mImportedThemes = importedByAnother(mThemes);
				mImportedLayouts = importedByAnother(mLayouts);
				return true;
			}

			// Meshes first, before the themes.
			//
			// This is the one ordering in the build that is not obvious, and it comes from
			// the meshes declaring what the theme may carry: a mesh that asks for a `seam`
			// colour adds a field to the theme's schema, so a theme cannot be checked until
			// every mesh has said what it wants. A mesh edited without its theme being
			// recompiled is the failure to watch for, which is why a mesh newer than a theme
			// makes that theme stale below.
			void buildMeshes() {
				std::vector<RDA::Layout::MeshThemeParam> meshParams;
				for (const fs::path& source : mMeshes) {
					if (isImported(mImportedMeshes, source)) { ++mParts; continue; }
					const fs::path out = RDA::Layout::meshPathFor(source.string());
					// Compiled even when the .rdam is current, because what it declares is what
					// the themes are checked against and that cannot be read back out of a
					// skipped file. It is the cheapest step in the build by a wide margin.
					if (!stale(out, { source }, mTool)) {
						keepMesh(source, out, meshParams);
						continue;
					}
					makeMesh(source, out, meshParams);
				}

				// The schema the themes below are checked against is the engine's table merged
				// with this. Registered even when empty, so a run that removed the last mesh
				// does not leave the previous run's names accepted.
				RDA::Layout::registerMeshThemeParams(meshParams);
			}

			void keepMesh(const fs::path& source, const fs::path& out,
			              std::vector<RDA::Layout::MeshThemeParam>& meshParams) {
				++mKept;
				mNewestMesh = (std::max)(mNewestMesh, RDA::fileStamp(out.string()));
				const RDA::Layout::MeshCompileResult declared =
					RDA::Layout::compileMesh(source.string());
				if (declared.ok) addMeshParams(meshParams, declared.params);
			}

			void makeMesh(const fs::path& source, const fs::path& out,
			              std::vector<RDA::Layout::MeshThemeParam>& meshParams) {
				const RDA::Layout::MeshCompileResult result =
					RDA::Layout::compileMeshToFile(source.string(), out.string());
				if (!result.ok) {
					std::printf("rda: %s\n%s\n", shown(source).c_str(), result.error.c_str());
					++mFailed;
					return;
				}
				std::printf("rda: %s -> %s  (%zu states, %zu vertices, %zu triangles)\n",
				            shown(source).c_str(), shown(out).c_str(),
				            result.stateCount, result.vertexCount, result.triangleCount);
				++mMade;
				mNewestMesh = (std::max)(mNewestMesh, RDA::fileStamp(out.string()));
				addMeshParams(meshParams, result.params);
			}

			// Themes next: the types read one, and a layout is checked against those.
			void buildThemes() {
				for (const fs::path& source : mThemes) {
					if (isImported(mImportedThemes, source)) { ++mParts; continue; }
					const fs::path out = RDA::Layout::themePathFor(source.string());
					// A mesh that moved makes every theme stale, because a theme names meshes
					// and is checked against what they declare.
					if (!stale(out, { source }, (std::max)(mTool, mNewestMesh))) { ++mKept; continue; }
					const RDA::Layout::ThemeCompileResult result =
						RDA::Layout::compileThemeToFile(source.string(), out.string());
					if (!result.ok) {
						std::printf("rda: %s\n%s\n", shown(source).c_str(), result.error.c_str());
						++mFailed;
						continue;
					}
					std::printf("rda: %s -> %s  (%zu variants)\n", shown(source).c_str(),
					            shown(out).c_str(), result.variantCount);
					++mMade;
				}
			}

			// The definitions a layout is checked against, from the first theme and the
			// state. Regenerated whenever either moves, and whenever the tool does: a .d.ts
			// that has drifted from the widget schema is worse than none.
			void buildTypes() {
				if (mLayouts.empty() && mThemes.empty()) return;

				const fs::path out = mRes / "layouts" / "rda.d.ts";
				fs::path firstTheme;
				for (const fs::path& theme : mThemes) {
					if (!isImported(mImportedThemes, theme)) { firstTheme = theme; break; }
				}
				std::vector<fs::path> inputs;
				if (!firstTheme.empty()) inputs.push_back(firstTheme);
				// What state.ts imports too: an action's signature is read from it.
				if (mHaveState) {
					for (const std::string& one : RDA::Layout::importedFiles(mState.string())) inputs.emplace_back(one);
					inputs.push_back(mState);
				}
				if (!stale(out, inputs, mTool)) { ++mKept; return; }

				std::error_code ec;
				fs::create_directories(out.parent_path(), ec);
				const RDA::Layout::TypesResult result = RDA::Layout::emitTypeDefinitions(
					out.string(), firstTheme.empty() ? std::string() : firstTheme.string(),
					mHaveState ? mState.string() : std::string());
				if (!result.ok) {
					std::printf("rda: %s\n", result.error.c_str());
					++mFailed;
				} else {
					std::printf("rda: %s  (%zu elements, %zu variants, %zu signals)\n",
					            shown(out).c_str(), result.widgetCount, result.variantCount,
					            result.stateCount);
					++mMade;
				}
				// state.ts's code is checked whenever its types are: a mistake in a computed
				// value or an action is this build's to report, whether or not a backend
				// module was asked for.
				if (result.ok && mHaveState) checkStateCode();
			}

			// state.ts, read and its code compiled at most once per run: the types need the
			// one and the generated modules carry the other.
			void checkStateCode() {
				mCheckedState = RDA::Layout::loadStateSchema(mState.string());
				mStateChecked = mCheckedState.ok;
				if (mStateChecked && !withStateCode(mCheckedState, mState.string())) {
					mStateChecked = false;
					++mFailed;
				}
			}

			// The layouts. A layout may import other files, so every one of those is an
			// input -- the same list hot reload watches.
			void buildLayouts() {
				for (const fs::path& source : mLayouts) {
					if (isImported(mImportedLayouts, source)) { ++mParts; continue; }
					const fs::path out = RDA::Layout::blueprintPathFor(source.string());
					std::vector<fs::path> inputs;
					for (const std::string& imported : RDA::Layout::importedFiles(source.string())) {
						inputs.emplace_back(imported);
					}
					if (inputs.empty()) inputs.push_back(source);
					if (!stale(out, inputs, mTool)) { ++mKept; continue; }
					const RDA::Layout::CompileResult result =
						RDA::Layout::compileToFile(source.string(), out.string(), compileOptions());
					if (!result.ok) {
						std::printf("rda: %s\n%s\n", shown(source).c_str(), result.error.c_str());
						++mFailed;
						continue;
					}
					std::printf("rda: %s -> %s  (%zu nodes, %zu bindings%s)\n", shown(source).c_str(),
					            shown(out).c_str(), result.nodeCount, result.bindingCount,
					            runtimeSummary(result).c_str());
					++mMade;
				}
			}

			// The state, in whichever languages were asked for. Into the project directory,
			// beside the program that imports it, because that is where an import resolves
			// from with nothing configured.
			void buildStateModules() {
				const fs::path& project = mOptions.project;
				const std::vector<StateModule> modules = {
					{ mOptions.python,           project / "state.py",  RDA::Layout::emitStatePython },
					{ mOptions.node,             project / "state.mjs", RDA::Layout::emitStateNode },
					{ mOptions.csharp,           project / "state.cs",  RDA::Layout::emitStateCSharp },
					{ !mOptions.cppDir.empty(),  fs::path(mOptions.cppDir) / "RdaState.h", RDA::Layout::emitStateHeader },
				};
				bool anyWanted = false;
				for (const StateModule& one : modules) anyWanted = anyWanted || one.wanted;
				if (!anyWanted) return;
				if (!mHaveState) {
					std::printf("rda: no %s -- a state module was asked for and there is no "
					            "declaration to generate it from\n", shown(mState).c_str());
					++mFailed;
					return;
				}

				RDA::Layout::StateSchema schema;
				bool loaded = false;
				// What state.ts imports is part of it now: its code ships in these modules.
				std::vector<fs::path> stateInputs;
				for (const std::string& one : RDA::Layout::importedFiles(mState.string())) stateInputs.emplace_back(one);
				if (stateInputs.empty()) stateInputs.push_back(mState);
				for (const StateModule& module : modules) {
					if (!module.wanted) continue;
					if (!stale(module.out, stateInputs, mTool)) { ++mKept; continue; }
					if (!loaded && mStateChecked) {
						schema = mCheckedState;
						loaded = true;
					}
					if (!loaded) {
						loaded = true;
						if (!loadStateWithCode(mState.string(), schema)) {
							++mFailed;
							break;
						}
					}
					std::error_code ec;
					if (module.out.has_parent_path()) fs::create_directories(module.out.parent_path(), ec);
					std::string error;
					if (!module.emit(schema, module.out.string(), error)) {
						std::printf("rda: %s\n", error.c_str());
						++mFailed;
						continue;
					}
					std::printf("rda: %s -> %s  (%zu signals, %zu commands%s)\n", shown(mState).c_str(),
					            shown(module.out).c_str(), schema.fields.size(), schema.commands.size(),
					            stateSummary(schema).c_str());
					++mMade;
				}
			}

			int report() {
				std::string partsNote;
				if (mParts) {
					partsNote = ", " + std::to_string(mParts) +
					            (mParts == 1 ? " imported by another and left to it"
					                         : " imported by others and left to them");
				}
				if (mFailed) {
					std::printf("rda: %d failed, %zu written, %zu up to date%s\n", mFailed, mMade, mKept,
					            partsNote.c_str());
					return 1;
				}
				std::printf("rda: %zu written, %zu up to date%s\n", mMade, mKept, partsNote.c_str());
				return 0;
			}

			BuildOptions mOptions;

			fs::path              mRes;
			fs::path              mState;
			bool                  mHaveState = false;
			std::vector<fs::path> mMeshes, mThemes, mLayouts;
			std::set<fs::path>    mImportedMeshes, mImportedThemes, mImportedLayouts;
			int64_t               mTool = 0;

			int    mFailed = 0;
			size_t mMade = 0, mKept = 0, mParts = 0;
			int64_t mNewestMesh = 0;

			RDA::Layout::StateSchema mCheckedState;
			bool                     mStateChecked = false;
		};
	}

	int cmdBuild(const Args& args) {
		BuildOptions options;
		if (!parseBuildOptions(args, options)) return 1;
		return ProjectBuild(std::move(options)).run();
	}
}
