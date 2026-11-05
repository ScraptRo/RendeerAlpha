#include <GraphicalSrc/Shader.h>
#include <GraphicalSrc/ShaderCompiler.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <Logger/Logger.h>

namespace RDA {

	VkShaderStageFlagBits shaderStageToVk(ShaderStage stage) {
		switch (stage) {
		case ShaderStage::Vertex:      return VK_SHADER_STAGE_VERTEX_BIT;
		case ShaderStage::Fragment:    return VK_SHADER_STAGE_FRAGMENT_BIT;
		case ShaderStage::Compute:     return VK_SHADER_STAGE_COMPUTE_BIT;
		case ShaderStage::Geometry:    return VK_SHADER_STAGE_GEOMETRY_BIT;
		case ShaderStage::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		case ShaderStage::TessEval:    return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		default:                       return VK_SHADER_STAGE_VERTEX_BIT;
		}
	}

	Shader::~Shader() {
		destroy();
	}

	Shader::Shader(Shader&& other) noexcept {
		mModule = other.mModule;
		mStage = other.mStage;
		other.mModule = VK_NULL_HANDLE;
	}

	Shader& Shader::operator=(Shader&& other) noexcept {
		if (this != &other) {
			destroy();
			mModule = other.mModule;
			mStage = other.mStage;
			other.mModule = VK_NULL_HANDLE;
		}
		return *this;
	}

	Shader Shader::fromSpirv(const std::vector<uint32_t>& spirv, ShaderStage stage) {
		Shader shader;
		shader.mStage = stage;
		if (spirv.empty()) {
			RDA_LOG_ERROR("Cannot create shader module from empty SPIR-V");
			return shader;
		}

		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = spirv.size() * sizeof(uint32_t);
		createInfo.pCode = spirv.data();

		if (vkCreateShaderModule(getDevice(), &createInfo, nullptr, &shader.mModule) != VK_SUCCESS) {
			RDA_LOG_ERROR("Failed to create shader module");
			shader.mModule = VK_NULL_HANDLE;
		}
		return shader;
	}

	Shader Shader::fromFile(const std::string& path, ShaderStage stage) {
		ShaderCompileResult compiled = compileFileCached(path, stage);
		if (!compiled.ok()) {
			return Shader{}; // error already logged by the compiler layer
		}
		return fromSpirv(compiled.spirv, stage);
	}

	Shader Shader::fromSource(const std::string& source, ShaderStage stage, const std::string& name) {
		ShaderCompileResult compiled = compileSourceCached(source, stage, name);
		if (!compiled.ok()) {
			return Shader{};
		}
		return fromSpirv(compiled.spirv, stage);
	}

	VkPipelineShaderStageCreateInfo Shader::stageInfo(const char* entryPoint) const {
		VkPipelineShaderStageCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage = shaderStageToVk(mStage);
		info.module = mModule;
		info.pName = entryPoint;
		return info;
	}

	void Shader::destroy() {
		if (mModule != VK_NULL_HANDLE) {
			vkDestroyShaderModule(getDevice(), mModule, nullptr);
			mModule = VK_NULL_HANDLE;
		}
	}
}
