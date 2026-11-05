#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Platform/OsWindow.h>
#include <Core/Svg.h>
#include <Logger/Logger.h>
#include <vendor/stb_image/stb_image.h>
#include <cctype>
#include <memory>
#include <vector>

// The picture the OS shows for a window: a .png as it is, or an .svg drawn at every size an
// OS reaches for.
namespace RDA {

	namespace {
		// One window icon, at one size, as the tightly packed RGBA the platform wants.
		struct IconImage {
			int width = 0, height = 0;
			std::vector<unsigned char> pixels;
		};

		// The sizes an OS actually reaches for: the title bar and the tray at the small
		// end, the alt-tab card and the taskbar in the middle, and the large one for a
		// window list that draws thumbnails. Given several, the platform picks; given one,
		// it scales, and a 16px icon scaled to 64 is the usual reason an application looks
		// unfinished in the task switcher.
		constexpr int kIconSizes[] = { 16, 24, 32, 48, 64, 128, 256 };

		// A drawing is rasterised at every one of them, because it can be: the shape is
		// the file, so each size is drawn rather than resampled and the small ones are
		// sharp instead of being a blurred large one. A .png is what it is, and is handed
		// over as the single size it was saved at.
		bool loadIcon(const std::string& path, std::vector<IconImage>& out) {
			out.clear();
			if (path.empty()) return true;   // "no icon" is a valid thing to ask for

			// Spelled out rather than through a case-insensitive compare, because the
			// one that exists is _stricmp on Windows and strcasecmp everywhere else.
			bool drawn = false;
			if (path.size() > 4) {
				std::string tail = path.substr(path.size() - 4);
				for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				drawn = tail == ".svg";
			}
			if (drawn) {
				std::shared_ptr<Svg::Picture> picture = Svg::load(path);
				if (!picture) {
					RDA_LOG_WARNING("Window icon: cannot read " << path);
					return false;
				}
				for (int size : kIconSizes) {
					IconImage image;
					if (!Svg::rasterise(*picture, static_cast<uint32_t>(size),
					                    static_cast<uint32_t>(size), image.pixels)) {
						continue;
					}
					image.width = image.height = size;
					out.push_back(std::move(image));
				}
				if (out.empty()) RDA_LOG_WARNING("Window icon: nothing drawn from " << path);
				return !out.empty();
			}

			int w = 0, h = 0, channels = 0;
			unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
			if (!pixels) {
				RDA_LOG_WARNING("Window icon: cannot read " << path << " ("
				                << (stbi_failure_reason() ? stbi_failure_reason() : "?") << ")");
				return false;
			}
			IconImage image;
			image.width = w;
			image.height = h;
			image.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
			stbi_image_free(pixels);
			out.push_back(std::move(image));
			return true;
		}
	}

	bool OsWindow::setIcon(const std::string& path) {
		if (!mWindow) return false;
		if (path.empty()) {
			glfwSetWindowIcon(mWindow, 0, nullptr);   // back to the platform's own
			mInfo.style.icon.clear();
			return true;
		}
		std::vector<IconImage> images;
		if (!loadIcon(path, images) || images.empty()) return false;

		std::vector<GLFWimage> handed;
		handed.reserve(images.size());
		for (IconImage& image : images) {
			handed.push_back(GLFWimage{ image.width, image.height, image.pixels.data() });
		}
		glfwSetWindowIcon(mWindow, static_cast<int>(handed.size()), handed.data());
		mInfo.style.icon = path;
		return true;
	}
}
