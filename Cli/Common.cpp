#include "Common.h"

#include <Core/FileStamp.h>
#include <Layout/ModuleTransform.h>

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace Cli {

	namespace {
		GlobalOptions gGlobal;
	}

	const GlobalOptions& globalOptions() { return gGlobal; }

	void takeGlobalOptions(Args& args) {
		for (size_t i = 0; i < args.size();) {
			if (args[i] == "--no-runtime-code") {
				gGlobal.noRuntimeCode = true;
				args.erase(args.begin() + static_cast<ptrdiff_t>(i));
			} else if (args[i] == "--strict") {
				gGlobal.strict = true;
				args.erase(args.begin() + static_cast<ptrdiff_t>(i));
			} else if (args[i] == "--mesh" && i + 1 < args.size()) {
				gGlobal.meshes.push_back(args[i + 1]);
				args.erase(args.begin() + static_cast<ptrdiff_t>(i),
				           args.begin() + static_cast<ptrdiff_t>(i) + 2);
			} else {
				++i;
			}
		}
	}

	RDA::Layout::CompileOptions compileOptions() {
		RDA::Layout::CompileOptions options;
		options.allowRuntimeCode = !gGlobal.noRuntimeCode;
		options.strict = gGlobal.strict;
		return options;
	}

	void printNameWarnings(const std::string& source, const RDA::Layout::CompileResult& result) {
		for (const std::string& one : result.nameWarnings) {
			std::printf("rda: warning: %s: %s\n", source.c_str(), one.c_str());
		}
	}

	bool number(const std::string& text, float& into) {
		try { into = std::stof(text); return true; } catch (...) { return false; }
	}

	bool coordinates(const std::string& text, float& x, float& y) {
		const size_t comma = text.find(',');
		return comma != std::string::npos
		    && number(text.substr(0, comma), x)
		    && number(text.substr(comma + 1), y);
	}

	std::string shown(const fs::path& path) { return path.lexically_normal().generic_string(); }

	bool stale(const fs::path& output, const std::vector<fs::path>& inputs, int64_t toolStamp) {
		const int64_t have = RDA::fileStamp(output.string());
		if (have == 0) return true;
		if (have < toolStamp) return true;
		for (const fs::path& input : inputs) {
			if (RDA::fileStamp(input.string()) > have) return true;
		}
		return false;
	}

	std::vector<fs::path> filesIn(const fs::path& directory, const std::string& extension) {
		std::vector<fs::path> found;
		std::error_code ec;
		if (!fs::is_directory(directory, ec)) return found;
		for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == extension) {
				found.push_back(entry.path());
			}
		}
		// Alphabetical, so "the first theme" means the same thing on every filesystem.
		std::sort(found.begin(), found.end());
		return found;
	}

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

	std::string Languages::described() const {
		std::string out;
		const auto add = [&](const char* name, const std::string& because) {
			if (because.empty()) return;
			out += (out.empty() ? "" : ", ") + std::string(name) + " (" + because + ")";
		};
		add("Python", python);
		add("Node", node);
		add("C#", csharp);
		add("C++", cpp);
		return out;
	}

	Languages detectLanguages(const fs::path& project) {
		Languages found;
		std::error_code ec;
		const auto exists = [&](const char* name) { return fs::is_regular_file(project / name, ec); };

		// The conventional names first, so that what is said is the file a person expects to
		// hear about; the rest of the folder after.
		if (exists("app.py")) found.python = "app.py";
		if (exists("package.json")) found.node = "package.json";
		if (exists("CMakeLists.txt")) found.cpp = "CMakeLists.txt";
		if (exists("state.cs")) found.csharp = "state.cs";

		for (const fs::directory_entry& entry : fs::directory_iterator(project, ec)) {
			if (!entry.is_regular_file(ec)) continue;
			const fs::path extension = entry.path().extension();
			const std::string name = entry.path().filename().string();
			if (extension == ".py" && found.python.empty()) found.python = name;
			else if (extension == ".mjs" && found.node.empty()) found.node = name;
			else if (extension == ".csproj" && (found.csharp.empty() || found.csharp == "state.cs")) found.csharp = name;
		}
		return found;
	}

	void addMeshParams(std::vector<RDA::Layout::MeshThemeParam>& into,
	                   const std::vector<RDA::Layout::MeshParamDecl>& declared) {
		for (const RDA::Layout::MeshParamDecl& one : declared) {
			into.push_back({ one.mesh + "." + one.name, one.doc,
			                 one.isColour ? RDA::Layout::ThemeFieldType::Colour
			                              : RDA::Layout::ThemeFieldType::Number });
		}
	}

	bool withStateCode(RDA::Layout::StateSchema& schema, const std::string& source) {
		if (!schema.hasCode() || !schema.code.empty()) return true;
		const RDA::Layout::StateCodeResult code = RDA::Layout::compileStateCode(source, compileOptions());
		if (!code.ok) {
			std::printf("rda: %s\n%s\n", source.c_str(), code.error.c_str());
			return false;
		}
		schema.code = code.bytes;
		return true;
	}

	bool loadStateWithCode(const std::string& source, RDA::Layout::StateSchema& schema) {
		schema = RDA::Layout::loadStateSchema(source);
		if (!schema.ok) {
			std::printf("rda: %s\n", schema.error.c_str());
			return false;
		}
		return withStateCode(schema, source);
	}

	std::string stateSummary(const RDA::Layout::StateSchema& schema) {
		if (schema.code.empty()) return {};
		size_t computed = 0, watches = 0, actions = 0;
		for (const RDA::Layout::StateField& one : schema.fields) {
			computed += one.computed ? 1 : 0;
			watches += one.watched ? 1 : 0;
		}
		for (const RDA::Layout::StateCommand& one : schema.commands) actions += one.action ? 1 : 0;
		char text[160];
		const std::string watched = watches == 0 ? std::string()
			: ", " + std::to_string(watches) + (watches == 1 ? " watch" : " watches");
		std::snprintf(text, sizeof(text), ", %zu computed, %zu action%s%s, %zu bytes of code",
		              computed, actions, actions == 1 ? "" : "s", watched.c_str(), schema.code.size());
		return text;
	}

	std::string runtimeSummary(const RDA::Layout::CompileResult& result) {
		if (result.scriptBytes == 0) return {};
		char text[160];
		std::snprintf(text, sizeof(text), ", run-time code: %zu binding%s and %zu function%s in %zu bytes",
		              result.scriptBindings, result.scriptBindings == 1 ? "" : "s",
		              result.scriptCalls, result.scriptCalls == 1 ? "" : "s", result.scriptBytes);
		return text;
	}
}
