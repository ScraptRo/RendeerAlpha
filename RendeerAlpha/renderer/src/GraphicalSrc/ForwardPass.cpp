#include <GraphicalSrc/ForwardPass.h>
#include <GraphicalSrc/SceneBindings.h>
#include <GraphicalSrc/Shader.h>
#include <GraphicalObjects/Scene.h>
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Material.h>
#include <Logger/Logger.h>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace RDA {

	namespace {
		// All that is left in the push constant: which object this draw is. 4 bytes, so
		// the whole 128-byte budget is free again for whatever comes next.
		struct ForwardPush {
			uint32_t objectIndex;
		};

		// This technique's push constant, declared for the shaders. Kept beside the pass
		// that sends it rather than in the shared set-0 block, because each technique has
		// its own.
		const char* kPushGlsl = R"GLSL(
		layout(push_constant) uniform Push {
			uint objectIndex;
		} push;
	)GLSL";

		const char* kVertexBody = R"GLSL(
		layout(location = 0) in vec3 inPosition;
		layout(location = 1) in vec3 inNormal;
		layout(location = 2) in vec2 inUV;

		layout(location = 0) out vec3 vWorldPos;
		layout(location = 1) out vec3 vWorldNormal;
		layout(location = 2) out vec2 vUV;

		void main() {
			ObjectData object = objectBuffer.objects[push.objectIndex];
			vec4 world = object.model * vec4(inPosition, 1.0);
			vWorldPos = world.xyz;
			// A real normal matrix, so non-uniform scale shades correctly.
			vWorldNormal = mat3(object.normalMatrix) * inNormal;
			vUV = inUV;
			gl_Position = scene.proj * scene.view * world;
		}
	)GLSL";

	// Cook-Torrance with GGX distribution, Smith geometry and Schlick Fresnel: the
	// standard metallic-roughness model. Output stays LINEAR - the swapchain is an sRGB
	// format, so the hardware does the encode and gamma-correcting here would double it.
		const char* kFragmentBody = R"GLSL(
		layout(location = 0) in vec3 vWorldPos;
		layout(location = 1) in vec3 vWorldNormal;
		layout(location = 2) in vec2 vUV;

		// Set 1: this material's maps. Always bound to something real — an unset map is
		// an engine default, not a missing descriptor.
		layout(set = 0, binding = 2) uniform sampler2D shadowMap;

		layout(set = 1, binding = 0) uniform sampler2D albedoMap;
		layout(set = 1, binding = 1) uniform sampler2D normalMap;
		layout(set = 1, binding = 2) uniform sampler2D metalRoughMap;

		layout(location = 0) out vec4 outColor;

		const float PI = 3.14159265359;

		// Tangent frame from screen-space derivatives, so a mesh needs no tangent
		// attribute. Costs a few instructions per pixel and works on any geometry with
		// UVs; a precomputed per-vertex tangent would be cheaper and smoother, and is the
		// upgrade path once the vertex format grows.
		vec3 perturbNormal(vec3 N, vec3 worldPos, vec2 uv) {
			vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
			// A flat map decodes to (0,0,1); skip the frame entirely in that case.
			if (dot(tangentNormal.xy, tangentNormal.xy) < 1e-8) return N;

			vec3 dp1 = dFdx(worldPos);
			vec3 dp2 = dFdy(worldPos);
			vec2 duv1 = dFdx(uv);
			vec2 duv2 = dFdy(uv);

			vec3 dp2perp = cross(dp2, N);
			vec3 dp1perp = cross(N, dp1);
			vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
			vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

			// Degenerate UVs (a face with no texture gradient) leave T and B at zero.
			float maxLength = max(dot(T, T), dot(B, B));
			if (maxLength < 1e-12) return N;

			float invmax = inversesqrt(maxLength);
			return normalize(mat3(T * invmax, B * invmax, N) * tangentNormal);
		}

		float distributionGGX(float NdotH, float roughness) {
			float a = roughness * roughness;
			float a2 = a * a;
			float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
			return a2 / max(PI * d * d, 1e-7);
		}

		float geometrySchlickGGX(float NdotX, float roughness) {
			// Direct-lighting remapping of the roughness into Smith's k.
			float r = roughness + 1.0;
			float k = (r * r) / 8.0;
			return NdotX / (NdotX * (1.0 - k) + k);
		}

		float geometrySmith(float NdotV, float NdotL, float roughness) {
			return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
		}

		vec3 fresnelSchlick(float cosTheta, vec3 F0) {
			return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
		}

		// How much of the sun reaches this point: 1 fully lit, 0 fully shadowed.
		float sunVisibility(vec3 worldPos, vec3 N, vec3 L) {
			if (scene.counts.y < 0.5) return 1.0;

			vec4 lightClip = scene.lightViewProj * vec4(worldPos, 1.0);
			vec3 proj = lightClip.xyz / lightClip.w;      // orthographic, so w is 1
			vec2 uv = proj.xy * 0.5 + 0.5;                // clip -> texture coordinates

			// Past the far plane there is nothing recorded to compare against.
			if (proj.z > 1.0) return 1.0;

			// Slope-scaled: a surface facing the light edge-on spans far more depth per
			// texel, so it needs a larger offset before a comparison is meaningful.
			float cosTheta = clamp(dot(N, L), 0.0, 1.0);
			float bias = max(0.0015 * (1.0 - cosTheta), 0.0004);

			// 3x3 percentage-closer filter: nine comparisons averaged, which turns the
			// hard per-texel edge into something the resolution can carry.
			vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
			float lit = 0.0;
			for (int x = -1; x <= 1; ++x) {
				for (int y = -1; y <= 1; ++y) {
					float closest = texture(shadowMap, uv + vec2(x, y) * texel).r;
					lit += (proj.z - bias > closest) ? 0.0 : 1.0;
				}
			}
			return lit / 9.0;
		}

		// ---- image-based lighting ----
		// The environment in a direction: a ground-to-horizon-to-sky gradient. The sqrt
		// tightens the bands toward the horizon, which is where a real sky changes fastest.
		vec3 environmentColor(vec3 dir) {
			float t = clamp(abs(dir.y), 0.0, 1.0);
			float band = sqrt(t);
			return (dir.y >= 0.0) ? mix(scene.horizonColor.rgb, scene.skyColor.rgb, band)
			                      : mix(scene.horizonColor.rgb, scene.groundColor.rgb, band);
		}

		// Cosine-weighted irradiance over the hemisphere around N. For a vertical
		// gradient this is close to how much sky versus ground that hemisphere can see,
		// which is a single mix rather than an integral.
		vec3 environmentIrradiance(vec3 N) {
			float upness = 0.5 + 0.5 * N.y;
			vec3 lower = mix(scene.groundColor.rgb, scene.horizonColor.rgb, 0.5);
			vec3 upper = mix(scene.horizonColor.rgb, scene.skyColor.rgb, 0.5);
			return mix(lower, upper, upness);
		}

		// What a prefiltered environment map would return: the sharp reflection for a
		// polished surface, fading to the overall irradiance as roughness widens the lobe.
		vec3 environmentSpecular(vec3 R, float roughness) {
			return mix(environmentColor(R), environmentIrradiance(R), roughness);
		}

		// Karis' analytic fit to the split-sum environment BRDF, standing in for the
		// usual precomputed 2D lookup table.
		vec3 envBRDFApprox(vec3 F0, float roughness, float NdotV) {
			const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
			const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
			vec4 r = roughness * c0 + c1;
			float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
			vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
			return F0 * AB.x + AB.y;
		}

		// Fresnel that accounts for roughness, so a rough surface does not get the same
		// hard grazing-angle rim a polished one does.
		vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
			vec3 maxReflect = max(vec3(1.0 - roughness), F0);
			return F0 + (maxReflect - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
		}

		// One light's contribution. `radiance` already carries colour * intensity *
		// attenuation, so the sun and a point light share this whole path.
		vec3 shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0,
		           float metallic, float roughness) {
			vec3 H = normalize(V + L);
			float NdotL = max(dot(N, L), 0.0);
			if (NdotL <= 0.0) return vec3(0.0);
			float NdotV = max(dot(N, V), 1e-4);
			float NdotH = max(dot(N, H), 0.0);

			float D = distributionGGX(NdotH, roughness);
			float G = geometrySmith(NdotV, NdotL, roughness);
			vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

			vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
			// What is not reflected is refracted; metals keep no diffuse at all.
			vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
			return (kD * albedo / PI + specular) * radiance * NdotL;
		}

		void main() {
			ObjectData object = objectBuffer.objects[push.objectIndex];

			// Maps modulate the scalars rather than replacing them, so a material can be
			// tinted or roughened without authoring a new texture.
			vec4 albedoSample = texture(albedoMap, vUV);
			vec3 albedo = object.baseColor.rgb * albedoSample.rgb;
			vec3 metalRough = texture(metalRoughMap, vUV).rgb; // glTF: G rough, B metal
			float metallic  = clamp(object.surface.x * metalRough.b, 0.0, 1.0);
			float roughness = clamp(object.surface.y * metalRough.g, 0.04, 1.0); // 0 is a singular mirror
			float ao        = object.surface.z;
			float emissive  = object.surface.w;

			vec3 N = perturbNormal(normalize(vWorldNormal), vWorldPos, vUV);
			vec3 V = normalize(scene.cameraPosition.xyz - vWorldPos);
			// Dielectrics reflect a flat 4%; metals tint their reflection with the albedo.
			vec3 F0 = mix(vec3(0.04), albedo, metallic);

			vec3 color = vec3(0.0);
			// Only the sun is shadowed: the point lights cast nothing, and the
			// environment term is ambient by definition.
			vec3 sunL = normalize(-scene.sunDirection.xyz);
			color += shade(N, V, sunL, scene.sunColor.rgb, albedo, F0, metallic, roughness)
			         * sunVisibility(vWorldPos, N, sunL);

			int count = int(scene.counts.x);
			for (int i = 0; i < count; ++i) {
				vec3 toLight = scene.points[i].position.xyz - vWorldPos;
				float distance = length(toLight);
				float range = max(scene.points[i].position.w, 1e-4);
				if (distance > range) continue;
				// Inverse-square falloff, windowed so the light actually reaches zero at
				// its range instead of being clipped mid-slope.
				float d2 = distance * distance;
				float window = clamp(1.0 - (d2 * d2) / (range * range * range * range), 0.0, 1.0);
				vec3 radiance = scene.points[i].color.rgb * (window * window) / max(d2, 1e-4);
				color += shade(N, V, toLight / max(distance, 1e-4), radiance,
				               albedo, F0, metallic, roughness);
			}

			// Image-based lighting: what the environment contributes from every direction
			// that is not one of the lights above. This is what stops a metal from being
			// black — it has an environment to reflect.
			float NdotV = max(dot(N, V), 1e-4);
			vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
			vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

			vec3 diffuseIBL = environmentIrradiance(N) * albedo * kD;
			vec3 R = reflect(-V, N);
			vec3 specularIBL = environmentSpecular(R, roughness) * envBRDFApprox(F0, roughness, NdotV);

			color += (diffuseIBL + specularIBL) * ao;
			color += albedo * emissive;

			// Reinhard: keeps intensities above 1 from clipping to white.
			color = color / (color + vec3(1.0));
			outColor = vec4(color, object.baseColor.a * albedoSample.a);
		}
	)GLSL";
	}

	bool ForwardPass::init(VkRenderPass target, VkDescriptorSetLayout sceneSetLayout,
	                       VkDescriptorSetLayout materialSetLayout) {
		// Both stages get the same preamble, so the uniform block is declared once and the
		// light-array size comes from the one constant that defines it.
		const std::string preamble = std::string("#version 450\n#define MAX_POINT_LIGHTS ") +
			std::to_string(Scene::kMaxPointLights) + "\n" + kSceneSet0Glsl + kPushGlsl;

		Shader vertex = Shader::fromSource((preamble + kVertexBody).c_str(),
			ShaderStage::Vertex, "forward.vert");
		Shader fragment = Shader::fromSource((preamble + kFragmentBody).c_str(),
			ShaderStage::Fragment, "forward.frag");
		if (!vertex.isValid() || !fragment.isValid()) {
			RDA_LOG_ERROR("Failed to build forward shaders");
			return false;
		}

		// Both stages read the object index to look up their half of ObjectData.
		VkPushConstantRange modelPush{};
		modelPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		modelPush.offset = 0;
		modelPush.size = sizeof(ForwardPush);

		// Position, normal and uv: the shader reads all three now that surfaces are
		// textured.
		VkVertexInputBindingDescription binding = Vertex::getBindingDescription();
		std::array<VkVertexInputAttributeDescription, 3> allAttributes = Vertex::getAttributeDescriptions();
		std::vector<VkVertexInputAttributeDescription> attributes(allAttributes.begin(), allAttributes.end());

		mPipeline = PipelineBuilder()
			.addShader(vertex)
			.addShader(fragment)
			.setVertexInput(binding, attributes)
			.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			// Back faces never contribute to a closed mesh, so half the triangles are
			// discarded before rasterisation.
			//
			// Counter-clockwise is the front, matching how the engine's primitives are
			// wound in world space. The Y flip in Camera::setPerspective does not change
			// this: it exists to cancel out Vulkan's framebuffer Y pointing down, so the
			// two inversions undo each other and the winding that reaches the rasteriser
			// is the world-space one. (Getting this backwards culls single-sided geometry
			// like a ground plane while closed meshes still look fine, because those just
			// fall back to showing their far faces.)
			.setCull(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
			.setDepth(true, true, VK_COMPARE_OP_LESS)
			.addDescriptorSetLayout(sceneSetLayout)     // set 0: scene + objects
			.addDescriptorSetLayout(materialSetLayout)  // set 1: this material's maps
			.addPushConstantRange(modelPush)
			.setTarget(target, 0)
			.build();

		if (!mPipeline.isValid()) {
			RDA_LOG_ERROR("Failed to build the forward pipeline");
			return false;
		}
		return true;
	}

	void ForwardPass::record(VkCommandBuffer cmd, VkDescriptorSet sceneSet, const Scene& scene) {
		mPipeline.bind(cmd);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.layout(),
			0, 1, &sceneSet, 0, nullptr);

		// One frustum for the whole frame, from the camera that is about to be used.
		const Frustum frustum = Frustum::fromViewProjection(scene.camera.proj * scene.camera.view);
		mCullStats = CullStats{};
		mCullStats.submitted = static_cast<uint32_t>(scene.items().size());

		// The index must follow the scene's item order, because the object buffer was
		// staged in exactly that order — including items skipped below, so a skipped mesh
		// does not shift everything after it.
		uint32_t index = 0;
		const Material* boundMaterial = nullptr;
		for (const DrawItem& item : scene.items()) {
			const uint32_t objectIndex = index++;
			if (!item.mesh.IsValid() || !item.mesh->isValid()) continue;

			if (mFrustumCulling) {
				// Move the mesh's local bounding sphere into world space. The radius
				// scales by the largest axis scale in the transform, so a non-uniformly
				// scaled object gets a sphere that still contains it.
				const glm::mat4& m = item.transform;
				const glm::vec3 center = glm::vec3(m * glm::vec4(item.mesh->boundsCenter(), 1.0f));
				const float scale = std::sqrt((glm::max)((glm::max)(
					glm::dot(glm::vec3(m[0]), glm::vec3(m[0])),
					glm::dot(glm::vec3(m[1]), glm::vec3(m[1]))),
					glm::dot(glm::vec3(m[2]), glm::vec3(m[2]))));
				if (!frustum.intersectsSphere(center, item.mesh->boundsRadius() * scale)) {
					++mCullStats.culled;
					continue;
				}
			}
			++mCullStats.drawn;

			// Set 1 only changes when the material does; consecutive items sharing one
			// material bind it once. Sorting by material is what makes that pay off.
			if (item.material && item.material != boundMaterial) {
				item.material->bindSets(cmd, mPipeline.layout());
				boundMaterial = item.material;
			}

			ForwardPush push{ objectIndex };
			vkCmdPushConstants(cmd, mPipeline.layout(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(push), &push);
			item.mesh->recordDraw(cmd);
		}
	}

	void ForwardPass::destroy() {
		mPipeline.destroy();
	}
}
