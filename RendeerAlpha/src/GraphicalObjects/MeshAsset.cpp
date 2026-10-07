#include <GraphicalObjects/MeshAsset.h>
#include <Logger/Logger.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace RDA {

	namespace {
		// "RDAM", little-endian, so a file opened in a hex editor says what it is.
		constexpr uint32_t kMagic = 0x4D414452u;
		constexpr uint32_t kVersion = 1;

		struct Header {
			uint32_t magic, version;
			uint32_t vertexCount, indexCount, stateCount, regionCount, paramCount;
			uint32_t groupCount, vertsPerState, stringBytes;
		};

		struct StateRec  { uint32_t name; uint32_t vertexFirst; };
		struct RegionRec { uint32_t name; uint32_t kind; float box[8]; };
		struct ParamRec  { uint32_t name; uint32_t doc; uint32_t kind; uint32_t colour; float number; };

		// Every read is bounds-checked against the size that was actually handed over.
		// A .rdam arrives from a build the engine did not run and may be truncated,
		// stale, or something else entirely that happens to have the right extension.
		struct Reader {
			const uint8_t* bytes;
			size_t size;
			size_t at = 0;
			bool ok = true;

			const void* take(size_t count) {
				if (!ok || at + count > size) { ok = false; return nullptr; }
				const void* here = bytes + at;
				at += count;
				return here;
			}
		};

		// A colour, blended. Per channel and in the bytes they are stored in, which is
		// what every other blend in this engine does -- the shader decodes sRGB, so a
		// lerp here is a lerp of the numbers a person wrote.
		uint32_t mixColour(uint32_t a, uint32_t b, float t) {
			if (t <= 0.0f) return a;
			if (t >= 1.0f) return b;
			uint32_t out = 0;
			for (int shift = 0; shift < 32; shift += 8) {
				const float x = static_cast<float>((a >> shift) & 0xFFu);
				const float y = static_cast<float>((b >> shift) & 0xFFu);
				const int mixed = static_cast<int>(x + (y - x) * t + 0.5f);
				out |= static_cast<uint32_t>(std::clamp(mixed, 0, 255)) << shift;
			}
			return out;
		}
	}

	bool WidgetMesh::parse(const uint8_t* data, size_t size, std::string& error) {
		Reader in{ data, size };
		const Header* header = static_cast<const Header*>(in.take(sizeof(Header)));
		if (!header) { error = "too short to hold a header"; return false; }
		if (header->magic != kMagic) { error = "not a mesh (bad magic)"; return false; }
		if (header->version != kVersion) {
			error = "built by a different version of rda (" + std::to_string(header->version) +
			        ", this engine reads " + std::to_string(kVersion) + ")";
			return false;
		}

		const auto* verts   = static_cast<const MeshVertex*>(in.take(sizeof(MeshVertex) * header->vertexCount));
		const auto* indices = static_cast<const uint16_t*>(in.take(sizeof(uint16_t) * header->indexCount));
		const auto* states  = static_cast<const StateRec*>(in.take(sizeof(StateRec) * header->stateCount));
		const auto* regions = static_cast<const RegionRec*>(in.take(sizeof(RegionRec) * header->regionCount));
		const auto* params  = static_cast<const ParamRec*>(in.take(sizeof(ParamRec) * header->paramCount));
		const auto* groups  = static_cast<const uint32_t*>(in.take(sizeof(uint32_t) * header->groupCount));
		const char* strings = static_cast<const char*>(in.take(header->stringBytes));
		if (!in.ok) { error = "truncated"; return false; }

		// Every string is an offset into one blob, and the blob ends in a NUL -- so a
		// name that runs off the end reads as empty rather than off the end of the file.
		const auto text = [&](uint32_t offset) -> std::string {
			if (!strings || offset >= header->stringBytes) return {};
			return std::string(strings + offset);
		};
		if (header->stringBytes > 0 && strings[header->stringBytes - 1] != '\0') {
			error = "the string table does not end";
			return false;
		}
		if (header->stateCount == 0 || header->vertsPerState == 0) {
			error = "has no states";
			return false;
		}
		if (static_cast<uint64_t>(header->stateCount) * header->vertsPerState != header->vertexCount) {
			error = "states do not share a vertex count, which is what makes them blendable";
			return false;
		}
		for (uint32_t i = 0; i < header->indexCount; ++i) {
			if (indices[i] >= header->vertsPerState) { error = "an index is out of range"; return false; }
		}

		mVertices.assign(verts, verts + header->vertexCount);
		mIndices.assign(indices, indices + header->indexCount);
		mVertsPerState = header->vertsPerState;

		mStates.clear();
		for (uint32_t i = 0; i < header->stateCount; ++i) mStates.push_back(text(states[i].name));
		mGroups.clear();
		for (uint32_t i = 0; i < header->groupCount; ++i) mGroups.push_back(text(groups[i]));

		mRegions.clear();
		for (uint32_t i = 0; i < header->regionCount; ++i) {
			MeshRegion one;
			one.name = text(regions[i].name);
			one.kind = static_cast<MeshRegionKind>(regions[i].kind);
			std::memcpy(one.box, regions[i].box, sizeof(one.box));
			mRegions.push_back(std::move(one));
		}

		mParams.clear();
		for (uint32_t i = 0; i < header->paramCount; ++i) {
			MeshParam one;
			one.name = text(params[i].name);
			one.doc = text(params[i].doc);
			one.kind = static_cast<MeshParamKind>(params[i].kind);
			one.colour = params[i].colour;
			one.number = params[i].number;
			mParams.push_back(std::move(one));
		}
		return true;
	}

	bool WidgetMesh::load(const std::string& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) return false;
		const std::streamsize size = file.tellg();
		if (size <= 0) return false;
		file.seekg(0);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return false;

		std::string error;
		if (!parse(bytes.data(), bytes.size(), error)) {
			RDA_LOG_WARNING("mesh '" << path << "': " << error);
			return false;
		}
		return true;
	}

	int WidgetMesh::stateIndex(const std::string& name) const {
		for (size_t i = 0; i < mStates.size(); ++i) {
			if (mStates[i] == name) return static_cast<int>(i);
		}
		return -1;
	}
	int WidgetMesh::paramIndex(const std::string& name) const {
		for (size_t i = 0; i < mParams.size(); ++i) {
			if (mParams[i].name == name) return static_cast<int>(i);
		}
		return -1;
	}
	int WidgetMesh::regionIndex(const std::string& name) const {
		for (size_t i = 0; i < mRegions.size(); ++i) {
			if (mRegions[i].name == name) return static_cast<int>(i);
		}
		return -1;
	}

	Rect WidgetMesh::regionRect(size_t index, const Rect& rect) const {
		if (index >= mRegions.size()) return rect;
		const float* b = mRegions[index].box;
		const float left   = rect.x + b[0] * rect.w + b[1];
		const float top    = rect.y + b[2] * rect.h + b[3];
		const float right  = rect.x + b[4] * rect.w + b[5];
		const float bottom = rect.y + b[6] * rect.h + b[7];
		return Rect{ left, top, (std::max)(0.0f, right - left), (std::max)(0.0f, bottom - top) };
	}

	void WidgetMesh::resolve(int from, int into, float t, const Rect& rect,
	                   const uint32_t* colours, size_t colourCount, uint32_t base,
	                   std::vector<GuiVertex>& out) const {
		if (mStates.empty()) return;
		const int last = static_cast<int>(mStates.size()) - 1;
		from = std::clamp(from, 0, last);
		into = std::clamp(into, 0, last);
		if (from == into) t = 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);

		const MeshVertex* a = mVertices.data() + static_cast<size_t>(from) * mVertsPerState;
		const MeshVertex* b = mVertices.data() + static_cast<size_t>(into) * mVertsPerState;

		out.resize(mVertsPerState);
		for (uint32_t i = 0; i < mVertsPerState; ++i) {
			// The blend is of the *anchors*, not of the resolved positions -- which is
			// the same answer here and the cheaper one, since both share the rect.
			const float ax = a[i].ax + (b[i].ax - a[i].ax) * t;
			const float ay = a[i].ay + (b[i].ay - a[i].ay) * t;
			const float ox = a[i].ox + (b[i].ox - a[i].ox) * t;
			const float oy = a[i].oy + (b[i].oy - a[i].oy) * t;

			// The two fused multiply-adds this whole design is built around.
			out[i].pos = { rect.x + ax * rect.w + ox, rect.y + ay * rect.h + oy };
			out[i].uv  = { a[i].u + (b[i].u - a[i].u) * t, a[i].v + (b[i].v - a[i].v) * t };

			// The theme's value for that parameter, then the mesh's own default, then the
			// colour the widget would have used. Same rule as everywhere else here: a
			// theme that says nothing gets what was declared, and a mesh that declares
			// nothing gets what the widget was going to draw anyway.
			const auto colourOf = [&](uint8_t param) -> uint32_t {
				if (param == kNoMeshParam) return base;
				if (colours && param < colourCount) return colours[param];
				if (param < mParams.size() && mParams[param].kind == MeshParamKind::Colour) {
					return mParams[param].colour;
				}
				return base;
			};
			out[i].color = mixColour(colourOf(a[i].param), colourOf(b[i].param), t);
		}
	}

	const WidgetMesh* WidgetMeshLibrary::find(const std::string& path) {
		if (path.empty()) return nullptr;
		auto found = mByPath.find(path);
		if (found == mByPath.end()) {
			Entry entry;
			entry.ok = entry.mesh.load(path);
			if (!entry.ok) {
				// Said once. A widget asks for its mesh every frame, and a missing file
				// that reported itself sixty times a second would bury the rest of the
				// log within a minute.
				RDA_LOG_WARNING("mesh '" << path << "' will not load; the widget is drawn "
				                "the way it would have been without it");
			}
			found = mByPath.emplace(path, std::move(entry)).first;
		}
		return found->second.ok ? &found->second.mesh : nullptr;
	}

	void WidgetMeshLibrary::clear() { mByPath.clear(); }

	std::vector<std::string> WidgetMeshLibrary::paths() const {
		std::vector<std::string> out;
		out.reserve(mByPath.size());
		for (const auto& one : mByPath) out.push_back(one.first);
		return out;
	}

	WidgetMeshLibrary& widgetMeshes() {
		// Never destroyed, for the reason the other engine tables are not: an
		// application's globals outlive this translation unit's statics, and one of them
		// releasing a mesh into a freed table is a segfault after the last frame.
		static WidgetMeshLibrary* library = new WidgetMeshLibrary();
		return *library;
	}
}
