#include <Layout/Rebuilder.h>
#include <Layout/LayoutCompiler.h>
#include <Layout/MeshCompiler.h>
#include <Layout/ThemeCompiler.h>

// The toolchain's half of Layout/Rebuilder.h: the three compilers hot reload uses, behind
// the table the watchers ask. Nothing on the run-time side references this file except
// through installRebuilder(), which only a hot-reload build calls -- so a build without it
// links none of the compilers.
namespace RDA::Layout {

	namespace {
		bool rebuildLayout(const std::string& source, const std::string& blueprint, std::string& error) {
			const CompileResult built = compileToFile(source, blueprint);
			if (!built.ok) error = built.error;
			return built.ok;
		}

		bool rebuildMesh(const std::string& source, const std::string& compiled, std::string& error) {
			const MeshCompileResult built = compileMeshToFile(source, compiled);
			if (!built.ok) error = built.error;
			return built.ok;
		}

		bool rebuildTheme(const std::string& source, const std::string& compiled,
		                  const std::vector<MeshThemeParam>& meshParams, std::string& error) {
			// What a mesh asks the theme for is part of what the theme may carry, so the
			// schema has to know every declaration before the file is checked against it.
			registerMeshThemeParams(meshParams);
			const ThemeCompileResult built = compileThemeToFile(source, compiled);
			if (!built.ok) error = built.error;
			return built.ok;
		}

		const Rebuilder kCompilers{ &rebuildLayout, &rebuildMesh, &rebuildTheme };
	}

	void installRebuilder() { setRebuilder(&kCompilers); }
}
