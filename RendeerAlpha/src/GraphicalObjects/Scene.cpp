#include <GraphicalObjects/Scene.h>
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

	void Scene::add(const obj_ref<Mesh>& mesh, const Material& material, const glm::mat4& transform) {
		DrawItem item;
		item.mesh = mesh;
		item.material = &material;
		item.transform = transform;
		mItems.push_back(item);
	}
}
