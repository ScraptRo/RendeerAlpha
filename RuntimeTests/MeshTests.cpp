#include "TestHarness.h"
#include <GraphicalObjects/MeshAsset.h>
#include <GraphicalObjects/GuiTheme.h>
#include <Layout/StyleWatch.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

// A compiled mesh, read back.
//
// Two halves of one format live in this engine: the writer in Layout/MeshCompiler.cpp and
// the reader in GraphicalObjects/MeshAsset.cpp. They are separate structs over the same
// bytes, which is a fragility worth testing from both sides -- `rda mesh` now parses what
// it has just written, so the two cannot disagree in a file anybody ships, and this says
// what those bytes are supposed to *be*. The layout is written out here by hand on
// purpose: a test that built its input with the writer's own code would agree with the
// writer however wrong both of them were.
//
// None of this needs a device. A mesh resolves to vertices against a rectangle, which is
// arithmetic, and the refusals are a reader looking at a file it does not like.
using namespace RDA;

namespace {

	// The records, as they sit on disk. Every member is four bytes wide, so there is no
	// padding to get wrong.
	struct StateRec  { uint32_t name; uint32_t vertexFirst; };
	struct RegionRec { uint32_t name; uint32_t kind; float box[8]; };
	struct ParamRec  { uint32_t name; uint32_t doc; uint32_t kind; uint32_t colour; float number; };

	struct Strings {
		std::string blob;
		uint32_t add(const std::string& text) {
			const uint32_t at = static_cast<uint32_t>(blob.size());
			blob += text;
			blob += '\0';
			return at;
		}
	};

	// What a mesh is made of, before it is bytes. Each field can be set to something the
	// reader should refuse.
	struct Spec {
		std::vector<std::string> states;
		std::vector<MeshVertex>  verts;      // states.size() * vertsPerState of them
		std::vector<uint16_t>    indices;
		std::vector<MeshRegion>  regions;
		std::vector<MeshParam>   params;
		std::vector<std::string> groups;
		uint32_t vertsPerState = 0;          // 0 means "work it out from the vertices"

		// Overrides, for the cases that are about a file being wrong.
		uint32_t magic = 0x4D414452u;        // "RDAM"
		uint32_t version = 1;
		bool     terminateStrings = true;
	};

	template <typename T>
	void put(std::vector<uint8_t>& out, const T& value) {
		const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
		out.insert(out.end(), bytes, bytes + sizeof(T));
	}

	std::vector<uint8_t> bytesOf(const Spec& spec) {
		Strings strings;
		std::vector<uint8_t> states, regions, params, groups, indices, verts;

		for (const MeshVertex& one : spec.verts) put(verts, one);
		for (uint16_t one : spec.indices) put(indices, one);
		for (const std::string& name : spec.states) {
			put(states, StateRec{ strings.add(name), 0 });
		}
		for (const MeshRegion& one : spec.regions) {
			RegionRec rec{ strings.add(one.name), static_cast<uint32_t>(one.kind), {} };
			for (int i = 0; i < 8; ++i) rec.box[i] = one.box[i];
			put(regions, rec);
		}
		for (const MeshParam& one : spec.params) {
			put(params, ParamRec{ strings.add(one.name), strings.add(one.doc),
			                      static_cast<uint32_t>(one.kind), one.colour, one.number });
		}
		for (const std::string& one : spec.groups) put(groups, strings.add(one));

		const uint32_t perState = spec.vertsPerState != 0
			? spec.vertsPerState
			: (spec.states.empty() ? 0u
			                       : static_cast<uint32_t>(spec.verts.size() / spec.states.size()));

		if (!spec.terminateStrings && !strings.blob.empty()) strings.blob.back() = 'x';

		std::vector<uint8_t> out;
		put(out, spec.magic);
		put(out, spec.version);
		put(out, static_cast<uint32_t>(spec.verts.size()));
		put(out, static_cast<uint32_t>(spec.indices.size()));
		put(out, static_cast<uint32_t>(spec.states.size()));
		put(out, static_cast<uint32_t>(spec.regions.size()));
		put(out, static_cast<uint32_t>(spec.params.size()));
		put(out, static_cast<uint32_t>(spec.groups.size()));
		put(out, perState);
		put(out, static_cast<uint32_t>(strings.blob.size()));

		out.insert(out.end(), verts.begin(), verts.end());
		out.insert(out.end(), indices.begin(), indices.end());
		out.insert(out.end(), states.begin(), states.end());
		out.insert(out.end(), regions.begin(), regions.end());
		out.insert(out.end(), params.begin(), params.end());
		out.insert(out.end(), groups.begin(), groups.end());
		out.insert(out.end(), strings.blob.begin(), strings.blob.end());
		return out;
	}

	MeshVertex vert(float ax, float ay, float ox, float oy, uint8_t param = kNoMeshParam) {
		MeshVertex v;
		v.ax = ax; v.ay = ay; v.ox = ox; v.oy = oy;
		v.param = param;
		return v;
	}

	// The shape every case below starts from: a quad at rest, and the same quad inset four
	// pixels on every side when the pointer is on it -- the smallest mesh that is still a
	// mesh, since two states that share a topology is the whole idea.
	Spec quad() {
		Spec spec;
		spec.states = { "normal", "hover" };
		spec.verts = {
			vert(0, 0, 0, 0), vert(1, 0, 0, 0), vert(1, 1, 0, 0), vert(0, 1, 0, 0),
			vert(0, 0,  4,  4, 0), vert(1, 0, -4,  4, 0),
			vert(1, 1, -4, -4, 0), vert(0, 1,  4, -4, 0),
		};
		spec.indices = { 0, 1, 2, 0, 2, 3 };

		MeshParam seam;
		seam.name = "seam";
		seam.doc = "where the tab meets the panel";
		seam.kind = MeshParamKind::Colour;
		seam.colour = 0xFF302010u;
		spec.params = { seam };

		MeshRegion body;
		body.name = "body";
		body.kind = MeshRegionKind::Hit;
		MeshRegion label;
		label.name = "label";
		label.kind = MeshRegionKind::Text;
		const float inset[8] = { 0, 10, 0, 2, 1, -10, 1, -2 };
		for (int i = 0; i < 8; ++i) label.box[i] = inset[i];
		spec.regions = { body, label };

		spec.groups = { "edge" };
		return spec;
	}

	bool parses(const Spec& spec, WidgetMesh& mesh, std::string& error) {
		const std::vector<uint8_t> bytes = bytesOf(spec);
		return mesh.parse(bytes.data(), bytes.size(), error);
	}

	const Rect kRect{ 100.0f, 200.0f, 50.0f, 20.0f };
}

// ---- what a reader accepts ------------------------------------------------------------

TEST(a_mesh_reads_back_everything_it_was_given) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));
	CHECK_STR(error, "");
	CHECK(mesh.valid());

	CHECK_EQ(mesh.stateNames().size(), size_t(2));
	CHECK_STR(mesh.stateNames()[0], "normal");
	CHECK_STR(mesh.stateNames()[1], "hover");
	CHECK_EQ(mesh.vertsPerState(), 4u);
	CHECK_EQ(mesh.indices().size(), size_t(6));
	CHECK_EQ(mesh.params().size(), size_t(1));
	CHECK_STR(mesh.params()[0].name, "seam");
	CHECK_STR(mesh.params()[0].doc, "where the tab meets the panel");
	CHECK_EQ(mesh.params()[0].colour, 0xFF302010u);
	CHECK_EQ(mesh.regions().size(), size_t(2));
	CHECK_STR(mesh.regions()[0].name, "body");
	CHECK_EQ(mesh.groupNames().size(), size_t(1));
	CHECK_STR(mesh.groupNames()[0], "edge");
}

TEST(a_name_that_is_not_there_is_minus_one_rather_than_a_guess) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	CHECK_EQ(mesh.stateIndex("hover"), 1);
	CHECK_EQ(mesh.stateIndex("pressed"), -1);
	CHECK_EQ(mesh.paramIndex("seam"), 0);
	CHECK_EQ(mesh.paramIndex("seem"), -1);   // the misspelling the theme schema refuses
	CHECK_EQ(mesh.regionIndex("label"), 1);
	CHECK_EQ(mesh.regionIndex("title"), -1);
}

// ---- and what it refuses --------------------------------------------------------------
//
// A .rdam arrives from a build the engine did not run. Every one of these is a file that
// could plausibly be on disk, and none of them may be read as if it were fine.

TEST(something_that_is_not_a_mesh_is_refused_by_its_magic) {
	Spec spec = quad();
	spec.magic = 0x42414452u;    // "RDAB" -- a layout, handed to the wrong loader
	WidgetMesh mesh;
	std::string error;
	CHECK(!parses(spec, mesh, error));
	CHECK(error.find("magic") != std::string::npos);
	CHECK(!mesh.valid());
}

TEST(a_mesh_from_another_version_of_the_tool_names_both_versions) {
	Spec spec = quad();
	spec.version = 2;
	WidgetMesh mesh;
	std::string error;
	CHECK(!parses(spec, mesh, error));
	// Not just "wrong version": which one it is and which one this engine reads, because
	// the fix is to rebuild with a matching rda and that is only obvious if it says so.
	CHECK(error.find("2") != std::string::npos);
	CHECK(error.find("1") != std::string::npos);
}

TEST(a_truncated_mesh_is_refused_rather_than_read_off_the_end) {
	const std::vector<uint8_t> whole = bytesOf(quad());
	WidgetMesh mesh;
	// Every prefix of a valid file, including the ones that stop inside the header.
	for (size_t size = 0; size < whole.size(); size += 7) {
		std::string error;
		CHECK(!mesh.parse(whole.data(), size, error));
		CHECK(!error.empty());
	}
	std::string error;
	CHECK(mesh.parse(whole.data(), whole.size(), error));  // and the whole thing still reads
}

TEST(states_that_do_not_share_a_vertex_count_are_refused) {
	// The one invariant the whole design rests on: states are the same vertices in
	// different places, which is what makes them blendable. A file saying otherwise
	// cannot be drawn, and blending it would read past the end of a state.
	// Eight vertices and two states, with the header saying three each. Said the other way
	// round -- a vertex count the file does not hold -- every offset after it shifts and the
	// reader refuses it as truncated, which is a different thing being caught.
	Spec spec = quad();
	spec.vertsPerState = 3;
	WidgetMesh mesh;
	std::string error;
	CHECK(!parses(spec, mesh, error));
	CHECK(error.find("blendable") != std::string::npos);
}

TEST(a_mesh_with_no_states_is_refused) {
	Spec spec;
	spec.vertsPerState = 0;
	WidgetMesh mesh;
	std::string error;
	CHECK(!parses(spec, mesh, error));
	CHECK(error.find("states") != std::string::npos);
}

TEST(an_index_past_the_end_of_a_state_is_refused) {
	Spec spec = quad();
	spec.indices = { 0, 1, 2, 0, 2, 4 };   // four vertices per state, so 4 is one too far
	WidgetMesh mesh;
	std::string error;
	CHECK(!parses(spec, mesh, error));
	CHECK(error.find("index") != std::string::npos);
}

TEST(a_string_table_that_does_not_end_is_refused) {
	// Names are offsets into one blob and are read as C strings. If the blob does not end
	// in a NUL, the last name runs off the end of the file -- so the terminator is
	// checked rather than trusted.
	Spec spec = quad();
	spec.terminateStrings = false;
	WidgetMesh mesh;
	std::string error;
	CHECK(!parses(spec, mesh, error));
	CHECK(error.find("string table") != std::string::npos);
}

// ---- anchors --------------------------------------------------------------------------

TEST(a_vertex_pinned_in_pixels_keeps_its_offset_at_any_width) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	std::vector<GuiVertex> narrow, wide;
	mesh.resolve(1, 1, 0.0f, Rect{ 0, 0, 50, 20 }, nullptr, 0, 0, narrow);
	mesh.resolve(1, 1, 0.0f, Rect{ 0, 0, 500, 20 }, nullptr, 0, 0, wide);

	// The hover state is the quad inset four pixels. Four pixels at either width.
	CHECK_EQ(narrow[0].pos.x, 4.0f);
	CHECK_EQ(wide[0].pos.x, 4.0f);
	// And the right edge follows the rect, because that corner's anchor is 1.
	CHECK_EQ(narrow[1].pos.x, 46.0f);
	CHECK_EQ(wide[1].pos.x, 496.0f);
}

TEST(a_vertex_resolves_against_the_rect_it_is_given) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	std::vector<GuiVertex> out;
	mesh.resolve(0, 0, 0.0f, kRect, nullptr, 0, 0, out);
	CHECK_EQ(out.size(), size_t(4));
	CHECK_EQ(out[0].pos.x, 100.0f);   // x + 0 * w
	CHECK_EQ(out[0].pos.y, 200.0f);
	CHECK_EQ(out[2].pos.x, 150.0f);   // x + 1 * w
	CHECK_EQ(out[2].pos.y, 220.0f);
}

TEST(a_region_is_anchored_the_way_a_vertex_is) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	const Rect body = mesh.regionRect(0, kRect);   // {} -- the whole rect
	CHECK_EQ(body.x, 100.0f);
	CHECK_EQ(body.w, 50.0f);

	const Rect label = mesh.regionRect(1, kRect);  // ten pixels in, two down
	CHECK_EQ(label.x, 110.0f);
	CHECK_EQ(label.y, 202.0f);
	CHECK_EQ(label.w, 30.0f);        // 50 - 10 - 10
	CHECK_EQ(label.h, 16.0f);        // 20 - 2 - 2

	// A region nobody declared is the widget's own rect rather than nothing: a text
	// region is optional, and a string with no region still has somewhere to go.
	const Rect missing = mesh.regionRect(9, kRect);
	CHECK_EQ(missing.w, kRect.w);
}

TEST(a_region_narrower_than_its_own_insets_is_empty_rather_than_inside_out) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));
	// Twenty pixels of inset in a fifteen pixel box. A negative width would draw a
	// rectangle the other way round, and clip everything in it.
	const Rect label = mesh.regionRect(1, Rect{ 0, 0, 15, 2 });
	CHECK_EQ(label.w, 0.0f);
	CHECK_EQ(label.h, 0.0f);
}

// ---- blending -------------------------------------------------------------------------

TEST(a_state_blend_lands_halfway_at_half) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	std::vector<GuiVertex> out;
	mesh.resolve(0, 1, 0.5f, kRect, nullptr, 0, 0, out);
	CHECK_EQ(out[0].pos.x, 102.0f);   // between 100 and 104
	CHECK_EQ(out[0].pos.y, 202.0f);
	CHECK_EQ(out[2].pos.x, 148.0f);   // between 150 and 146
	CHECK_EQ(out[2].pos.y, 218.0f);
}

TEST(the_ends_of_a_blend_are_exactly_the_states_themselves) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	std::vector<GuiVertex> at0, at1, resting, hovered;
	mesh.resolve(0, 1, 0.0f, kRect, nullptr, 0, 0, at0);
	mesh.resolve(0, 1, 1.0f, kRect, nullptr, 0, 0, at1);
	mesh.resolve(0, 0, 0.0f, kRect, nullptr, 0, 0, resting);
	mesh.resolve(1, 1, 0.0f, kRect, nullptr, 0, 0, hovered);

	for (size_t i = 0; i < at0.size(); ++i) {
		CHECK_EQ(at0[i].pos.x, resting[i].pos.x);
		CHECK_EQ(at0[i].pos.y, resting[i].pos.y);
		CHECK_EQ(at1[i].pos.x, hovered[i].pos.x);
		CHECK_EQ(at1[i].pos.y, hovered[i].pos.y);
	}
}

TEST(a_blend_past_either_end_is_held_at_the_end) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	std::vector<GuiVertex> under, over, at0, at1;
	mesh.resolve(0, 1, -3.0f, kRect, nullptr, 0, 0, under);
	mesh.resolve(0, 1, 9.0f, kRect, nullptr, 0, 0, over);
	mesh.resolve(0, 1, 0.0f, kRect, nullptr, 0, 0, at0);
	mesh.resolve(0, 1, 1.0f, kRect, nullptr, 0, 0, at1);
	CHECK_EQ(under[0].pos.x, at0[0].pos.x);
	CHECK_EQ(over[0].pos.x, at1[0].pos.x);
}

TEST(a_state_index_nobody_has_is_clamped_rather_than_read) {
	// The ladder is as long as the mesh says it is, and a widget asking for the third rung
	// of a two-state mesh is ordinary -- `selected` asks for the last state, whatever the
	// mesh turned out to have.
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	std::vector<GuiVertex> far, last;
	mesh.resolve(7, 7, 0.0f, kRect, nullptr, 0, 0, far);
	mesh.resolve(1, 1, 0.0f, kRect, nullptr, 0, 0, last);
	CHECK_EQ(far.size(), size_t(4));
	CHECK_EQ(far[0].pos.x, last[0].pos.x);

	std::vector<GuiVertex> below, first;
	mesh.resolve(-4, -4, 0.0f, kRect, nullptr, 0, 0, below);
	mesh.resolve(0, 0, 0.0f, kRect, nullptr, 0, 0, first);
	CHECK_EQ(below[0].pos.x, first[0].pos.x);
}

// ---- colour ---------------------------------------------------------------------------

TEST(a_vertex_with_no_parameter_takes_the_colour_the_widget_would_have_used) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	const uint32_t themed[1] = { 0xFFFFFFFFu };
	std::vector<GuiVertex> out;
	mesh.resolve(0, 0, 0.0f, kRect, themed, 1, 0xFF112233u, out);
	CHECK_EQ(out[0].color, 0xFF112233u);   // the resting state declares no parameter
}

TEST(a_vertex_with_a_parameter_takes_what_the_theme_set) {
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	const uint32_t themed[1] = { 0xFF884422u };
	std::vector<GuiVertex> out;
	mesh.resolve(1, 1, 0.0f, kRect, themed, 1, 0xFF112233u, out);
	CHECK_EQ(out[0].color, 0xFF884422u);
}

TEST(a_theme_that_said_nothing_gets_what_the_mesh_declared) {
	// Every parameter carries a default, so a mesh drawn by a theme that never mentions it
	// still draws -- in the colour its author chose.
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	std::vector<GuiVertex> out;
	mesh.resolve(1, 1, 0.0f, kRect, nullptr, 0, 0xFF112233u, out);
	CHECK_EQ(out[0].color, 0xFF302010u);   // `seam`'s declared value
}

TEST(a_parameter_index_past_the_colours_falls_back_instead_of_reading_on) {
	// A mesh built against one theme and drawn by another: the vertex names parameter
	// five, the theme resolved one colour. Reading colours[5] there would be a stray
	// read of whatever follows a std::vector's buffer.
	Spec spec = quad();
	spec.verts[4] = vert(0, 0, 4, 4, 5);
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(spec, mesh, error));

	const uint32_t themed[1] = { 0xFF884422u };
	std::vector<GuiVertex> out;
	mesh.resolve(1, 1, 0.0f, kRect, themed, 1, 0xFF112233u, out);
	CHECK_EQ(out[0].color, 0xFF112233u);   // base, which is always a colour
}

TEST(a_colour_blends_per_channel_in_the_bytes_it_was_written_in) {
	// The shader decodes sRGB, so a lerp here is a lerp of the numbers a person wrote in
	// the theme -- half way between two colours is the colour half way between them as
	// written, which is the same rule every other blend in the engine follows.
	WidgetMesh mesh;
	std::string error;
	CHECK(parses(quad(), mesh, error));

	const uint32_t themed[1] = { 0xFFFFFFFFu };
	std::vector<GuiVertex> out;
	mesh.resolve(0, 1, 0.5f, kRect, themed, 1, 0x00000000u, out);
	CHECK_EQ(out[0].color, 0x80808080u);
}

// ---- the library in front of it --------------------------------------------------------

TEST(a_mesh_that_will_not_load_is_null_rather_than_a_blank_shape) {
	WidgetMeshLibrary library;
	CHECK(library.find("") == nullptr);
	CHECK(library.find("res/mesh/nothing-is-here.rdam") == nullptr);
	// Asked twice, because the answer is cached and a cached failure is still a failure.
	CHECK(library.find("res/mesh/nothing-is-here.rdam") == nullptr);
}

namespace {
	// A file somewhere a test may write. Named per case so two of them cannot collide.
	std::filesystem::path scratch(const char* name) {
		return std::filesystem::temp_directory_path() / name;
	}

	void writeMesh(const std::filesystem::path& path, const Spec& spec) {
		const std::vector<uint8_t> bytes = bytesOf(spec);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()),
		          static_cast<std::streamsize>(bytes.size()));
	}

	// Past the watcher's own throttle. It is deliberately not injectable: the interval is
	// part of what the thing does, and a test that set it to zero would be testing a seam
	// rather than the behaviour. A sixth of a second, three times, is a tenth of what the
	// binding tests cost.
	void waitForTheWatcher() {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

TEST(a_mesh_is_read_once_and_re_read_when_the_library_is_cleared) {
	// This is what hot reload stands on. A widget asks for its mesh every frame, so the
	// file is read once and kept; clearing is how an edited mesh gets a second look.
	const std::filesystem::path path =
		std::filesystem::temp_directory_path() / "rda-test-mesh.rdam";

	const auto write = [&](const Spec& spec) {
		const std::vector<uint8_t> bytes = bytesOf(spec);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()),
		          static_cast<std::streamsize>(bytes.size()));
	};

	write(quad());
	WidgetMeshLibrary library;
	const WidgetMesh* first = library.find(path.string());
	CHECK(first != nullptr);
	if (first) CHECK_EQ(first->stateNames().size(), size_t(2));

	// A third state, which is what adding a `pressed` rung to the ladder looks like.
	Spec grown = quad();
	grown.states.push_back("pressed");
	for (int i = 0; i < 4; ++i) grown.verts.push_back(vert(0, 0, 0, 0, 0));
	write(grown);

	const WidgetMesh* again = library.find(path.string());
	CHECK(again != nullptr);
	if (again) CHECK_EQ(again->stateNames().size(), size_t(2));  // still the one it read

	library.clear();
	const WidgetMesh* reread = library.find(path.string());
	CHECK(reread != nullptr);
	if (reread) CHECK_EQ(reread->stateNames().size(), size_t(3));

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

// ---- and watching it ------------------------------------------------------------------
//
// Layout/StyleWatch.h is the rest of the loop: a theme and the meshes it names, re-read
// when they change, so authoring a shape does not mean restarting the application between
// every two pixels. The theme half of it needs a compiled .rdth and therefore the theme
// compiler, which this binary deliberately does not have -- what is covered here is the
// mesh half and the rules that hold for both.

TEST(an_edited_mesh_is_re_read_without_the_application_restarting) {
	const std::filesystem::path path = scratch("rda-test-watch.rdam");
	writeMesh(path, quad());

	// What a frame does the first time something draws with this variant.
	widgetMeshes().clear();
	const WidgetMesh* before = widgetMeshes().find(path.string());
	CHECK(before != nullptr);
	if (before) CHECK_EQ(before->stateNames().size(), size_t(2));

	Theme theme;
	Layout::StyleWatch watch;
	watch.open(theme, "");   // no theme file: only the meshes anything has asked for

	// It does not look again straight away. Checking a timestamp is cheap and checking it
	// sixty times a second is still pointless.
	CHECK(!watch.reloadIfChanged());

	waitForTheWatcher();
	Spec grown = quad();
	grown.states.push_back("pressed");       // a third rung on the ladder
	for (int i = 0; i < 4; ++i) grown.verts.push_back(vert(0, 0, 0, 0, 0));
	writeMesh(path, grown);

	CHECK(watch.reloadIfChanged());
	const WidgetMesh* after = widgetMeshes().find(path.string());
	CHECK(after != nullptr);
	if (after) CHECK_EQ(after->stateNames().size(), size_t(3));

	// And nothing moved since, so it says so.
	waitForTheWatcher();
	CHECK(!watch.reloadIfChanged());

	widgetMeshes().clear();
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(a_reloaded_mesh_makes_every_style_resolve_its_parameters_again) {
	// The trap this is here for. A style works out which colour belongs to which of a
	// mesh's parameters once, and remembers the mesh it did that for -- by address, because
	// that is the cheap way to ask "is this still the mesh I resolved against". Clearing
	// the library destroys that mesh, and the replacement can be allocated at the very same
	// address: the style would read as already resolved and keep the colours it worked out
	// for the shape that is gone.
	const std::filesystem::path path = scratch("rda-test-resolve.rdam");
	writeMesh(path, quad());

	widgetMeshes().clear();
	const WidgetMesh* mesh = widgetMeshes().find(path.string());
	CHECK(mesh != nullptr);

	Theme theme;
	ButtonStyle style;
	style.mesh = path.string();
	theme.defineButton(Variant(std::string_view("tab")), style);

	// What Gui::button does on the frame it first draws one.
	const ButtonStyle& resolved = theme.button(Variant(std::string_view("tab")));
	resolved.meshColours.assign(1, 0xFFABCDEFu);
	resolved.meshResolvedFor = mesh;
	const uint64_t revisionBefore = theme.revision();

	Layout::StyleWatch watch;
	watch.open(theme, "");
	waitForTheWatcher();
	writeMesh(path, quad());         // same shape, new timestamp: a save with no edit
	CHECK(watch.reloadIfChanged());

	const ButtonStyle& again = theme.button(Variant(std::string_view("tab")));
	CHECK(again.meshResolvedFor == nullptr);
	CHECK(again.meshColours.empty());
	// And the look is marked as having moved, or an on-demand window would have no reason
	// to draw the new shape.
	CHECK(theme.revision() != revisionBefore);

	widgetMeshes().clear();
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(a_mesh_replaced_with_something_unreadable_falls_back_to_the_built_in_shape) {
	// Not the compile path -- a source that does not compile is never written, so the shape
	// on screen is the last one that did. This is the other way in: something else wrote
	// the .rdam, and wrote rubbish. The widget is then drawn the way it would have been
	// without a mesh at all, which is the documented answer and must not be a crash.
	const std::filesystem::path path = scratch("rda-test-broken.rdam");
	writeMesh(path, quad());

	widgetMeshes().clear();
	CHECK(widgetMeshes().find(path.string()) != nullptr);

	Theme theme;
	Layout::StyleWatch watch;
	watch.open(theme, "");
	waitForTheWatcher();

	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << "this is not a mesh";
	}
	CHECK(watch.reloadIfChanged());
	CHECK(widgetMeshes().find(path.string()) == nullptr);

	widgetMeshes().clear();
	std::error_code ec;
	std::filesystem::remove(path, ec);
}
