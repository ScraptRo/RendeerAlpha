#include <GraphicalObjects/Images.h>
#include <GraphicalObjects/Texture.h>
#include <Logger/Logger.h>

namespace RDA {

	Images::~Images() {
		// Nothing here: by the time a static teardown reaches this the device is gone, and
		// freeing a Vulkan image then is the assertion inside VMA that says an allocation
		// outlived the block it came from. clear() is called at shutdown, while there is
		// still something to free it with -- see engineMain.
		mByName.clear();
	}

	bool Images::define(const std::string& name, const void* bytes, size_t size) {
		if (name.empty() || !bytes || size == 0) {
			RDA_LOG_WARNING("image: nothing to define '" << name << "' from");
			return false;
		}
		auto texture = std::make_unique<Texture>(Texture::loadFromMemory(bytes, size));
		if (!texture->isValid()) {
			RDA_LOG_WARNING("image '" << name << "': those " << size
			                << " bytes are not a picture this engine can read (PNG, JPEG, "
			                   "BMP, TGA, GIF, PSD, HDR, PNM)");
			return false;
		}
		mByName[name] = std::move(texture);
		++mRevision;
		return true;
	}

	bool Images::definePixels(const std::string& name, const void* rgba,
	                          uint32_t width, uint32_t height) {
		if (name.empty() || !rgba || width == 0 || height == 0) {
			RDA_LOG_WARNING("image: nothing to define '" << name << "' from");
			return false;
		}
		auto texture = std::make_unique<Texture>(Texture::fromPixels(rgba, width, height));
		if (!texture->isValid()) {
			RDA_LOG_WARNING("image '" << name << "': cannot make a " << width << "x"
			                << height << " texture");
			return false;
		}
		mByName[name] = std::move(texture);
		++mRevision;
		return true;
	}

	void Images::forget(const std::string& name) {
		if (mByName.erase(name) > 0) ++mRevision;
	}

	const Texture* Images::find(const std::string& name) const {
		const auto at = mByName.find(name);
		return at == mByName.end() ? nullptr : at->second.get();
	}

	void Images::clear() {
		if (mByName.empty()) return;
		mByName.clear();
		++mRevision;
	}

	Images& images() {
		static Images one;
		return one;
	}
}
