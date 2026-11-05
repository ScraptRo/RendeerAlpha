#include <GraphicalObjects/Widgets/Media/Image.h>
#include <GraphicalObjects/Images.h>
#include <Core/Svg.h>
#include <GraphicalObjects/Gui.h>
#include <Logger/Logger.h>
#include <cmath>
#include <algorithm>

namespace RDA {

	// ---- Image ---------------------------------------------------------------------
	Image::~Image() { release(); }

	// The texture is not destroyed here, and cannot be.
	//
	// This widget is destroyed from the application's update -- a screen swapped, a layout
	// reloaded -- and by then the frame's draw list has already been built, holding this
	// texture's address. It is recorded from after the update returns. Destroying the
	// texture here means the GUI records a descriptor from freed memory, which is what an
	// `Invalid VkImageView 0xdddddddddddddddd` is: the fill pattern of a freed heap block.
	//
	// So ownership goes to the engine, which destroys it at the top of a frame -- once the
	// GPU is idle and the GUI has released the descriptor set it cached by that address.
	void Image::retireFrames() const {
		for (auto& frame : mFrames) {
			if (frame) retireTexture(std::move(frame));
		}
		mFrames.clear();
		mPlayhead = 0.0f;
	}

	void Image::release() const {
		if (mTexture) retireTexture(std::move(mTexture));
		mTexture.reset();
		retireFrames();
		// Borrowed, so there is nothing to free -- but it must stop pointing at it, or a
		// source that changed from a registered picture to a file would go on drawing the
		// picture.
		mShared = nullptr;
	}

	void Image::ensureLoaded() const {
		const std::string_view prefix(kMemoryImagePrefix);
		const bool registered = source.compare(0, prefix.size(), prefix) == 0;

		// A registered picture is re-asked for whenever the registry moves, because a
		// redefine replaces the texture behind the same name -- and may well reuse the
		// address, so comparing pointers would miss it.
		if (registered) {
			if (mLoaded == source && mSharedRevision == images().revision()) return;
			release();
			mLoaded = source;
			mSharedRevision = images().revision();
			mShared = images().find(source.substr(prefix.size()));
			if (!mShared && !mFailed) {
				mFailed = true;
				RDA_LOG_WARNING("Image '" << mId << "': nothing registered as '"
				                << source.substr(prefix.size())
				                << "'. Register it before the frame that shows it -- in "
				                   "Python rda.define_image(name, bytes).");
			} else if (mShared) {
				mFailed = false;
			}
			return;
		}

		if (mLoaded == source && ((mTexture && mTexture->isValid()) || mFailed)) return;
		release();
		mLoaded = source;
		mFailed = false;
		if (source.empty()) { mFailed = true; return; }
		mTexture = std::make_unique<Texture>(Texture::loadFromFile(source));
		if (!mTexture->isValid()) {
			mFailed = true;
			mTexture.reset();
			RDA_LOG_WARNING("Image '" << mId << "': cannot load " << source);
		}
	}

	bool Image::isDrawing() const {
		// By extension, which is how every other picture decides what it is. A drawing has
		// no size of its own until somebody says how big to draw it, which is the whole
		// reason it cannot go down the same path as a file.
		return source.size() > 4 &&
		       (source.compare(source.size() - 4, 4, ".svg") == 0 ||
		        source.compare(source.size() - 4, 4, ".SVG") == 0);
	}

	void Image::ensureDrawn(uint32_t width, uint32_t height) const {
		if (width == 0 || height == 0) return;
		// Read once. Changing size re-draws the shape; it does not re-read the file, which
		// is the point of keeping the parsed document.
		if (mLoaded != source || !mDrawing) {
			release();
			mLoaded = source;
			mFailed = false;
			mDrawnWidth = mDrawnHeight = 0;
			std::shared_ptr<Svg::Picture> parsed = Svg::load(source);
			if (!parsed) {
				mFailed = true;
				mDrawing.reset();
				return;
			}
			mDrawing = parsed;
		}
		if (mFailed || !mDrawing) return;
		const bool sized = mDrawnWidth == width && mDrawnHeight == height;
		if (sized && (mTexture || !mFrames.empty())) return;


		const Svg::Picture& picture = *static_cast<const Svg::Picture*>(mDrawing.get());
		mLoopSeconds = Svg::duration(picture);
		mDrawnWidth = width;
		mDrawnHeight = height;
		// Handed over rather than dropped. These are the frames the *previous* size was
		// drawn at, and the GUI is holding a descriptor set for each one that it looks up
		// by address -- so destroying them here frees a sampler a frame in flight is
		// still reading from, and the address is then handed straight back by the
		// allocator to one of the new frames below, which is what made the GUI find a set
		// describing an image that had been rebuilt underneath it.
		retireFrames();

		// A still icon is one picture. A moving one is its whole loop, rasterised now so
		// that playing it later is free -- see the note beside mFrames.
		//
		// Thirty a second rather than sixty: an icon is small and a loop is short, and
		// nobody has ever looked at a spinner and wanted more frames. Capped, because a
		// long loop at a large size is memory somebody did not ask for, and a slower icon
		// is better than a hitch.
		const int wanted = mLoopSeconds > 0.0f
			? std::clamp(static_cast<int>(std::ceil(mLoopSeconds * 30.0f)), 2, 120) : 1;

		std::vector<unsigned char> pixels;
		for (int i = 0; i < wanted; ++i) {
			const float at = wanted > 1
				? mLoopSeconds * static_cast<float>(i) / static_cast<float>(wanted) : 0.0f;
			if (!Svg::rasterise(picture, width, height, pixels, at)) {
				mFailed = true;
				return;
			}
			auto frame = std::make_unique<Texture>(
				Texture::fromPixels(pixels.data(), width, height));
			if (!frame->isValid()) {
				mFailed = true;
				retireFrames();
				RDA_LOG_WARNING("Image '" << mId << "': cannot make a " << width << "x"
				                << height << " surface for " << source);
				return;
			}
			mFrames.push_back(std::move(frame));
		}
		if (mTexture) retireTexture(std::move(mTexture));
		mTexture.reset();
	}

	glm::vec2 Image::measureContent(Gui& gui, glm::vec2 available) const {
		(void)gui;
		// A drawing is whatever size it says it is, without drawing anything: asking how
		// big it wants to be must not need a texture, because the answer is what decides
		// how big the texture will be.
		if (isDrawing()) {
			if (mLoaded != source || !mDrawing) {
				mLoaded = source;
				mFailed = false;
				std::shared_ptr<Svg::Picture> parsed = Svg::load(source);
				if (!parsed) { mFailed = true; return { 0.0f, 0.0f }; }
				mDrawing = parsed;
			}
			float w = 0.0f, h = 0.0f;
			Svg::size(*static_cast<const Svg::Picture*>(mDrawing.get()), w, h);
			const glm::vec2 own(w, h);
			if (available.x <= 0.0f || own.x <= available.x) return own;
			return { available.x, own.y * (available.x / own.x) };
		}
		ensureLoaded();
		const Texture* picture = active();
		if (!picture) return { 0.0f, 0.0f };
		const glm::vec2 own(static_cast<float>(picture->extent().width),
		                    static_cast<float>(picture->extent().height));
		// A picture bigger than the room it is in asks for the room, keeping its shape --
		// otherwise width="content" on a photograph asks for four thousand pixels.
		if (available.x <= 0.0f || own.x <= available.x) return own;
		return { available.x, own.y * (available.x / own.x) };
	}

	void Image::paint(Gui& gui, glm::vec2 origin) {
		const Rect box = placement(gui, origin);
		if (isDrawing()) {
			// Drawn at the box, not at some size it was exported at. `contain` is already
			// what the rasteriser does with the shape inside the box it is given, so both
			// fits come out of one call and there is no second scaling afterwards to go
			// soft.
			// Rasterised in pixels, drawn in units. The box is 24 units wide and on a
			// display at 150% that is 36 pixels of picture -- which is the whole reason a
			// drawing is rasterised at the size it is given rather than exported at one.
			const float k = gui.unitScale();
			ensureDrawn(static_cast<uint32_t>((std::max)(1.0f, std::round(box.w * k))),
			            static_cast<uint32_t>((std::max)(1.0f, std::round(box.h * k))));
			if (mFrames.empty()) return;
			size_t frame = 0;
			if (mFrames.size() > 1 && mLoopSeconds > 0.0f) {
				mPlayhead = std::fmod(mPlayhead + gui.input().dt, mLoopSeconds);
				frame = (std::min)(mFrames.size() - 1,
				                   static_cast<size_t>(mPlayhead / mLoopSeconds *
				                                       static_cast<float>(mFrames.size())));
				// It will look different next frame, and nothing else on this window can
				// tell -- the tree has not changed and neither has the input.
				gui.keepAwake();
			}
			gui.image(box, mFrames[frame].get(), tint);
			return;
		}
		ensureLoaded();
		const Texture* picture = active();
		if (!picture) return;
		if (fit == Fit::Stretch) { gui.image(box, picture, tint); return; }

		// Contain: the largest rectangle of the picture's shape that fits, centred. Done
		// here rather than in a shader, which keeps the quad the thing that is positioned
		// -- the same as everything else in this file.
		const float ow = static_cast<float>(picture->extent().width);
		const float oh = static_cast<float>(picture->extent().height);
		if (ow <= 0.0f || oh <= 0.0f || box.w <= 0.0f || box.h <= 0.0f) return;
		const float scale = (std::min)(box.w / ow, box.h / oh);
		const float w = ow * scale;
		const float h = oh * scale;
		gui.image({ box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h }, picture,
		          tint);
	}

	bool Image::describe(AccessibleInfo& out) const {
		// A picture nobody described is decoration, and a screen reader saying "image"
		// at every icon is noise rather than help.
		if (alt.empty()) return false;
		out.role = AccessibleRole::Image;
		out.name = alt;
		return true;
	}
}
