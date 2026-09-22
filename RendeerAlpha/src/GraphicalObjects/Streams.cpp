#include <GraphicalObjects/Streams.h>
#include <GraphicalObjects/Texture.h>
#include <Core/Datatypes.h>
#include <GraphicalSrc/DeviceHandler.h>
#include <GraphicalSrc/MemoryBuffer.h>
#include <array>
#include <cstring>
#include <cmath>
#include <mutex>
#include <Logger/Logger.h>
#include <vendor/stb_image/stb_image.h>

namespace RDA {

	namespace {
		// How many frames a stream stays "wanted" after the last time something drew it.
		//
		// Not one: an on-demand window may not have drawn at all since the producer last
		// asked, and a feed that stopped because the interface was idle for a moment would
		// have no way back. Long enough to survive a quiet second, short enough that a
		// screen the reader navigated away from stops costing anything.
		constexpr uint64_t kSeenWithin = 90;
	}

	Streams::~Streams() {
		// Nothing here: by the time static teardown reaches this the device is gone, and
		// freeing a Vulkan image then is the assertion inside VMA that says an allocation
		// outlived the block it came from. clear() is called at shutdown -- see engineMain.
		mByName.clear();
	}

	// Caller holds mLock.
	Streams::Live& Streams::slot(const std::string& name) {
		const auto found = mByName.find(name);
		if (found != mByName.end()) return found->second;
		Live& made = mByName[name];
		// Not zero: a stream is wanted for a window of frames after it is first heard of,
		// and that window has to start somewhere.
		made.knownFrame = mFrameNumber.load();
		return made;
	}

	bool Streams::upload(Live& live, const std::string& name, const void* rgba,
	                     uint32_t width, uint32_t height) {
		// The whole point: a frame the same size as the last one goes into the texture
		// that is already there. Only a size change builds a new one.
		if (!live.texture || live.width != width || live.height != height) {
			auto built = std::make_unique<Texture>(Texture::fromPixels(rgba, width, height));
			if (!built->isValid()) {
				RDA_LOG_WARNING("stream '" << name << "': cannot make a " << width << "x"
				                << height << " surface");
				return false;
			}
			live.texture = std::move(built);
			live.width = width;
			live.height = height;
			live.writable = false;
		} else {
			const VkDeviceSize bytes =
				static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
			if (!live.texture->uploadPixels(rgba, bytes)) {
				RDA_LOG_WARNING("stream '" << name << "': that frame would not upload");
				return false;
			}
		}
		++live.pushed;
		live.fresh = true;
		++mRevision;
		return true;
	}

	bool Streams::push(const std::string& name, const void* rgba,
	                   uint32_t width, uint32_t height) {
		if (name.empty() || !rgba || width == 0 || height == 0) {
			RDA_LOG_WARNING("stream: nothing to push into '" << name << "'");
			return false;
		}
		std::lock_guard<std::mutex> held(mLock);
		return upload(slot(name), name, rgba, width, height);
	}

	bool Streams::pushEncoded(const std::string& name, const void* bytes, size_t size) {
		if (name.empty() || !bytes || size == 0) {
			RDA_LOG_WARNING("stream: nothing to push into '" << name << "'");
			return false;
		}
		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(bytes),
		                                        static_cast<int>(size),
		                                        &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels) {
			RDA_LOG_WARNING("stream '" << name << "': those " << size
			                << " bytes are not a frame this engine can read (PNG, JPEG, "
			                   "BMP, TGA, GIF, PSD, HDR, PNM)");
			return false;
		}
		bool ok = false;
		{
			std::lock_guard<std::mutex> held(mLock);
			ok = upload(slot(name), name, pixels, static_cast<uint32_t>(width),
			            static_cast<uint32_t>(height));
		}
		stbi_image_free(pixels);
		return ok;
	}

	Texture* Streams::target(const std::string& name, uint32_t width, uint32_t height) {
		if (name.empty() || width == 0 || height == 0) {
			RDA_LOG_WARNING("stream: cannot make a surface for '" << name << "'");
			return nullptr;
		}
		std::lock_guard<std::mutex> held(mLock);
		Live& live = slot(name);
		if (live.texture && live.writable && live.width == width && live.height == height) {
			return live.texture.get();
		}

		TextureDesc desc;
		desc.width = width;
		desc.height = height;
		// UNORM, not sRGB: Vulkan has no storage image in an sRGB format. What is stored
		// is therefore linear, which is what the GUI's image path wants anyway.
		desc.format = VK_FORMAT_R8G8B8A8_UNORM;
		// TRANSFER_SRC as well, so read() can copy it back. A pushed frame gets it from
		// building a mip chain; this one has no chain and would otherwise be the one kind
		// of surface you could filter and not read.
		desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.withSampler = true;
		// No mip chain: it is rewritten every time something runs over it, and a chain
		// would have to be rebuilt each time to be anything but stale.
		desc.mipmapped = false;

		auto built = std::make_unique<Texture>();
		if (!built->create(desc)) {
			RDA_LOG_WARNING("stream '" << name << "': cannot make a " << width << "x"
			                << height << " surface to write into");
			return nullptr;
		}
		live.texture = std::move(built);
		live.width = width;
		live.height = height;
		live.writable = true;
		++mRevision;
		return live.texture.get();
	}

	void Streams::noteWritten(const std::string& name) {
		std::lock_guard<std::mutex> held(mLock);
		const auto at = mByName.find(name);
		if (at == mByName.end()) return;
		++at->second.pushed;
		at->second.fresh = true;
		++mRevision;
	}

	bool Streams::read(const std::string& name, std::vector<unsigned char>& out,
	                   uint32_t& width, uint32_t& height) const {
		out.clear();
		width = 0;
		height = 0;

		VkImage image = VK_NULL_HANDLE;
		VkImageLayout wasIn = VK_IMAGE_LAYOUT_UNDEFINED;
		bool linear = false;
		{
			std::lock_guard<std::mutex> held(mLock);
			const auto at = mByName.find(name);
			if (at == mByName.end() || !at->second.texture || !at->second.texture->isValid()) {
				RDA_LOG_WARNING("stream '" << name << "': there is no frame to read");
				return false;
			}
			const Live& live = at->second;
			image = live.texture->image();
			wasIn = live.texture->layout();
			// A surface an effect wrote holds linear light; a pushed frame holds what was
			// pushed. Only the first needs encoding on the way out.
			linear = live.writable;
			width = live.width;
			height = live.height;
		}

		const VkDeviceSize bytes =
			static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

		MemoryBuffer staging;
		if (!staging.create(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryResidence::GpuToCpu)) {
			RDA_LOG_WARNING("stream '" << name << "': cannot make room to read " << bytes
			                << " bytes back");
			return false;
		}

		VkBuffer target = staging.handle();
		immediateSubmit([&](VkCommandBuffer cmd) {
			VkImageMemoryBarrier toRead{};
			toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toRead.oldLayout = wasIn;
			toRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.image = image;
			toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			toRead.subresourceRange.levelCount = 1;
			toRead.subresourceRange.layerCount = 1;
			toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			toRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
			                     1, &toRead);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { width, height, 1 };
			vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			                       target, 1, &region);

			// Back where it was, so the next frame draws it without noticing this happened.
			VkImageMemoryBarrier back = toRead;
			back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			back.newLayout = wasIn == VK_IMAGE_LAYOUT_UNDEFINED
				? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : wasIn;
			back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			back.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
			                     nullptr, 1, &back);
		});

		const void* mapped = staging.map();
		if (!mapped) {
			RDA_LOG_WARNING("stream '" << name << "': the frame would not come back");
			return false;
		}
		out.resize(static_cast<size_t>(bytes));
		std::memcpy(out.data(), mapped, static_cast<size_t>(bytes));
		staging.unmap();

		if (linear) {
			// Encoded once, on a lookup table: 256 values, and the alternative is a pow()
			// per channel per pixel, which for a 4K frame is twenty-five million of them.
			static const std::array<unsigned char, 256> kEncoded = [] {
				std::array<unsigned char, 256> table{};
				for (int i = 0; i < 256; ++i) {
					const double x = i / 255.0;
					const double y = x <= 0.0031308 ? 12.92 * x
					                                : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
					table[static_cast<size_t>(i)] =
						static_cast<unsigned char>(y * 255.0 + 0.5);
				}
				return table;
			}();
			for (size_t i = 0; i + 3 < out.size(); i += 4) {
				out[i + 0] = kEncoded[out[i + 0]];
				out[i + 1] = kEncoded[out[i + 1]];
				out[i + 2] = kEncoded[out[i + 2]];
				// Alpha is coverage, not light.
			}
		}
		return true;
	}

	void Streams::close(const std::string& name) {
		std::lock_guard<std::mutex> held(mLock);
		if (mByName.erase(name) > 0) ++mRevision;
	}

	const Texture* Streams::find(const std::string& name) const {
		std::lock_guard<std::mutex> held(mLock);
		const auto at = mByName.find(name);
		if (at == mByName.end()) return nullptr;
		return at->second.texture.get();
	}

	bool Streams::wanted(const std::string& name) const {
		std::lock_guard<std::mutex> held(mLock);
		const uint64_t now = mFrameNumber.load();
		const auto at = mByName.find(name);
		// Never heard of: the interface may not have painted once yet, and answering "no"
		// would stop a producer before it began -- a widget waiting for a frame and a
		// frame waiting for a widget.
		if (at == mByName.end()) return true;

		Live& live = const_cast<Live&>(at->second);
		// Drawn recently is the ordinary yes.
		if (live.lastSeenFrame != 0) return now - live.lastSeenFrame <= kSeenWithin;

		// Never drawn. Wanted for the same window after it was first heard of, and then
		// not -- because by now the answer really is that nothing shows it, which is
		// usually a name spelt two ways.
		if (now - live.knownFrame <= kSeenWithin) return true;
		if (!live.warnedUnwatched) {
			live.warnedUnwatched = true;
			RDA_LOG_WARNING("stream '" << name << "': frames are arriving and no <stream> "
			                              "is showing it. Check the name against the "
			                              "layout's; nothing is being drawn.");
		}
		return false;
	}

	void Streams::markSeen(const std::string& name) {
		if (name.empty()) return;
		std::lock_guard<std::mutex> held(mLock);
		// Made here if it is not there: a <stream> naming it *is* the registration of
		// interest, and it usually paints before the first frame arrives.
		Live& live = slot(name);
		live.lastSeenFrame = mFrameNumber.load();
		if (live.lastSeenFrame == 0) live.lastSeenFrame = 1; // 0 means "never"
		live.warnedUnwatched = false;
		// Counted once per frame that was still current when it was drawn. A frame
		// replaced before anything sampled it is the waste this reports.
		if (live.fresh) {
			live.fresh = false;
			++live.shown;
		}
	}

	void Streams::counts(const std::string& name, uint64_t& pushed, uint64_t& shown) const {
		pushed = 0;
		shown = 0;
		std::lock_guard<std::mutex> held(mLock);
		const auto at = mByName.find(name);
		if (at == mByName.end()) return;
		pushed = at->second.pushed;
		shown = at->second.shown;
	}

	void Streams::clear() {
		std::lock_guard<std::mutex> held(mLock);
		if (mByName.empty()) return;
		mByName.clear();
		++mRevision;
	}

	Streams& streams() {
		static Streams one;
		return one;
	}
}
