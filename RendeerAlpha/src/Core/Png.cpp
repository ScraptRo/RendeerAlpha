#include <Core/Png.h>
#include <Logger/Logger.h>

#include <cstring>
#include <fstream>

namespace RDA::Png {

	namespace {

		void be32(std::vector<unsigned char>& out, uint32_t value) {
			out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
			out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
			out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
			out.push_back(static_cast<unsigned char>(value & 0xFF));
		}

		// CRC-32 as PNG specifies it, over the chunk's type and data. The table is built
		// once on first use rather than written out as 256 constants.
		uint32_t crc32(const unsigned char* data, size_t size) {
			static uint32_t table[256];
			static bool ready = false;
			if (!ready) {
				for (uint32_t i = 0; i < 256; ++i) {
					uint32_t c = i;
					for (int k = 0; k < 8; ++k) {
						c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
					}
					table[i] = c;
				}
				ready = true;
			}
			uint32_t c = 0xFFFFFFFFu;
			for (size_t i = 0; i < size; ++i) {
				c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
			}
			return c ^ 0xFFFFFFFFu;
		}

		// Adler-32 over the *uncompressed* stream, which is what a zlib trailer carries.
		uint32_t adler32(const unsigned char* data, size_t size) {
			uint32_t a = 1, b = 0;
			for (size_t i = 0; i < size; ++i) {
				a = (a + data[i]) % 65521u;
				b = (b + a) % 65521u;
			}
			return (b << 16) | a;
		}

		void chunk(std::vector<unsigned char>& out, const char type[4],
		           const std::vector<unsigned char>& data) {
			be32(out, static_cast<uint32_t>(data.size()));
			const size_t typeAt = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), data.begin(), data.end());
			be32(out, crc32(out.data() + typeAt, 4 + data.size()));
		}
	}

	std::vector<unsigned char> encode(const unsigned char* rgba,
	                                  uint32_t width, uint32_t height) {
		std::vector<unsigned char> out;
		if (!rgba || width == 0 || height == 0) return out;

		// Every scanline is preceded by its filter type. Zero is "none", which is the
		// whole of the filtering this writer does.
		std::vector<unsigned char> raw;
		raw.reserve(static_cast<size_t>(height) * (1u + static_cast<size_t>(width) * 4u));
		for (uint32_t y = 0; y < height; ++y) {
			raw.push_back(0);
			const unsigned char* row = rgba + static_cast<size_t>(y) * width * 4u;
			raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4u);
		}

		out.reserve(raw.size() + raw.size() / 1000u + 256u);
		const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		out.insert(out.end(), signature, signature + 8);

		std::vector<unsigned char> ihdr;
		be32(ihdr, width);
		be32(ihdr, height);
		ihdr.push_back(8);   // bits per channel
		ihdr.push_back(6);   // colour type 6: RGBA
		ihdr.push_back(0);   // deflate, the only compression PNG has
		ihdr.push_back(0);   // filter method 0
		ihdr.push_back(0);   // not interlaced
		chunk(out, "IHDR", ihdr);

		// A zlib stream whose deflate blocks are all stored: a header, then LEN bytes
		// verbatim per block, then the Adler of everything that went in.
		std::vector<unsigned char> idat;
		idat.reserve(raw.size() + raw.size() / 65535u * 5u + 16u);
		idat.push_back(0x78);   // deflate, 32K window
		idat.push_back(0x01);   // no preset dictionary, fastest
		size_t at = 0;
		while (at < raw.size() || raw.empty()) {
			const size_t left = raw.size() - at;
			const uint16_t take = static_cast<uint16_t>(left > 65535u ? 65535u : left);
			const bool last = (at + take) >= raw.size();
			idat.push_back(last ? 1 : 0);                       // BFINAL, BTYPE = stored
			idat.push_back(static_cast<unsigned char>(take & 0xFF));
			idat.push_back(static_cast<unsigned char>((take >> 8) & 0xFF));
			idat.push_back(static_cast<unsigned char>(~take & 0xFF));
			idat.push_back(static_cast<unsigned char>((~take >> 8) & 0xFF));
			idat.insert(idat.end(), raw.begin() + at, raw.begin() + at + take);
			at += take;
			if (last) break;
		}
		be32(idat, adler32(raw.data(), raw.size()));
		chunk(out, "IDAT", idat);

		chunk(out, "IEND", {});
		return out;
	}

	bool write(const std::string& path, const unsigned char* rgba,
	           uint32_t width, uint32_t height) {
		const std::vector<unsigned char> bytes = encode(rgba, width, height);
		if (bytes.empty()) {
			RDA_LOG_WARNING("png: nothing to write for " << path << " (" << width << "x"
			                << height << ")");
			return false;
		}
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file || !file.write(reinterpret_cast<const char*>(bytes.data()),
		                         static_cast<std::streamsize>(bytes.size()))) {
			RDA_LOG_WARNING("png: cannot write " << path);
			return false;
		}
		return true;
	}
}
