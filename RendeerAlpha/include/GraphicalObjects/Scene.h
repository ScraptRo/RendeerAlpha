#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Material.h>
#include <memory>
#include <string>

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

	// The six planes bounding what the camera can see, in world space. Extracted from a
	// combined view-projection matrix (Gribb-Hartmann): each plane is a row of the
	// matrix added to or subtracted from the w row, which works for any projection —
	// perspective, orthographic, infinite-far — without special cases.
	//
	// Planes point inward, so a point is inside when it is on the positive side of all
	// six. Testing a sphere is then six dot products.
	struct Frustum {
		glm::vec4 planes[6]{}; // xyz = normal, w = distance; normalised

		static Frustum fromViewProjection(const glm::mat4& viewProjection);

		// True when any part of the sphere is inside. Conservative at the corners: a
		// sphere just outside two planes can still pass, which costs a draw, never a
		// missing object.
		bool intersectsSphere(const glm::vec3& center, float radius) const {
			for (const glm::vec4& plane : planes) {
				if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) return false;
			}
			return true;
		}
	};

	// One thing to draw this frame. Holding an obj_ref means the scene itself keeps
	// the mesh alive for as long as it is queued to draw.
	struct DrawItem {
		obj_ref<Mesh>   mesh;
		const Material* material = nullptr;
		glm::mat4       transform{ 1.0f };
	};

	// The sun: one infinitely distant light, the same everywhere in the scene.
	struct DirectionalLight {
		glm::vec3 direction{ -0.5f, -1.0f, -0.3f }; // the direction the light travels
		glm::vec3 color{ 1.0f, 0.98f, 0.95f };     // linear RGB
		float     intensity = 3.0f;

		// Shadowing. The sun is the only light that casts, and it casts over a fixed
		// square of world centred on `shadowCenter`, `shadowExtent` across. Anything
		// outside that square is lit normally — enlarging the extent covers more scene
		// at the cost of resolution, which is the trade cascaded shadow maps exist to
		// avoid.
		bool      castsShadows = true;
		glm::vec3 shadowCenter{ 0.0f };
		float     shadowExtent = 10.0f;
		float     shadowDepth = 40.0f; // how far back the light is placed, and its far plane
	};

	// A light at a place, falling off with distance. `range` is where its contribution
	// reaches zero, which is what keeps a light from having to be evaluated everywhere.
	struct PointLight {
		glm::vec3 position{ 0.0f };
		glm::vec3 color{ 1.0f };
		float     intensity = 5.0f;
		float     range = 10.0f;
	};

	// The surrounding environment, as a vertical gradient: what a surface sees when it
	// looks in any direction that is not a light. This is what lets metals reflect
	// something instead of coming out black, and what gives shadowed sides a colour
	// rather than a flat fill.
	//
	// It is analytic rather than a captured cubemap: the gradient is evaluated directly
	// in the shader, so there is no environment map to load and no convolution pass to
	// precompute. A real captured environment is a strictly better version of exactly
	// this interface — same three lookups, sourced from a texture instead of a formula.
	struct Environment {
		glm::vec3 skyColor{ 0.32f, 0.46f, 0.78f };     // straight up
		glm::vec3 horizonColor{ 0.58f, 0.60f, 0.64f }; // level with the horizon
		glm::vec3 groundColor{ 0.16f, 0.14f, 0.12f };  // straight down
		float     intensity = 1.0f;
	};

	// A thing that stays in the scene between frames.
	//
	// DrawItems are the opposite: the application clears and refills them every frame, so
	// nothing can hold on to one. An Entity has a stable address, which is what lets a
	// script (or an editor, or a gizmo) keep a reference and move it later.
	struct Entity {
		obj_ref<Mesh>   mesh;
		const Material* material = nullptr;
		glm::mat4       transform{ 1.0f };
		bool            visible = true;
		std::string     name;
	};

	// What the renderer draws: a camera, the lighting environment, and a list of draw
	// items. Cleared and refilled every frame by the application.
	class Scene {
	public:
		// The shader carries this many point lights; adding more is ignored with a warning.
		static constexpr uint32_t kMaxPointLights = 8;

		Camera camera;

		DirectionalLight sun;
		Environment      environment;

		void add(const obj_ref<Mesh>& mesh, const Material& material, const glm::mat4& transform = glm::mat4(1.0f));
		void addLight(const PointLight& light);
		// Clears only what is rebuilt each frame. Entities, the sun and the environment
		// survive, so anything that changed them stays changed.
		void clear() { mItems.clear(); mLights.clear(); }

		// ---- retained entities ----
		// Created once and kept. The returned pointer stays valid until destroyEntity or
		// clearEntities, because the entities are held indirectly.
		Entity* createEntity(const obj_ref<Mesh>& mesh, const Material& material,
		                     const glm::mat4& transform = glm::mat4(1.0f),
		                     const std::string& name = {});
		void    destroyEntity(Entity* entity);
		void    clearEntities() { mEntities.clear(); }
		Entity* findEntity(const std::string& name) const;
		size_t  entityCount() const { return mEntities.size(); }
		Entity* entityAt(size_t index) const {
			return index < mEntities.size() ? mEntities[index].get() : nullptr;
		}

		// Queues every visible entity for this frame. Called after clear(), so entities
		// and per-frame items end up in one list and the renderer needs no special case.
		void submitEntities();

		const std::vector<DrawItem>&   items()  const { return mItems; }
		const std::vector<PointLight>& lights() const { return mLights; }

	private:
		std::vector<DrawItem>   mItems;
		std::vector<PointLight> mLights;
		std::vector<std::unique_ptr<Entity>> mEntities;
	};

	// The engine-owned scene the renderer draws each frame.
	Scene& getScene();
}
