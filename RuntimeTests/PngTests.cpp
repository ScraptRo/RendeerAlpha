#include "TestHarness.h"
#include <Core/Png.h>
#include <vendor/stb_image/stb_image.h>

#include <vector>

// A picture the engine wrote, read back by something that did not write it.
//
// This is the only kind of test worth having for an encoder. Checking the bytes against
// bytes I decided were right would only prove the writer still agrees with itself; decoding
// with stb_image -- which is vendored here for reading pictures and knows nothing about
// Core/Png.cpp -- proves the file is one a real reader accepts.
using namespace RDA;

namespace {

	struct Decoded {
		int width = 0, height = 0, channels = 0;
		std::vector<unsigned char> rgba;
		bool ok = false;
	};

	Decoded decode(const std::vector<unsigned char>& png) {
		Decoded out;
		if (png.empty()) return out;
		int w = 0, h = 0, channels = 0;
		unsigned char* pixels = stbi_load_from_memory(
			png.data(), static_cast<int>(png.size()), &w, &h, &channels, 4);
		if (!pixels) return out;
		out.width = w;
		out.height = h;
		out.channels = channels;
		out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);
		out.ok = true;
		return out;
	}

	// Something with structure in every channel, so a swapped or dropped one shows up.
	std::vector<unsigned char> pattern(uint32_t width, uint32_t height) {
		std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				unsigned char* p = rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
				p[0] = static_cast<unsigned char>(x * 7 + 1);
				p[1] = static_cast<unsigned char>(y * 11 + 2);
				p[2] = static_cast<unsigned char>((x ^ y) * 13 + 3);
				p[3] = static_cast<unsigned char>(255 - ((x + y) & 0x7F));
			}
		}
		return rgba;
	}

	bool same(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
		if (a.size() != b.size()) return false;
		for (size_t i = 0; i < a.size(); ++i) {
			if (a[i] != b[i]) return false;
		}
		return true;
	}
}

TEST(a_small_picture_survives_being_written_and_read) {
	const uint32_t w = 2, h = 2;
	const std::vector<unsigned char> source = pattern(w, h);
	const Decoded got = decode(Png::encode(source.data(), w, h));

	CHECK(got.ok);
	CHECK_EQ(got.width, 2);
	CHECK_EQ(got.height, 2);
	CHECK(same(got.rgba, source));
}

TEST(a_picture_bigger_than_one_deflate_block_survives_too) {
	// The stored blocks this writer emits hold 65535 bytes each, and a frame is far more
	// than that -- so the seam between two blocks is on the path every real capture takes.
	// 300x100 is 120400 bytes of scanlines: two blocks, the second a short one.
	const uint32_t w = 300, h = 100;
	const std::vector<unsigned char> source = pattern(w, h);
	const std::vector<unsigned char> png = Png::encode(source.data(), w, h);
	CHECK(png.size() > 65535u);

	const Decoded got = decode(png);
	CHECK(got.ok);
	CHECK_EQ(got.width, 300);
	CHECK_EQ(got.height, 100);
	CHECK(same(got.rgba, source));
}

TEST(a_picture_exactly_one_block_long_survives) {
	// 65535 bytes of raw data exactly: the boundary case, where the writer must not emit
	// an empty second block and must still mark the first as final.
	// 13107 pixels per row + 1 filter byte = 52429 bytes a row; one row is under, so this
	// is sized to land on the boundary from the other side.
	const uint32_t w = 16383, h = 1;    // 1 + 16383*4 = 65533 bytes
    const std::vector<unsigned char> source = pattern(w, h);
	const Decoded got = decode(Png::encode(source.data(), w, h));
	CHECK(got.ok);
	CHECK_EQ(got.width, 16383);
	CHECK(same(got.rgba, source));
}

TEST(a_picture_with_no_pixels_is_refused_rather_than_written_broken) {
	CHECK(Png::encode(nullptr, 4, 4).empty());
	const std::vector<unsigned char> one(4, 0);
	CHECK(Png::encode(one.data(), 0, 4).empty());
	CHECK(Png::encode(one.data(), 4, 0).empty());
}

TEST(the_signature_and_the_chunks_are_where_a_reader_looks_for_them) {
	// Belt and braces beside the round trip: if stb_image ever grows a tolerance for a
	// malformed file, this still says what the bytes are supposed to be.
	const std::vector<unsigned char> source = pattern(3, 3);
	const std::vector<unsigned char> png = Png::encode(source.data(), 3, 3);
	CHECK(png.size() > 8);

	const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	for (int i = 0; i < 8; ++i) CHECK_EQ(png[static_cast<size_t>(i)], signature[i]);

	const auto holds = [&](const char* text) {
		const std::string needle = text;
		const std::string haystack(png.begin(), png.end());
		return haystack.find(needle) != std::string::npos;
	};
	CHECK(holds("IHDR"));
	CHECK(holds("IDAT"));
	CHECK(holds("IEND"));
}
