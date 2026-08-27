#include <RendeerAlpha.h>
#include <cmath>
#include <vector>

// Procedural primitive meshes. These used to live in the sample application, which meant
// every application that wanted a cube to look at had to write out 24 vertices first.
// They are engine facilities: geometry is the engine's business, and a renderer is much
// easier to work on when a test scene is one call away.
//
// All of them are built for the forward path's Vertex layout (position, normal, uv) with
// flat per-face normals where the surface is flat, so lighting has something sane to work
// with. Every primitive is centred on the origin.
namespace RDA {

	namespace {
		// Builds one quad face from four corners wound counter-clockwise, with a shared
		// normal. Split per face rather than sharing corner vertices, because a cube's
		// corners need a different normal per face.
		void addQuadFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
		                 const glm::vec3& normal, const glm::vec3& a, const glm::vec3& b,
		                 const glm::vec3& c, const glm::vec3& d) {
			uint32_t base = static_cast<uint32_t>(vertices.size());
			vertices.push_back({ a, normal, { 0.0f, 0.0f } });
			vertices.push_back({ b, normal, { 1.0f, 0.0f } });
			vertices.push_back({ c, normal, { 1.0f, 1.0f } });
			vertices.push_back({ d, normal, { 0.0f, 1.0f } });
			indices.insert(indices.end(), { base, base + 1, base + 2, base + 2, base + 3, base });
		}

		obj_ref<Mesh> upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
			obj_ref<Mesh> mesh = createMesh();
			if (mesh.IsValid()) mesh->upload(vertices, indices);
			return mesh;
		}
	}

	obj_ref<Mesh> createCubeMesh(float size) {
		const float h = size * 0.5f;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(24);
		indices.reserve(36);

		addQuadFace(vertices, indices, {  0,  0,  1 }, { -h, -h,  h }, {  h, -h,  h }, {  h,  h,  h }, { -h,  h,  h });
		addQuadFace(vertices, indices, {  0,  0, -1 }, {  h, -h, -h }, { -h, -h, -h }, { -h,  h, -h }, {  h,  h, -h });
		addQuadFace(vertices, indices, {  1,  0,  0 }, {  h, -h,  h }, {  h, -h, -h }, {  h,  h, -h }, {  h,  h,  h });
		addQuadFace(vertices, indices, { -1,  0,  0 }, { -h, -h, -h }, { -h, -h,  h }, { -h,  h,  h }, { -h,  h, -h });
		addQuadFace(vertices, indices, {  0,  1,  0 }, { -h,  h,  h }, {  h,  h,  h }, {  h,  h, -h }, { -h,  h, -h });
		addQuadFace(vertices, indices, {  0, -1,  0 }, { -h, -h, -h }, {  h, -h, -h }, {  h, -h,  h }, { -h, -h,  h });

		return upload(vertices, indices);
	}

	obj_ref<Mesh> createPlaneMesh(float size, uint32_t subdivisions, float uvTiles) {
		const uint32_t steps = subdivisions + 1;         // quads per side
		const uint32_t line = steps + 1;                 // vertices per side
		const float half = size * 0.5f;
		const float step = size / static_cast<float>(steps);

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(line * line);
		indices.reserve(steps * steps * 6);

		// Lies in XZ facing +Y, which is what a ground plane wants.
		for (uint32_t z = 0; z < line; ++z) {
			for (uint32_t x = 0; x < line; ++x) {
				float px = -half + step * static_cast<float>(x);
				float pz = -half + step * static_cast<float>(z);
				// UVs repeat `uvTiles` times across the plane, so a texture keeps its
				// real-world scale instead of being stretched over the whole surface.
				float u = (static_cast<float>(x) / static_cast<float>(steps)) * uvTiles;
				float v = (static_cast<float>(z) / static_cast<float>(steps)) * uvTiles;
				vertices.push_back({ { px, 0.0f, pz }, { 0.0f, 1.0f, 0.0f }, { u, v } });
			}
		}
		for (uint32_t z = 0; z < steps; ++z) {
			for (uint32_t x = 0; x < steps; ++x) {
				uint32_t i0 = z * line + x;
				uint32_t i1 = i0 + 1;
				uint32_t i2 = i0 + line;
				uint32_t i3 = i2 + 1;
				indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
			}
		}
		return upload(vertices, indices);
	}

	obj_ref<Mesh> createSphereMesh(float radius, uint32_t segments, uint32_t rings) {
		segments = (segments < 3) ? 3 : segments;
		rings = (rings < 2) ? 2 : rings;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve((rings + 1) * (segments + 1));
		indices.reserve(rings * segments * 6);

		constexpr float kPi = 3.14159265358979323846f;
		// A UV sphere: rings from pole to pole, segments around. The seam column is
		// duplicated so the u coordinate can run 0..1 without wrapping.
		for (uint32_t r = 0; r <= rings; ++r) {
			float v = static_cast<float>(r) / static_cast<float>(rings);
			float phi = v * kPi;                     // 0 at +Y pole, pi at -Y
			float y = std::cos(phi);
			float ringRadius = std::sin(phi);
			for (uint32_t s = 0; s <= segments; ++s) {
				float u = static_cast<float>(s) / static_cast<float>(segments);
				float theta = u * 2.0f * kPi;
				glm::vec3 normal{ std::cos(theta) * ringRadius, y, std::sin(theta) * ringRadius };
				vertices.push_back({ normal * radius, normal, { u, v } });
			}
		}
		const uint32_t line = segments + 1;
		for (uint32_t r = 0; r < rings; ++r) {
			for (uint32_t s = 0; s < segments; ++s) {
				uint32_t i0 = r * line + s;
				uint32_t i1 = i0 + 1;      // next segment, same ring
				uint32_t i2 = i0 + line;   // next ring, same segment
				uint32_t i3 = i2 + 1;
				// Wound counter-clockwise seen from outside, like every other primitive
				// here. Note this is the opposite index order from the plane above even
				// though the grids look alike: on the plane the second axis runs along
				// +Z, while here it runs from the +Y pole downwards, which flips the
				// handedness of the parameterisation.
				indices.insert(indices.end(), { i0, i1, i2, i1, i3, i2 });
			}
		}
		return upload(vertices, indices);
	}
}
