#include <GraphicalObjects/Scene.h>
#include <Logger/Logger.h>
#include <glm/gtc/matrix_transform.hpp>

namespace RDA {

	void Camera::setPerspective(float fovYRadians, float aspect, float nearZ, float farZ) {
		proj = glm::perspective(fovYRadians, aspect, nearZ, farZ);
		// GLM builds an OpenGL-style projection: Y is up and the framebuffer origin
		// is bottom-left. Vulkan's Y points down, so flip it here once rather than
		// making every shader compensate.
		proj[1][1] *= -1.0f;
	}

	void Camera::lookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up) {
		view = glm::lookAt(eye, center, up);
		position = eye;
	}

	Frustum Frustum::fromViewProjection(const glm::mat4& m) {
		Frustum frustum;
		// glm is column-major: m[col][row]. Row i of the matrix is therefore
		// (m[0][i], m[1][i], m[2][i], m[3][i]). Each plane is row3 +/- row i.
		auto row = [&m](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
		const glm::vec4 rowW = row(3);

		frustum.planes[0] = rowW + row(0); // left
		frustum.planes[1] = rowW - row(0); // right
		frustum.planes[2] = rowW + row(1); // bottom
		frustum.planes[3] = rowW - row(1); // top
		// Depth maps to [0,1] (GLM_FORCE_DEPTH_ZERO_TO_ONE), so near is row2 alone
		// rather than rowW + row2 as it would be in an OpenGL-style [-1,1] range.
		frustum.planes[4] = row(2);        // near
		frustum.planes[5] = rowW - row(2); // far

		// Normalise so plane.w is a real distance and the sphere test can compare
		// against a radius directly.
		for (glm::vec4& plane : frustum.planes) {
			const float length = glm::length(glm::vec3(plane));
			if (length > 0.0f) plane /= length;
		}
		return frustum;
	}

	void Scene::add(const obj_ref<Mesh>& mesh, const Material& material, const glm::mat4& transform) {
		DrawItem item;
		item.mesh = mesh;
		item.material = &material;
		item.transform = transform;
		mItems.push_back(item);
	}

	Entity* Scene::createEntity(const obj_ref<Mesh>& mesh, const Material& material,
	                            const glm::mat4& transform, const std::string& name) {
		auto entity = std::make_unique<Entity>();
		entity->mesh = mesh;
		entity->material = &material;
		entity->transform = transform;
		entity->name = name;
		Entity* raw = entity.get();
		mEntities.push_back(std::move(entity));
		return raw;
	}

	void Scene::destroyEntity(Entity* entity) {
		for (auto it = mEntities.begin(); it != mEntities.end(); ++it) {
			if (it->get() == entity) { mEntities.erase(it); return; }
		}
	}

	Entity* Scene::findEntity(const std::string& name) const {
		for (const auto& entity : mEntities) {
			if (entity->name == name) return entity.get();
		}
		return nullptr;
	}

	void Scene::submitEntities() {
		for (const auto& entity : mEntities) {
			if (!entity->visible || !entity->material) continue;
			add(entity->mesh, *entity->material, entity->transform);
		}
	}

	void Scene::addLight(const PointLight& light) {
		// The uniform block is a fixed-size array, so this is a hard ceiling rather than
		// something that quietly grows. Dropping the extras is better than overrunning it.
		if (mLights.size() >= kMaxPointLights) {
			RDA_LOG_WARNING("Scene: more than " << kMaxPointLights << " point lights; ignoring the rest");
			return;
		}
		mLights.push_back(light);
	}
}
