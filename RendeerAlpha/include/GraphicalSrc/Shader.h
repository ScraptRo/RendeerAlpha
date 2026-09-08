#pragma once
#include <cstdint>
#include <Core/Datatypes.h>

namespace RDA {

	enum class ShaderStage {
		Vertex,
		Fragment,
		Compute,
		Geometry,
		TessControl,
		TessEval,
	};

	VkShaderStageFlagBits shaderStageToVk(ShaderStage stage);

	class Shader {
	public:
		Shader() = default;
		~Shader();

		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;
		Shader(Shader&& other) noexcept;
		Shader& operator=(Shader&& other) noexcept;

		static Shader fromSpirv(const std::vector<uint32_t>& spirv, ShaderStage stage);
		static Shader fromFile(const std::string& path, ShaderStage stage);
		static Shader fromSource(const std::string& source, ShaderStage stage, const std::string& name);

		// Ready-to-use entry for VkGraphicsPipelineCreateInfo::pStages.
		VkPipelineShaderStageCreateInfo stageInfo(const char* entryPoint = "main") const;

		VkShaderModule handle() const { return mModule; }
		ShaderStage    stage()  const { return mStage; }
		bool           isValid() const { return mModule != VK_NULL_HANDLE; }

		void destroy();

	private:
		VkShaderModule mModule = VK_NULL_HANDLE;
		ShaderStage    mStage = ShaderStage::Vertex;
	};
}
