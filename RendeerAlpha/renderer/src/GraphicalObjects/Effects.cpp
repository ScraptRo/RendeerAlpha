#include <GraphicalObjects/Effects.h>
#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Texture.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/Shader.h>
#include <GraphicalSrc/ShaderCompiler.h>
#include <Logger/Logger.h>

namespace RDA {

	namespace {
		// One invocation per pixel, in tiles of this. 8x8 is 64 -- at or under the minimum
		// workgroup size every Vulkan device is required to support, so nothing here has
		// to ask the device what it can do.
		constexpr uint32_t kTile = 8;

		// What an effect gets for free. Everything the fixed contract promises, so the
		// shader is the filter and nothing else.
		//
		// The two images are UNORM views of linear values, which needs saying: the source
		// is sampled from an sRGB texture, so the hardware has already decoded it, and the
		// destination is drawn by a GUI that expects linear. So an effect reads linear and
		// writes linear -- which is also the only space image arithmetic is correct in. A
		// blur averaged in sRGB is the wrong average.
		const char* kPrelude = R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D rdaSrc[4];
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D dst;
layout(push_constant) uniform RdaEffectParams { vec4 a; vec4 b; } rdaParams;

// `src` is the first input under its old name, so a filter written against one input goes
// on saying what it said.
#define src rdaSrc[0]

ivec2 coord() { return ivec2(gl_GlobalInvocationID.xy); }
ivec2 size()  { return imageSize(dst); }
vec2  uv()    { return (vec2(coord()) + 0.5) / vec2(size()); }
vec4  tap(int i, vec2 at) { return texture(rdaSrc[i], at); }
float param(int i) { return i < 4 ? rdaParams.a[i] : rdaParams.b[i - 4]; }
void  store(vec4 c) { imageStore(dst, coord(), c); }
#line 1
)GLSL";

	}

	// A compiled filter: the pipeline and the one descriptor set it binds. Both are built
	// once and reused, which is the point -- a filter that rebuilt its pipeline per frame
	// would cost more than the work it does.
	struct Effects::Effect {
		VkPipeline            pipeline = VK_NULL_HANDLE;
		VkPipelineLayout      layout = VK_NULL_HANDLE;
		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		VkDescriptorPool      pool = VK_NULL_HANDLE;
		VkDescriptorSet       set = VK_NULL_HANDLE;
		// What the set currently points at, so it is only rewritten when it has to be.
		uint64_t boundSource[kMaxEffectInputs] = { 0, 0, 0, 0 };
		uint64_t boundTarget = 0;

		~Effect() {
			VkDevice device = getDevice();
			if (!device) return;
			if (pipeline)  vkDestroyPipeline(device, pipeline, nullptr);
			if (layout)    vkDestroyPipelineLayout(device, layout, nullptr);
			if (pool)      vkDestroyDescriptorPool(device, pool, nullptr);
			if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		}
	};

	Effects::~Effects() {
		// See the note in Images: by static teardown the device is gone. clear() is what
		// releases these, at shutdown, while there is still something to release them with.
		mByName.clear();
	}

	bool Effects::define(const std::string& name, const std::string& glsl) {
		if (name.empty() || glsl.empty()) {
			RDA_LOG_WARNING("effect: nothing to define '" << name << "' from");
			return false;
		}
		VkDevice device = getDevice();
		if (!device) {
			RDA_LOG_WARNING("effect '" << name << "': there is no device yet. Define it "
			                             "from on_start or later, not before rda_init.");
			return false;
		}

		// Source starting with #version is a whole shader and is left alone; anything else
		// gets the contract prepended. `#line 1` at the end of the prelude means the
		// compiler's error messages count from the author's first line rather than from
		// the engine's.
		const bool whole = glsl.compare(0, 8, "#version") == 0;
		const std::string source = whole ? glsl : (kPrelude + glsl);

		// Through the compiler rather than Shader::fromSource, which answers only that it
		// failed. The compiler's own message names the line and the token, and for a
		// shader somebody just wrote that is the whole of what is worth saying.
		const ShaderCompileResult compiled =
			compileSourceCached(source, ShaderStage::Compute, "effect:" + name);
		if (!compiled.ok()) {
			RDA_LOG_ERROR("effect '" << name << "' will not compile: " << compiled.error);
			return false;
		}
		Shader shader = Shader::fromSpirv(compiled.spirv, ShaderStage::Compute);
		if (!shader.isValid()) {
			RDA_LOG_WARNING("effect '" << name << "': the device refused that shader");
			return false;
		}

		auto built = std::make_unique<Effect>();

		VkDescriptorSetLayoutBinding bindings[2]{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		// All four, always. A shader that names only `src` still declares an array of
		// four, and Vulkan wants every element of a statically used array bound -- so the
		// slots a caller did not fill are given the first source rather than left dangling.
		bindings[0].descriptorCount = kMaxEffectInputs;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo setInfo{};
		setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setInfo.bindingCount = 2;
		setInfo.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &built->setLayout) != VK_SUCCESS) {
			RDA_LOG_WARNING("effect '" << name << "': cannot describe its inputs");
			return false;
		}

		VkDescriptorPoolSize sizes[2]{};
		sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[0].descriptorCount = kMaxEffectInputs;
		sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		sizes[1].descriptorCount = 1;
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = sizes;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &built->pool) != VK_SUCCESS) {
			RDA_LOG_WARNING("effect '" << name << "': cannot allocate its inputs");
			return false;
		}

		VkDescriptorSetAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.descriptorPool = built->pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &built->setLayout;
		if (vkAllocateDescriptorSets(device, &alloc, &built->set) != VK_SUCCESS) {
			RDA_LOG_WARNING("effect '" << name << "': cannot allocate its inputs");
			return false;
		}

		VkPushConstantRange push{};
		push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		push.offset = 0;
		push.size = sizeof(EffectParams);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &built->setLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &push;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &built->layout) != VK_SUCCESS) {
			RDA_LOG_WARNING("effect '" << name << "': cannot build its pipeline layout");
			return false;
		}

		VkComputePipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeInfo.stage = shader.stageInfo();
		pipeInfo.layout = built->layout;
		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr,
		                             &built->pipeline) != VK_SUCCESS) {
			RDA_LOG_WARNING("effect '" << name << "': cannot build its pipeline");
			return false;
		}

		mByName[name] = std::move(built);
		return true;
	}

	bool Effects::apply(const std::string& name, const std::vector<std::string>& sources,
	                    const std::string& into, const EffectParams& params) {
		const auto at = mByName.find(name);
		if (at == mByName.end()) {
			RDA_LOG_WARNING("effect '" << name << "': nothing is defined under that name");
			return false;
		}
		if (sources.empty()) {
			RDA_LOG_WARNING("effect '" << name << "': no source to read");
			return false;
		}
		if (static_cast<int>(sources.size()) > kMaxEffectInputs) {
			RDA_LOG_WARNING("effect '" << name << "': " << sources.size()
			                << " sources, and a filter reads at most " << kMaxEffectInputs
			                << ". Chain two effects instead.");
			return false;
		}
		for (const std::string& one : sources) {
			if (one != into) continue;
			RDA_LOG_WARNING("effect '" << name << "': reading and writing '" << into
			                << "' at once. A compute shader reading the image it is writing "
			                   "sees whatever its neighbours have already written, which is "
			                   "a different picture every run. Write into another stream.");
			return false;
		}

		// Every source, gathered before anything is bound -- one missing is a refusal, not
		// a dispatch over three of four pictures.
		const Texture* from[kMaxEffectInputs] = { nullptr, nullptr, nullptr, nullptr };
		for (size_t i = 0; i < sources.size(); ++i) {
			from[i] = streams().find(sources[i]);
			if (!from[i] || !from[i]->isValid()) {
				RDA_LOG_WARNING("effect '" << name << "': stream '" << sources[i]
				                << "' has had no frame yet, so there is nothing to read");
				return false;
			}
		}
		// The slots the caller did not fill get the first picture. Vulkan wants every
		// element of a statically used array bound, and a shader that never mentions
		// tap(3, ...) does not care what is in slot three -- it only has to be something.
		for (int i = static_cast<int>(sources.size()); i < kMaxEffectInputs; ++i) {
			from[i] = from[0];
		}

		// The first source decides the size. The others are sampled in 0..1, so a mask or
		// a lookup of a different size is resized rather than refused.
		const uint32_t width = from[0]->extent().width;
		const uint32_t height = from[0]->extent().height;
		Texture* to = streams().target(into, width, height);
		if (!to) return false;   // Streams has said why

		Effect& effect = *at->second;
		VkDevice device = getDevice();

		// Only when it has to be: a filter run every frame over the same pictures rewrites
		// nothing. `revision` rather than the pointer, because a stream keeps its Texture
		// object across a size change and replaces the image inside it.
		bool stale = effect.boundTarget != to->revision();
		for (int i = 0; i < kMaxEffectInputs && !stale; ++i) {
			stale = effect.boundSource[i] != from[i]->revision();
		}
		if (stale) {
			VkDescriptorImageInfo in[kMaxEffectInputs]{};
			for (int i = 0; i < kMaxEffectInputs; ++i) {
				in[i].sampler = from[i]->sampler();
				in[i].imageView = from[i]->view();
				in[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}

			VkDescriptorImageInfo out{};
			out.imageView = to->view();
			out.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet writes[2]{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = effect.set;
			writes[0].dstBinding = 0;
			writes[0].descriptorCount = kMaxEffectInputs;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[0].pImageInfo = in;
			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = effect.set;
			writes[1].dstBinding = 1;
			writes[1].descriptorCount = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			writes[1].pImageInfo = &out;
			vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

			for (int i = 0; i < kMaxEffectInputs; ++i) {
				effect.boundSource[i] = from[i]->revision();
			}
			effect.boundTarget = to->revision();
		}

		immediateSubmit([&](VkCommandBuffer cmd) {
			// Into GENERAL to be written. From whatever it was -- UNDEFINED the first time,
			// SHADER_READ_ONLY_OPTIMAL every time after, because that is what the last run
			// left it in for the GUI to sample. Through the texture, so what it believes
			// its layout to be stays true.
			to->transitionTo(cmd, VK_IMAGE_LAYOUT_GENERAL);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.layout,
			                        0, 1, &effect.set, 0, nullptr);
			vkCmdPushConstants(cmd, effect.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
			                   sizeof(EffectParams), &params);
			vkCmdDispatch(cmd, (width + kTile - 1) / kTile, (height + kTile - 1) / kTile, 1);

			// And back to something the GUI can sample, with the writes made visible to it.
			to->transitionTo(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});
		streams().noteWritten(into);
		return true;
	}


	void Effects::forget(const std::string& name) {
		mByName.erase(name);
	}

	void Effects::clear() {
		mByName.clear();
	}

	Effects& effects() {
		static Effects one;
		return one;
	}
}
