#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Compiling a mesh written in TypeScript into a .rdam.
//
// The same shape as the theme compiler: esbuild turns the module into JavaScript,
// QuickJS evaluates it, and what it returns is written out as a flat binary. The
// difference is what the module is allowed to say -- a mesh is evaluated with the
// geometry prelude (boxes, rect, rrect, union, group, weld) and with nothing that can
// see a signal, because a mesh runs at build time and there is nothing to see yet.
//
// The parameters a mesh declares come back with the result, because they are what the
// theme is then checked against. That is the one real dependency this adds to the build:
// a project's themes can only be compiled once its meshes have been.
namespace RDA::Layout {

	// One value a mesh asks the theme for. Collected so the theme compiler can accept it
	// as a field and the type emitter can put it in the generated interface.
	struct MeshParamDecl {
		std::string mesh;     // the mesh's name, which namespaces it
		std::string name;
		std::string doc;
		bool        isColour = true;
	};

	struct MeshCompileResult {
		bool                 ok = false;
		std::vector<uint8_t> bytes;
		std::string          error;
		size_t               stateCount = 0;
		size_t               vertexCount = 0;
		size_t               triangleCount = 0;
		double               transformMs = 0.0;
		double               evaluateMs = 0.0;
		std::vector<MeshParamDecl> params;
	};

	// res/mesh/tab.ts -> res/mesh/tab.rdam
	std::string meshPathFor(const std::string& sourcePath);
	// The name a mesh's parameters are namespaced under: the file's stem.
	std::string meshNameFor(const std::string& sourcePath);

	MeshCompileResult compileMesh(const std::string& sourcePath,
	                              const std::string& esbuildPath = {});
	MeshCompileResult compileMeshToFile(const std::string& sourcePath,
	                                    const std::string& outputPath,
	                                    const std::string& esbuildPath = {});
}
