#include <GraphicalSrc/ShaderCompiler.h>
#include <Logger/Logger.h>
#include <shaderc/shaderc.hpp>
#include <filesystem>
#include <fstream>
#include <functional>

namespace RDA {

	namespace {
		shaderc_shader_kind toShadercKind(ShaderStage stage) {
			switch (stage) {
			case ShaderStage::Vertex:      return shaderc_vertex_shader;
			case ShaderStage::Fragment:    return shaderc_fragment_shader;
			case ShaderStage::Compute:     return shaderc_compute_shader;
			case ShaderStage::Geometry:    return shaderc_geometry_shader;
			case ShaderStage::TessControl: return shaderc_tess_control_shader;
			case ShaderStage::TessEval:    return shaderc_tess_evaluation_shader;
			default:                       return shaderc_glsl_infer_from_source;
			}
		}

		// Where compiled SPIR-V is cached between runs.
		const std::filesystem::path kCacheDir = "shader_cache";

		bool readTextFile(const std::string& path, std::string& out) {
			std::ifstream file(path, std::ios::in | std::ios::binary);
			if (!file) return false;
			std::ostringstream ss;
			ss << file.rdbuf();
			out = ss.str();
			return true;
		}

		bool readSpirvFile(const std::filesystem::path& path, std::vector<uint32_t>& out) {
			std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
			if (!file) return false;
			std::streamsize size = file.tellg();
			if (size <= 0 || (size % sizeof(uint32_t)) != 0) return false;
			file.seekg(0);
			out.resize(static_cast<size_t>(size) / sizeof(uint32_t));
			return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
		}

		void writeSpirvFile(const std::filesystem::path& path, const std::vector<uint32_t>& spirv) {
			std::error_code ec;
			std::filesystem::create_directories(kCacheDir, ec);
			std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
			if (file) {
				file.write(reinterpret_cast<const char*>(spirv.data()),
				           static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
			}
		}
	}

	// ---- GLSL front-end (shaderc) --------------------------------------------------
	ShaderCompileResult GlslShaderCompiler::compile(const std::string& source,
	                                                ShaderStage stage,
	                                                const std::string& name) {
		ShaderCompileResult result;

		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		options.SetOptimizationLevel(shaderc_optimization_level_performance);
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

		shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
			source, toShadercKind(stage), name.c_str(), options);

		if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
			result.error = module.GetErrorMessage();
			return result;
		}
		result.spirv.assign(module.cbegin(), module.cend());
		return result;
	}

	// ---- Default compiler selection ------------------------------------------------
	static GlslShaderCompiler gDefaultGlslCompiler;
	static IShaderCompiler*   gActiveCompiler = &gDefaultGlslCompiler;

	IShaderCompiler& defaultShaderCompiler() { return *gActiveCompiler; }
	void setDefaultShaderCompiler(IShaderCompiler* compiler) {
		gActiveCompiler = compiler ? compiler : &gDefaultGlslCompiler;
	}

	// ---- Compile with SPIR-V cache -------------------------------------------------
	ShaderCompileResult compileSourceCached(const std::string& source, ShaderStage stage, const std::string& name) {
		ShaderCompileResult result;

		IShaderCompiler& compiler = defaultShaderCompiler();

		// Cache key: language + stage + source-file name + content hash. Because the
		// content hash is in the name, any edit to the source produces a new file and
		// forces a recompile; an unchanged shader hits the cache.
		size_t hash = std::hash<std::string>{}(source);
		hash ^= std::hash<int>{}(static_cast<int>(stage)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

		std::filesystem::path srcName = std::filesystem::path(name).filename();
		std::ostringstream cacheName;
		cacheName << compiler.languageId() << '_' << srcName.string()
		          << '_' << std::hex << hash << ".spv";
		std::filesystem::path cachePath = kCacheDir / cacheName.str();

		if (readSpirvFile(cachePath, result.spirv) && !result.spirv.empty()) {
			return result; // cache hit
		}

		result = compiler.compile(source, stage, name);
		if (!result.ok()) {
			RDA_LOG_ERROR("Shader compile failed (" << name << "):\n" << result.error);
			return result;
		}

		writeSpirvFile(cachePath, result.spirv);
		return result;
	}

	ShaderCompileResult compileFileCached(const std::string& path, ShaderStage stage) {
		std::string source;
		if (!readTextFile(path, source)) {
			ShaderCompileResult result;
			result.error = "Could not open shader file: " + path;
			RDA_LOG_ERROR(result.error);
			return result;
		}
		return compileSourceCached(source, stage, path);
	}
}
