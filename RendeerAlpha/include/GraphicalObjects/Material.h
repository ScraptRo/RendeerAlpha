#pragma once
#include <cstdint>
#include <Core/Datatypes.h>
#include <GraphicalObjects/Texture.h>
#include <GraphicalSrc/GraphicsPipeline.h>

namespace RDA {

	// The maps a surface can carry. Any left invalid falls back to a neutral engine
	// default (white / flat / white), so an untextured material takes the same shader
	// path as a textured one — no branching, no separate pipeline.
	struct MaterialTextures {
		// sRGB. Multiplied with MaterialParams::baseColor.
		obj_ref<Texture> albedo;
		// Linear tangent-space normals. The tangent frame is derived in the shader from
		// screen-space derivatives, so meshes need no tangent attribute.
		obj_ref<Texture> normal;
		// Linear, glTF convention: G = roughness, B = metallic. Multiplied with the
		// matching MaterialParams scalars.
		obj_ref<Texture> metallicRoughness;
	};

	// What a surface looks like, in the metallic-roughness convention the rest of the
	// industry uses: one base colour, plus how metallic and how rough the surface is.
	// Dielectrics (metallic 0) reflect a fixed 4% specular and keep their base colour as
	// diffuse albedo; metals (metallic 1) have no diffuse and tint their reflection with
	// the base colour. Everything in between interpolates.
	//
	// Colours are LINEAR, not sRGB. The swapchain is an sRGB format, so the hardware
	// encodes on write and the shader works in linear throughout — a colour picked from
	// a paint program has to be converted before it lands here.
	struct MaterialParams {
		glm::vec4 baseColor{ 0.8f, 0.8f, 0.82f, 1.0f };
		float metallic = 0.0f;    // 0 = dielectric, 1 = metal
		float roughness = 0.5f;   // 0 = mirror, 1 = fully diffuse
		float ambientOcclusion = 1.0f;
		float emissive = 0.0f;    // scales baseColor added on top, unlit
	};

	class Material {
	public:
		Material() = default;

		// Per-instance surface parameters. A Material is a value: copy the one the
		// engine hands out and change these to get a different-looking surface, without
		// touching the pipeline it draws with.
		MaterialParams params;

		// The maps this material was built with. Held so the material keeps its textures
		// alive: the pools are reference counted, and a texture the application has
		// stopped holding would otherwise be freed out from under the descriptor set.
		MaterialTextures textures;

		void setPipeline(const GraphicsPipeline* pipeline) { mPipeline = pipeline; }
		void setSets(uint32_t firstSet, const std::vector<VkDescriptorSet>& sets) {
			mFirstSet = firstSet;
			mSets = sets;
		}

		void bind(VkCommandBuffer cmd) const;
		void bindSets(VkCommandBuffer cmd) const;
		void bindSets(VkCommandBuffer cmd, VkPipelineLayout layout) const;

		const GraphicsPipeline* pipeline() const { return mPipeline; }
		bool isValid() const { return mPipeline && mPipeline->isValid(); }

		// Hands the descriptor sets out and forgets them, so whoever allocated them can
		// take them back. Leaves this material bindable but describing nothing, which is
		// why only the code releasing it should call this.
		std::vector<VkDescriptorSet> takeSets() {
			std::vector<VkDescriptorSet> taken = std::move(mSets);
			mSets.clear();
			return taken;
		}

	private:
		const GraphicsPipeline*      mPipeline = nullptr;
		std::vector<VkDescriptorSet> mSets;
		uint32_t                     mFirstSet = 0;
	};
}
