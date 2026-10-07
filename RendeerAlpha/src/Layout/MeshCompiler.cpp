#include <Layout/MeshCompiler.h>
#include <GraphicalObjects/MeshAsset.h>
#include "ModuleTransform.h"

#include <vendor/quickjs/quickjs.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace RDA::Layout {

	namespace {
		using Clock = std::chrono::steady_clock;

		double millisSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		std::string exceptionText(JSContext* ctx) {
			JSValue error = JS_GetException(ctx);
			const char* message = JS_ToCString(ctx, error);
			std::string text = message ? message : "<unprintable exception>";
			if (message) JS_FreeCString(ctx, message);
			JS_FreeValue(ctx, error);
			return text;
		}

		// The prelude hands everything back as flat arrays, so reading it is four
		// helpers rather than a walk over nested objects.
		bool numbersAt(JSContext* ctx, JSValueConst object, const char* field,
		               std::vector<double>& out) {
			out.clear();
			JSValue array = JS_GetPropertyStr(ctx, object, field);
			if (!JS_IsArray(array)) { JS_FreeValue(ctx, array); return false; }
			uint32_t count = 0;
			JSValue length = JS_GetPropertyStr(ctx, array, "length");
			JS_ToUint32(ctx, &count, length);
			JS_FreeValue(ctx, length);
			out.reserve(count);
			for (uint32_t i = 0; i < count; ++i) {
				JSValue one = JS_GetPropertyUint32(ctx, array, i);
				double value = 0.0;
				JS_ToFloat64(ctx, &value, one);
				JS_FreeValue(ctx, one);
				out.push_back(value);
			}
			JS_FreeValue(ctx, array);
			return true;
		}

		bool stringsAt(JSContext* ctx, JSValueConst object, const char* field,
		               std::vector<std::string>& out) {
			out.clear();
			JSValue array = JS_GetPropertyStr(ctx, object, field);
			if (!JS_IsArray(array)) { JS_FreeValue(ctx, array); return false; }
			uint32_t count = 0;
			JSValue length = JS_GetPropertyStr(ctx, array, "length");
			JS_ToUint32(ctx, &count, length);
			JS_FreeValue(ctx, length);
			for (uint32_t i = 0; i < count; ++i) {
				JSValue one = JS_GetPropertyUint32(ctx, array, i);
				const char* text = JS_ToCString(ctx, one);
				out.emplace_back(text ? text : "");
				if (text) JS_FreeCString(ctx, text);
				JS_FreeValue(ctx, one);
			}
			JS_FreeValue(ctx, array);
			return true;
		}

		uint32_t numberAt(JSContext* ctx, JSValueConst object, const char* field) {
			JSValue one = JS_GetPropertyStr(ctx, object, field);
			uint32_t value = 0;
			JS_ToUint32(ctx, &value, one);
			JS_FreeValue(ctx, one);
			return value;
		}

		// What the compiler collects before it lays the file out.
		struct Gathered {
			std::vector<std::string> paramNames, paramDocs, paramColours;
			std::vector<double>      paramKinds, paramNumbers;
			std::vector<std::string> stateNames, groupNames;
			std::vector<double>      verts, idx, regionKinds, regionBoxes;
			std::vector<std::string> regionNames;
			uint32_t vertsPerState = 0;
		};

		bool evaluate(const std::string& js, const std::string& name,
		              Gathered& out, std::string& error) {
			JSRuntime* runtime = JS_NewRuntime();
			JSContext* ctx = JS_NewContext(runtime);
			if (!runtime || !ctx) {
				error = "cannot start the JavaScript engine";
				if (ctx) JS_FreeContext(ctx);
				if (runtime) JS_FreeRuntime(runtime);
				return false;
			}

			bool ok = true;
			const auto run = [&](const char* source, const char* what) {
				if (!ok) return;
				JSValue value = JS_Eval(ctx, source, std::strlen(source), what, JS_EVAL_TYPE_GLOBAL);
				if (JS_IsException(value)) { error = exceptionText(ctx); ok = false; }
				JS_FreeValue(ctx, value);
			};
			// Colours first: a mesh's defaults are written with rgb() and fade() like
			// everything else.
			run(colourPrelude(), "<colours>");
			run(meshPrelude(), "<mesh>");

			if (ok) {
				JSValue result = JS_Eval(ctx, js.c_str(), js.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
				if (JS_IsException(result)) { error = exceptionText(ctx); ok = false; }
				JS_FreeValue(ctx, result);
			}

			if (ok) {
				JSValue global = JS_GetGlobalObject(ctx);
				JSValue module = JS_GetPropertyStr(ctx, global, "__module");
				JSValue def = JS_GetPropertyStr(ctx, module, "default");
				if (!JS_IsObject(def)) {
					error = "a mesh file exports default mesh({ … })";
					ok = false;
				} else if (numberAt(ctx, def, "__rdaMesh") != 1) {
					error = "the default export is not a mesh(); did you forget to call it?";
					ok = false;
				} else {
					stringsAt(ctx, def, "paramNames", out.paramNames);
					stringsAt(ctx, def, "paramDocs", out.paramDocs);
					stringsAt(ctx, def, "paramColours", out.paramColours);
					numbersAt(ctx, def, "paramKinds", out.paramKinds);
					numbersAt(ctx, def, "paramNumbers", out.paramNumbers);
					stringsAt(ctx, def, "stateNames", out.stateNames);
					stringsAt(ctx, def, "groupNames", out.groupNames);
					stringsAt(ctx, def, "regionNames", out.regionNames);
					numbersAt(ctx, def, "regionKinds", out.regionKinds);
					numbersAt(ctx, def, "regionBoxes", out.regionBoxes);
					numbersAt(ctx, def, "verts", out.verts);
					numbersAt(ctx, def, "idx", out.idx);
					out.vertsPerState = numberAt(ctx, def, "vertsPerState");
				}
				JS_FreeValue(ctx, def);
				JS_FreeValue(ctx, module);
				JS_FreeValue(ctx, global);
			}

			JS_FreeContext(ctx);
			JS_FreeRuntime(runtime);
			return ok;
		}

		// ---- the file ----------------------------------------------------------------
		constexpr uint32_t kMagic = 0x4D414452u;   // "RDAM"
		constexpr uint32_t kVersion = 1;

		struct Header {
			uint32_t magic, version;
			uint32_t vertexCount, indexCount, stateCount, regionCount, paramCount;
			uint32_t groupCount, vertsPerState, stringBytes;
		};
		struct StateRec  { uint32_t name; uint32_t vertexFirst; };
		struct RegionRec { uint32_t name; uint32_t kind; float box[8]; };
		struct ParamRec  { uint32_t name; uint32_t doc; uint32_t kind; uint32_t colour; float number; };

		// One blob, and every name an offset into it. Identical strings share an entry,
		// which matters less for the bytes than for making a mesh's own names comparable
		// by offset if that is ever wanted.
		struct Strings {
			std::vector<char> blob{ '\0' };   // offset 0 is the empty string
			uint32_t add(const std::string& text) {
				if (text.empty()) return 0;
				const std::string needle = text;
				for (size_t at = 0; at + needle.size() < blob.size(); ++at) {
					if (std::memcmp(blob.data() + at, needle.c_str(), needle.size() + 1) == 0) {
						return static_cast<uint32_t>(at);
					}
				}
				const uint32_t at = static_cast<uint32_t>(blob.size());
				blob.insert(blob.end(), needle.begin(), needle.end());
				blob.push_back('\0');
				return at;
			}
		};

		template <typename T>
		void append(std::vector<uint8_t>& out, const T& value) {
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			out.insert(out.end(), bytes, bytes + sizeof(T));
		}
	}

	std::string meshPathFor(const std::string& sourcePath) {
		std::filesystem::path path(sourcePath);
		path.replace_extension(".rdam");
		return path.string();
	}

	std::string meshNameFor(const std::string& sourcePath) {
		return std::filesystem::path(sourcePath).stem().string();
	}

	MeshCompileResult compileMesh(const std::string& sourcePath, const std::string& esbuildPath) {
		MeshCompileResult result;

		const Clock::time_point transformStart = Clock::now();
		std::string js;
		// No JSX: a mesh is geometry, and an angle bracket in one is a mistake.
		if (!transformModule(sourcePath, esbuildPath, /*withJsx*/ false, js, result.error)) {
			return result;
		}
		result.transformMs = millisSince(transformStart);

		const Clock::time_point evaluateStart = Clock::now();
		Gathered got;
		if (!evaluate(js, sourcePath, got, result.error)) return result;
		result.evaluateMs = millisSince(evaluateStart);

		if (got.stateNames.empty() || got.vertsPerState == 0) {
			result.error = "has no states";
			return result;
		}
		if (got.verts.size() != static_cast<size_t>(got.stateNames.size()) * got.vertsPerState * 8) {
			result.error = "the vertex table does not match the states it claims";
			return result;
		}
		if (got.vertsPerState > 0xFFFFu) {
			result.error = "a mesh may hold 65535 vertices per state";
			return result;
		}

		Strings strings;
		std::vector<uint8_t> verts, idx, states, regions, params, groups;

		for (size_t s = 0; s < got.stateNames.size(); ++s) {
			for (uint32_t i = 0; i < got.vertsPerState; ++i) {
				const double* one = got.verts.data() + (s * got.vertsPerState + i) * 8;
				MeshVertex out;
				out.ax = static_cast<float>(one[0]);
				out.ay = static_cast<float>(one[1]);
				out.ox = static_cast<float>(one[2]);
				out.oy = static_cast<float>(one[3]);
				out.u  = static_cast<float>(one[4]);
				out.v  = static_cast<float>(one[5]);
				out.groups = static_cast<uint32_t>(one[6]);
				out.rect = 0;   // reserved; a mesh sees one rectangle today
				out.param = static_cast<uint8_t>(one[7]);
				out.flags = 0;
				append(verts, out);
			}
			StateRec rec{ strings.add(got.stateNames[s]),
			              static_cast<uint32_t>(s * got.vertsPerState) };
			append(states, rec);
		}

		for (double one : got.idx) append(idx, static_cast<uint16_t>(one));

		for (size_t r = 0; r < got.regionNames.size(); ++r) {
			RegionRec rec{};
			rec.name = strings.add(got.regionNames[r]);
			rec.kind = r < got.regionKinds.size() ? static_cast<uint32_t>(got.regionKinds[r]) : 0u;
			for (int k = 0; k < 8; ++k) {
				const size_t at = r * 8 + k;
				rec.box[k] = at < got.regionBoxes.size() ? static_cast<float>(got.regionBoxes[at]) : 0.0f;
			}
			append(regions, rec);
		}

		for (size_t p = 0; p < got.paramNames.size(); ++p) {
			ParamRec rec{};
			rec.name = strings.add(got.paramNames[p]);
			rec.doc = strings.add(p < got.paramDocs.size() ? got.paramDocs[p] : std::string());
			rec.kind = p < got.paramKinds.size() ? static_cast<uint32_t>(got.paramKinds[p]) : 0u;
			rec.number = p < got.paramNumbers.size() ? static_cast<float>(got.paramNumbers[p]) : 0.0f;
			rec.colour = 0;
			if (rec.kind == 0 && p < got.paramColours.size()) {
				// Parsed here rather than at load: a default that is not a colour is a
				// build failure, and the runtime should never meet one.
				if (!parseColor(got.paramColours[p].c_str(), rec.colour)) {
					result.error = "the default for `" + got.paramNames[p] + "` is not a colour: " +
					               got.paramColours[p];
					return result;
				}
			}
			append(params, rec);

			MeshParamDecl decl;
			decl.mesh = meshNameFor(sourcePath);
			decl.name = got.paramNames[p];
			decl.doc = p < got.paramDocs.size() ? got.paramDocs[p] : std::string();
			decl.isColour = (rec.kind == 0);
			result.params.push_back(std::move(decl));
		}

		for (const std::string& one : got.groupNames) {
			append(groups, strings.add(one));
		}

		Header header{};
		header.magic = kMagic;
		header.version = kVersion;
		header.vertexCount = static_cast<uint32_t>(got.stateNames.size()) * got.vertsPerState;
		header.indexCount = static_cast<uint32_t>(got.idx.size());
		header.stateCount = static_cast<uint32_t>(got.stateNames.size());
		header.regionCount = static_cast<uint32_t>(got.regionNames.size());
		header.paramCount = static_cast<uint32_t>(got.paramNames.size());
		header.groupCount = static_cast<uint32_t>(got.groupNames.size());
		header.vertsPerState = got.vertsPerState;
		header.stringBytes = static_cast<uint32_t>(strings.blob.size());

		append(result.bytes, header);
		result.bytes.insert(result.bytes.end(), verts.begin(), verts.end());
		result.bytes.insert(result.bytes.end(), idx.begin(), idx.end());
		result.bytes.insert(result.bytes.end(), states.begin(), states.end());
		result.bytes.insert(result.bytes.end(), regions.begin(), regions.end());
		result.bytes.insert(result.bytes.end(), params.begin(), params.end());
		result.bytes.insert(result.bytes.end(), groups.begin(), groups.end());
		result.bytes.insert(result.bytes.end(), strings.blob.begin(), strings.blob.end());

		// Read it back, with the reader the engine itself will use.
		//
		// This format has two halves in two files: the records written above, and the ones
		// GraphicalObjects/MeshAsset.cpp reads them into. They are separate structs over
		// the same bytes, so the failure they can have together is a .rdam that compiles
		// without complaint and then will not load -- which surfaces much later as a
		// widget quietly drawn without its mesh, one warning deep in a log. One parse of a
		// few kilobytes here makes that impossible to ship.
		WidgetMesh readBack;
		std::string readError;
		if (!readBack.parse(result.bytes.data(), result.bytes.size(), readError)) {
			result.error = "wrote a mesh this engine cannot read back: " + readError +
			               "\n     That is a bug in rda rather than in " + sourcePath + ".";
			return result;
		}

		result.stateCount = got.stateNames.size();
		result.vertexCount = got.vertsPerState;
		result.triangleCount = got.idx.size() / 3;
		result.ok = true;
		return result;
	}

	MeshCompileResult compileMeshToFile(const std::string& sourcePath,
	                                    const std::string& outputPath,
	                                    const std::string& esbuildPath) {
		MeshCompileResult result = compileMesh(sourcePath, esbuildPath);
		if (!result.ok) return result;

		std::error_code ec;
		const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty()) std::filesystem::create_directories(parent, ec);

		std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
		if (!out || !out.write(reinterpret_cast<const char*>(result.bytes.data()),
		                       static_cast<std::streamsize>(result.bytes.size()))) {
			result.ok = false;
			result.error = "cannot write " + outputPath;
		}
		return result;
	}
}
