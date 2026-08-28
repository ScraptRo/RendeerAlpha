#include <GraphicalSrc/SceneFrame.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Material.h>
#include <GraphicalObjects/Texture.h>
#include <Logger/Logger.h>
#include <algorithm>

namespace RDA {

	namespace {
		// Enough for a small scene without growing, small enough that a window which never
		// draws one costs nothing worth counting.
		constexpr uint32_t kInitialObjects = 64;
	}

	bool SceneFrame::create(DescriptorAllocator& pool, VkDescriptorSetLayout layout) {
		if (!mUniform.create(sizeof(SceneUniform),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryResidence::CpuToGpu)) {
			RDA_LOG_ERROR("Failed to create camera uniform buffer");
			return false;
		}

		mPool = &pool;
		mLayout = layout;
		mSet = pool.allocate(layout);
		if (mSet == VK_NULL_HANDLE) return false;

		DescriptorWriter()
			.writeBuffer(0, mUniform.handle(), sizeof(SceneUniform), 0,
			             VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			.update(mSet);

		// Seeded so binding 1 is never left unwritten, which would be a validation error
		// the first time the set is bound.
		return ensureObjectCapacity(kInitialObjects);
	}

	void SceneFrame::bindShadowMap(const Texture& depth) {
		if (mSet == VK_NULL_HANDLE) return;
		DescriptorWriter()
			.writeImage(2, depth.view(), depth.sampler(),
			            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.update(mSet);
	}

	bool SceneFrame::ensureObjectCapacity(uint32_t count) {
		if (count <= mObjectCapacity) return true;

		// Grow in powers of two so a scene that keeps adding objects stops reallocating.
		uint32_t capacity = mObjectCapacity ? mObjectCapacity : kInitialObjects;
		while (capacity < count) capacity *= 2;

		if (!mObjects.create(sizeof(ObjectData) * capacity,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryResidence::CpuToGpu)) {
			RDA_LOG_ERROR("Failed to create the per-object buffer");
			return false;
		}
		mObjectCapacity = capacity;

		// Point the set at the new buffer. MemoryBuffer::create() destroys before it
		// allocates, so this is only safe because the caller has already waited on this
		// frame's fence — and because nothing else can reach this buffer.
		DescriptorWriter()
			.writeBuffer(1, mObjects.handle(), sizeof(ObjectData) * capacity, 0,
			             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
			.update(mSet);
		return true;
	}

	void SceneFrame::updateCamera(const Scene& scene, const glm::mat4& lightViewProjection) {
		SceneUniform data{};
		data.view = scene.camera.view;
		data.proj = scene.camera.proj;
		data.cameraPosition = glm::vec4(scene.camera.position, 1.0f);

		// Intensity is folded into the colour here, so the shader only ever multiplies.
		data.sunDirection = glm::vec4(scene.sun.direction, 0.0f);
		data.sunColor = glm::vec4(scene.sun.color * scene.sun.intensity, 0.0f);
		data.lightViewProj = lightViewProjection;

		const Environment& env = scene.environment;
		data.skyColor = glm::vec4(env.skyColor * env.intensity, 0.0f);
		data.horizonColor = glm::vec4(env.horizonColor * env.intensity, 0.0f);
		data.groundColor = glm::vec4(env.groundColor * env.intensity, 0.0f);

		const uint32_t count = (std::min)(static_cast<uint32_t>(scene.lights().size()),
		                                  Scene::kMaxPointLights);
		data.counts.x = static_cast<float>(count);
		data.counts.y = scene.sun.castsShadows ? 1.0f : 0.0f;
		for (uint32_t i = 0; i < count; ++i) {
			const PointLight& light = scene.lights()[i];
			data.points[i].position = glm::vec4(light.position, light.range);
			data.points[i].color = glm::vec4(light.color * light.intensity, 0.0f);
		}

		mUniform.upload(&data, sizeof(data));
	}

	void SceneFrame::updateObjects(const Scene& scene, std::vector<ObjectData>& scratch) {
		const std::vector<DrawItem>& items = scene.items();
		if (items.empty()) return;
		if (!ensureObjectCapacity(static_cast<uint32_t>(items.size()))) return;

		static const MaterialParams kFallbackParams{}; // an item with no material still draws
		scratch.clear();
		scratch.reserve(items.size());
		for (const DrawItem& item : items) {
			const MaterialParams& params = item.material ? item.material->params : kFallbackParams;
			ObjectData data{};
			data.model = item.transform;
			// Inverse transpose of the upper 3x3: the transform for normals under scale.
			data.normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(item.transform))));
			data.baseColor = params.baseColor;
			data.surface = { params.metallic, params.roughness, params.ambientOcclusion, params.emissive };
			scratch.push_back(data);
		}
		mObjects.upload(scratch.data(), sizeof(ObjectData) * scratch.size());
	}

	void SceneFrame::destroy() {
		mUniform.destroy();
		mObjects.destroy();
		mObjectCapacity = 0;

		// The set goes back to the allocator rather than sitting in its pool until the
		// renderer shuts down. Safe because every caller has waited for the GPU before
		// releasing a window's frames — the same rule that makes destroying the buffers
		// above safe. The next window to open gets this one instead of a new pool.
		if (mPool && mSet != VK_NULL_HANDLE) mPool->recycle(mLayout, mSet);
		mSet = VK_NULL_HANDLE;
		mPool = nullptr;
		mLayout = VK_NULL_HANDLE;
	}
}
