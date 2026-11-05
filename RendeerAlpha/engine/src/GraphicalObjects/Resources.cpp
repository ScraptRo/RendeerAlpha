#include <GraphicalObjects/Resources.h>
#include <Logger/Logger.h>

#include <new>

namespace RDA {

	namespace {
		// Engine-owned resource pools. Everything created through the factories below
		// lives here so teardown can release every GPU resource while the device is
		// still alive, regardless of what handles the application still holds.
		obj_list<Mesh>    gMeshPool(64);
		obj_list<Texture> gTexturePool(64);
	}

	obj_ref<Mesh> createMesh(MeshKind kind) {
		obj_ref<Mesh> ref = gMeshPool.addObject();
		if (ref.IsValid()) {
			::new (ref.GetRawPointer()) Mesh();
			ref->setKind(kind);
		}
		return ref;
	}

	obj_ref<Texture> createTexture() {
		obj_ref<Texture> ref = gTexturePool.addObject();
		if (ref.IsValid()) {
			::new (ref.GetRawPointer()) Texture();
		}
		return ref;
	}

	obj_ref<Texture> loadTexture(const std::string& path, bool srgb) {
		obj_ref<Texture> ref = createTexture();
		if (ref.IsValid()) {
			*ref = Texture::loadFromFile(path, srgb);
		}
		return ref;
	}

	obj_ref<Texture> createTextureFromPixels(const void* rgba, uint32_t width, uint32_t height, bool srgb) {
		obj_ref<Texture> ref = createTexture();
		if (!ref.IsValid() || !rgba || width == 0 || height == 0) return ref;

		TextureDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		// A 1x1 default gets a single level anyway; anything larger is real content.
		desc.mipmapped = (width > 1 || height > 1);
		if (!ref->create(desc)) {
			RDA_LOG_ERROR("Failed to create a texture from pixels");
			return ref;
		}
		ref->uploadPixels(rgba, static_cast<VkDeviceSize>(width) * height * 4);
		return ref;
	}

	void releaseResourcePools() {
		gMeshPool.erase_all();
		gTexturePool.erase_all();
	}
}
