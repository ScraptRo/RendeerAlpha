#include <Layout/StyleWatch.h>
#include <GraphicalObjects/GuiTheme.h>
#include <GraphicalObjects/MeshAsset.h>
#include <Logger/Logger.h>

#if RDA_ENABLE_HOT_RELOAD_BUILD
#include <Layout/MeshCompiler.h>
#include <Layout/ThemeCompiler.h>
#include <Layout/ThemeSchema.h>
#endif

#include <chrono>
#include <filesystem>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;

		int64_t nowMicros() {
			return std::chrono::duration_cast<std::chrono::microseconds>(
			           Clock::now().time_since_epoch()).count();
		}

		// How often the files are looked at. The layout host checks every update, which at
		// sixty frames a second is sixty stat()s per file per second for a question whose
		// answer changes when a person presses Ctrl+S. A sixth of a second is still
		// instant to the person and is two orders of magnitude less asking.
		constexpr int64_t kCheckEveryMicros = 150'000;

		// The file's timestamp, or 0 when it is not there. Zero is a fine "no file": one
		// that appears later reads as a change, which is what should happen.
		int64_t stampOf(const std::string& path) {
			if (path.empty()) return 0;
			std::error_code ec;
			const auto written = std::filesystem::last_write_time(path, ec);
			if (ec) return 0;
			return written.time_since_epoch().count();
		}

		// The source that would have produced this artifact, by the convention the build
		// already follows: `rda build` writes each .rdth and .rdam beside its source. Empty
		// when there is no such file, which is the shipped case -- then only the artifact
		// is watched and something else is expected to write it.
		std::string sourceBeside(const std::string& compiled) {
			if (compiled.empty()) return {};
			std::filesystem::path path(compiled);
			path.replace_extension(".ts");
			std::error_code ec;
			return std::filesystem::exists(path, ec) ? path.string() : std::string();
		}

#if RDA_ENABLE_HOT_RELOAD_BUILD
		// What namespaces a mesh's parameters: the file's own stem, so res/mesh/tab.rdam
		// declares "tab.seam". The same answer meshNameFor gives for its source, which is
		// what makes the two sides agree.
		//
		// Only a build that can compile a theme needs this, and GCC says so about a static
		// function nothing calls.
		std::string meshNamespace(const std::string& compiled) {
			return std::filesystem::path(compiled).stem().string();
		}
#endif
	}

	void StyleWatch::open(RDA::Theme& theme, std::string compiledThemePath) {
		mTheme = &theme;
		mThemeFile = Watched{};
		mThemeFile.compiled = std::move(compiledThemePath);
		mThemeFile.source = sourceBeside(mThemeFile.compiled);
		mMeshes.clear();
		mLastError.clear();

		if (!canCompile() && !mThemeFile.source.empty()) {
			// Worth saying rather than quietly doing less: there is a theme source sitting
			// beside the artifact and this build cannot turn one into the other. It will
			// still reload when something else writes a new .rdth.
			RDA_LOG_INFO("style: this build cannot compile " << mThemeFile.source
			             << "; watching the compiled theme and meshes only");
		}

		// Taken after the first look, so the first check does not report a change that is
		// only "this has not been looked at before" and reload for nothing.
		restamp(mThemeFile);
		noteMeshes();
		mNextCheckMicros = nowMicros() + kCheckEveryMicros;
	}

	// One artifact and its source as a single number. Mixed rather than summed: two files
	// whose times move in opposite directions by the same amount would otherwise look like
	// nothing happened.
	//
	// The source only counts in a build that can compile it. In one that cannot, editing a
	// .ts changes nothing anybody can see, and re-reading the artifact beside it would be a
	// reload and a frame for nothing.
	int64_t StyleWatch::stampPair(const Watched& one) const {
		const int64_t artifact = stampOf(one.compiled);
		return canCompile() ? artifact * 31 + stampOf(one.source) : artifact;
	}

	bool StyleWatch::moved(Watched& one) {
		const int64_t stamp = stampPair(one);
		if (stamp == one.stamp) return false;
		one.stamp = stamp;
		return true;
	}

	void StyleWatch::restamp(Watched& one) {
		one.stamp = stampPair(one);
	}

	// A mesh is read the first time something draws with it, so the set grows while an
	// application runs: a variant nobody has shown yet has asked for nothing. Nothing is
	// ever dropped -- clearing the library empties it by design, and a path that stops
	// being drawn costs one stat() to keep watching and would cost a reload to forget.
	void StyleWatch::noteMeshes() {
		for (const std::string& path : widgetMeshes().paths()) {
			bool known = false;
			for (const Watched& one : mMeshes) {
				if (one.compiled == path) { known = true; break; }
			}
			if (known) continue;
			Watched one;
			one.compiled = path;
			one.source = sourceBeside(path);
			restamp(one);
			mMeshes.push_back(std::move(one));
		}
	}

	bool StyleWatch::reloadMeshes() {
		bool any = false;
		for (Watched& one : mMeshes) {
			if (!moved(one)) continue;

#if RDA_ENABLE_HOT_RELOAD_BUILD
			if (!one.source.empty()) {
				const MeshCompileResult built = compileMeshToFile(one.source, one.compiled);
				if (!built.ok) {
					// The shape on screen is the last one that compiled, and it stays
					// there. A mesh with a mistake in it should cost a message, not the
					// window somebody is working in.
					mLastError = built.error;
					RDA_LOG_WARNING("mesh: " << built.error);
					// Stamped anyway: the file is as it is, and reporting the same error
					// seven times a second until it is fixed is not help.
					restamp(one);
					continue;
				}
				mLastError.clear();
				RDA_LOG_INFO("mesh: reloaded " << one.source);
			}
			// After the write this process just did, so that write is not read back as
			// another change on the very next check.
			restamp(one);
#endif
			any = true;
		}

		if (!any) return false;

		// Re-read on the next draw, and tell the theme to forget what it resolved against
		// the meshes that are about to be destroyed -- see Theme::forgetResolvedMeshes.
		widgetMeshes().clear();
		if (mTheme) mTheme->forgetResolvedMeshes();
		return true;
	}

	bool StyleWatch::reloadTheme() {
		if (!mTheme || mThemeFile.compiled.empty()) return false;
		if (!moved(mThemeFile)) return false;

#if RDA_ENABLE_HOT_RELOAD_BUILD
		if (!mThemeFile.source.empty()) {
			// What a mesh asks the theme for is part of what the theme may carry, so the
			// schema has to know every declaration before the file is checked against it.
			// Read out of the compiled meshes rather than by evaluating their sources
			// again: the engine has them parsed already, and a mesh shipped without its
			// source still contributes its parameters this way.
			//
			// find() re-reads anything the mesh pass above cleared, which is also what the
			// next frame would have done.
			std::vector<MeshThemeParam> declared;
			for (const Watched& one : mMeshes) {
				const WidgetMesh* mesh = widgetMeshes().find(one.compiled);
				if (!mesh) continue;
				const std::string space = meshNamespace(one.compiled);
				for (const MeshParam& param : mesh->params()) {
					declared.push_back({ space + "." + param.name, param.doc,
					                     param.kind == MeshParamKind::Colour
					                         ? ThemeFieldType::Colour
					                         : ThemeFieldType::Number });
				}
			}
			registerMeshThemeParams(declared);

			const ThemeCompileResult built =
				compileThemeToFile(mThemeFile.source, mThemeFile.compiled);
			if (!built.ok) {
				mLastError = built.error;
				RDA_LOG_WARNING("theme: " << built.error);
				restamp(mThemeFile);
				return false;
			}
			mLastError.clear();
			RDA_LOG_INFO("theme: reloaded " << mThemeFile.source);
		}
		restamp(mThemeFile);
#endif

		// Replaced rather than merged: a line deleted from a theme should put that value
		// back to the engine's default, and the loop this exists for is mostly deleting a
		// line and looking again.
		if (!mTheme->replaceWithFile(mThemeFile.compiled)) {
			mLastError = "the compiled theme would not load: " + mThemeFile.compiled;
			return false;
		}
		return true;
	}

	bool StyleWatch::reloadIfChanged() {
		if (!mTheme) return false;

		const int64_t now = nowMicros();
		if (now < mNextCheckMicros) return false;
		mNextCheckMicros = now + kCheckEveryMicros;

		noteMeshes();
		// Meshes first, in the order the build uses: a theme is checked against what they
		// declare. A theme reload after a mesh reload therefore sees the new declarations,
		// which is what lets somebody add a parameter and set it in the same minute.
		const bool meshes = reloadMeshes();
		const bool theme = reloadTheme();
		return meshes || theme;
	}

	StyleWatch& styleWatch() {
		// Never destroyed, for the reason the other engine tables are not: an application's
		// globals outlive this translation unit's statics.
		static StyleWatch* watch = new StyleWatch();
		return *watch;
	}
}
