#pragma once
#include <Core/Datatypes.h>
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Material.h>

namespace RDA {

	// A simple free camera. Note setPerspective already flips Y for Vulkan's
	// clip space, so the matrices here are ready to use as-is.
	struct Camera {
		glm::mat4 view{ 1.0f };
		glm::mat4 proj{ 1.0f };
		glm::vec3 position{ 0.0f };

		void setPerspective(float fovYRadians, float aspect, float nearZ, float farZ);
		void lookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
	};

	// One thing to draw this frame. Holding an obj_ref means the scene itself keeps
	// the mesh alive for as long as it is queued to draw.
	struct DrawItem {
		obj_ref<Mesh>   mesh;
		const Material* material = nullptr;
		glm::mat4       transform{ 1.0f };
	};

	// What the renderer draws: a camera, a light, and a list of draw items.
	class Scene {
	public:
		Camera    camera;
		glm::vec3 lightDirection{ -0.5f, -1.0f, -0.3f }; // direction the light travels

		void add(const obj_ref<Mesh>& mesh, const Material& material, const glm::mat4& transform = glm::mat4(1.0f));
		void clear() { mItems.clear(); }

		const std::vector<DrawItem>& items() const { return mItems; }

	private:
		std::vector<DrawItem> mItems;
	};

	// The engine-owned scene the renderer draws each frame.
	Scene& getScene();
}
