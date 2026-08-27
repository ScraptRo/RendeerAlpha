#pragma once
#include <Core/Datatypes.h>
#include <GraphicalObjects/Scene.h>

// Set 0: what every technique reads, declared once for both languages.
//
// The C++ structs and the GLSL block below describe the same bytes. They used to sit
// twenty lines apart in the middle of a thousand-line renderer with a comment saying they
// must agree — which is true, and easy to forget while editing one of them. Here they are
// adjacent and nothing else is in the file, so a change to one is visibly a change to the
// other.
//
// Every member is vec4-sized so the two layouts cannot drift: std140 pads vec3 to 16
// bytes anyway, and writing that out explicitly is what keeps this honest.
namespace RDA {

	struct GpuPointLight {
		glm::vec4 position; // xyz = world position, w = range
		glm::vec4 color;    // rgb = colour * intensity, w unused
	};

	// Binding 0.
	struct SceneUniform {
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 lightViewProj; // world -> the sun's clip space, for the shadow lookup
		glm::vec4 cameraPosition; // xyz; needed for the specular view vector
		glm::vec4 sunDirection;   // xyz = direction the light travels
		glm::vec4 sunColor;       // rgb = colour * intensity
		glm::vec4 skyColor;       // environment gradient, all three scaled by intensity
		glm::vec4 horizonColor;
		glm::vec4 groundColor;
		glm::vec4 counts;         // x = active point lights, y = 1 when the sun casts shadows
		GpuPointLight points[Scene::kMaxPointLights];
	};

	// Binding 1, one entry per draw this frame. Matches the std430 block below member for
	// member.
	struct ObjectData {
		glm::mat4 model;
		// Inverse transpose of model's upper 3x3: the transform normals need under
		// non-uniform scale. Computed once per object on the CPU rather than per vertex
		// on the GPU.
		glm::mat4 normalMatrix;
		glm::vec4 baseColor;
		glm::vec4 surface; // metallic, roughness, ambient occlusion, emissive
	};

	// The GLSL half. Pasted into every stage that reads set 0, so the declarations can
	// never disagree between two shaders either. MAX_POINT_LIGHTS is substituted from
	// Scene::kMaxPointLights by whoever builds the preamble.
	//
	// The push constant is deliberately *not* here: each technique has its own (the
	// forward pass sends an object index, the shadow pass sends a matrix as well), and
	// putting one of them in the shared block would make it look like part of the contract.
	inline constexpr const char* kSceneSet0Glsl = R"GLSL(
		struct PointLight {
			vec4 position; // xyz, w = range
			vec4 color;    // rgb = colour * intensity
		};
		layout(set = 0, binding = 0) uniform SceneUBO {
			mat4 view;
			mat4 proj;
			mat4 lightViewProj;
			vec4 cameraPosition;
			vec4 sunDirection;
			vec4 sunColor;
			vec4 skyColor;
			vec4 horizonColor;
			vec4 groundColor;
			vec4 counts;
			PointLight points[MAX_POINT_LIGHTS];
		} scene;

		struct ObjectData {
			mat4 model;
			mat4 normalMatrix;
			vec4 baseColor;
			vec4 surface; // metallic, roughness, ao, emissive
		};
		// Unbounded array: one entry per draw this frame, indexed by the push constant.
		// std430 so its layout matches the C++ struct exactly.
		layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer {
			ObjectData objects[];
		} objectBuffer;
	)GLSL";
}
