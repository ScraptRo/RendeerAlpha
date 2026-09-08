#pragma once
#include <cstdint>
#include <GraphicalSrc/Shader.h>

namespace RDA {

	struct ShaderCompileResult {
		std::vector<uint32_t> spirv;
		std::string           error;
		bool ok() const { return error.empty() && !spirv.empty(); }
	};

	class IShaderCompiler {
	public:
		virtual ~IShaderCompiler() = default;

		virtual ShaderCompileResult compile(const std::string& source,
		                                    ShaderStage stage,
		                                    const std::string& name) = 0;

		virtual const char* languageId() const = 0;
	};

	class GlslShaderCompiler : public IShaderCompiler {
	public:
		ShaderCompileResult compile(const std::string& source,
		                            ShaderStage stage,
		                            const std::string& name) override;
		const char* languageId() const override { return "glsl"; }
	};

	IShaderCompiler& defaultShaderCompiler();
	void             setDefaultShaderCompiler(IShaderCompiler* compiler);

	ShaderCompileResult compileSourceCached(const std::string& source, ShaderStage stage, const std::string& name);

	ShaderCompileResult compileFileCached(const std::string& path, ShaderStage stage);
}
