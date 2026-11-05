#pragma once
#include <GraphicalObjects/Mesh.h>
#include <GraphicalObjects/Texture.h>
#include <vendor/RDA_Library/obj_ref.h>
#include <cstdint>
#include <string>

// The engine-owned pools that meshes and textures handed to an application come from.
//
// Everything created through these lives in a pool the engine destroys at teardown, while
// the device and allocator are still alive -- so a handle the application still holds
// becomes inert rather than dangling, and reference counting frees a resource early once
// nothing holds it. They were in the loop file; they are here, beside what they pool, so
// the primitives and anything else that makes a mesh does not include the engine's API.
namespace RDA {

	// ---- Resources ----
	// Call these from the AppConfig callbacks (they run on the loop thread). MeshKind is
	// stored on the mesh for a future render path to dispatch on; the current forward
	// path draws every mesh the same way.
	obj_ref<Mesh>    createMesh(MeshKind kind = MeshKind::Opaque);
	obj_ref<Texture> createTexture();
	obj_ref<Texture> loadTexture(const std::string& path, bool srgb = true);
	// A sampled texture from tightly packed RGBA8 pixels — for generated data (noise,
	// gradients, lookup tables) that never lives in a file. `srgb` false for anything
	// read as data rather than colour: normal maps, roughness, masks.
	obj_ref<Texture> createTextureFromPixels(const void* rgba, uint32_t width, uint32_t height,
	                                         bool srgb = true);

	// Latches both pools closed and destroys every pooled resource. Teardown calls it
	// while the device is still alive; any handle held past it is inert.
	void releaseResourcePools();
}
