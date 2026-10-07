#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Writing a PNG, for pictures the engine produces rather than reads.
//
// stb_image is vendored for reading one. Nothing was vendored for writing one, because
// until there was a reason to hand a frame back to a person there was nothing to write.
//
// This is deliberately the simplest encoder that produces a file every reader accepts: no
// filtering, and deflate's *stored* mode, which is to say no compression at all. A capture
// of an interface comes out around the size of its pixels plus a thousandth. That is the
// right trade for what this is for -- a frame somebody is about to look at, written once --
// and it keeps a compressor out of the engine. If these ever need to be small, the place to
// start is a Paeth filter and fixed-Huffman blocks, not a dependency.
namespace RDA::Png {

	// `rgba` is width * height * 4 bytes, row-major from the top. False with a reason in
	// the log when the file cannot be written or the size does not match the data.
	bool write(const std::string& path, const unsigned char* rgba,
	           uint32_t width, uint32_t height);

	// The same bytes, for a caller that wants them in memory.
	std::vector<unsigned char> encode(const unsigned char* rgba,
	                                  uint32_t width, uint32_t height);
}
