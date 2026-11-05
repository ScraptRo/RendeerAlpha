#include "TestHarness.h"
#include <Layout/SourceMap.h>

#include <filesystem>
#include <string>

// An exception thrown while a layout is built, moved from the line of esbuild's bundle it
// was thrown on to the line it was written on. The maps here are esbuild's own, captured
// from these two files, so what is checked is the reading of a real map rather than of one
// written to agree with the reader:
//
//   res/layouts/imp.tsx                          res/layouts/helpers.ts
//   1  import { boom } from "./helpers";         1  export function boom(o: any) {
//   2  type T = { a: number };                   2    return o.z;
//   3  export default function M() {             3  }
//   4    const x = null as any;
//   5    return <label id="l" text={boom(x)} />;
//   6  }
//
// and the bundle, which is evaluated with one line in front of it (wrapBundle's):
//
//   1  // res/layouts/helpers.ts
//   2  function boom(o) {
//   3    return o.z;
//   4  }
//   5
//   6  // res/layouts/imp.tsx
//   7  function M() {
//   8    const x = null;
//   9    return /* @__PURE__ */ h("label", { id: "l", text: boom(x) });
//   10 }
//   11 export {
//   12   M as default
//   13 };
//
// The rda_layout_error_line and rda_state_error_line tests are the same thing end to end,
// through esbuild and QuickJS.
using namespace RDA::Layout;

namespace {

	const char* kTwoFiles =
		"{\n"
		"  \"version\": 3,\n"
		"  \"sources\": [\"res/layouts/helpers.ts\", \"res/layouts/imp.tsx\"],\n"
		"  \"mappings\": \";AAAO,SAAS,KAAK,GAAQ;AAC3B,SAAO,EAAE;AACX;;;ACAe,SAAR,IAAqB;AAC1B,QAAM,IAAI;"
		"AACV,SAAO,kBAAC,WAAM,IAAG,KAAI,MAAM,KAAK,CAAC,GAAG;AACtC;\",\n"
		"  \"names\": []\n"
		"}\n";

	// What QuickJS says when boom(null) throws, word for word: the bundle's lines, plus
	// wrapBundle's one, and the columns of the `o` in o.z and the `x` in boom(x).
	const char* kThrown =
		"TypeError: cannot read property 'z' of null\n"
		"    at boom (res/layouts/imp.tsx:4:10)\n"
		"    at M (res/layouts/imp.tsx:10:59)";
}

TEST(a_frame_is_moved_to_the_line_it_was_written_on) {
	const SourceMap map(kTwoFiles, "res/layouts/imp.tsx", 1);
	CHECK(!map.empty());

	// The call in M: bundle line 9, the `x` of boom(x) -- which is line 5 of the file, and
	// the same `x`, at column 35.
	SourceMap::Origin origin;
	CHECK(map.find(10, 59, origin));
	CHECK_STR(origin.file, "res/layouts/imp.tsx");
	CHECK_EQ(origin.line, 5u);
	CHECK_EQ(origin.column, 35u);

	// The `b` of boom, at 54, is column 30 there. A column between two mappings belongs to
	// the one before it: the `(` after boom, at 58, is still the `b`'s.
	CHECK(map.find(10, 54, origin));
	CHECK_EQ(origin.column, 30u);
	CHECK(map.find(10, 58, origin));
	CHECK_EQ(origin.line, 5u);
	CHECK_EQ(origin.column, 30u);
}

TEST(a_frame_in_an_imported_file_names_that_file) {
	const SourceMap map(kTwoFiles, "res/layouts/imp.tsx", 1);
	CHECK_STR(map.rewrite(kThrown),
	          "TypeError: cannot read property 'z' of null\n"
	          "    at boom (res/layouts/helpers.ts:2:10)\n"
	          "    at M (res/layouts/imp.tsx:5:35)");
}

TEST(a_line_no_source_accounts_for_is_the_engines) {
	const SourceMap map(kTwoFiles, "res/layouts/imp.tsx", 1);
	SourceMap::Origin origin;
	// wrapBundle's own line in front, a comment esbuild wrote, and the export clause
	// wrapBundle takes off -- whose lines the wrapper's closing lines then fall on.
	CHECK(!map.find(1, 1, origin));
	CHECK(!map.find(2, 1, origin));
	CHECK(!map.find(12, 3, origin));
	CHECK(!map.find(40, 1, origin));
	CHECK_STR(map.rewrite("    at <eval> (res/layouts/imp.tsx:15:4)"),
	          "    at <eval> (the engine's own code)");
}

TEST(only_the_bundles_own_frames_are_rewritten) {
	const SourceMap map(kTwoFiles, "res/layouts/imp.tsx", 1);
	// The helpers are evaluated as a script of their own and keep their lines. A longer
	// path that merely ends in the entry's name is some other file.
	const std::string text =
		"Error: signal() needs a name\n"
		"    at signal (<rda helpers>:266:11)\n"
		"    at M (res/layouts/imp.tsx:10:54)\n"
		"    at f (old/res/layouts/imp.tsx:10:54)";
	CHECK_STR(map.rewrite(text),
	          "Error: signal() needs a name\n"
	          "    at signal (<rda helpers>:266:11)\n"
	          "    at M (res/layouts/imp.tsx:5:30)\n"
	          "    at f (old/res/layouts/imp.tsx:10:54)");

	// The IIFE transformModule writes is evaluated as it is, with nothing in front: the
	// same frame of the same map is then one line further down the file.
	const SourceMap unwrapped(kTwoFiles, "res/layouts/imp.tsx");
	CHECK_STR(unwrapped.rewrite("    at boom (res/layouts/imp.tsx:3:10)"),
	          "    at boom (res/layouts/helpers.ts:2:10)");
}

TEST(an_exception_the_engine_caught_keeps_only_the_frames_in_a_file) {
	// A computed value that failed while state.ts was built: the message is the engine's
	// own, and of the stack only what is in the files is worth showing -- the evaluator's
	// frames around it, and the wrapper's, are dropped rather than said.
	const SourceMap map(kTwoFiles, "res/layouts/imp.tsx", 1);
	const std::string stack =
		"Error: too many\n"
		"    at <anonymous> (<rda helpers>:334:15)\n"
		"    at compute (res/layouts/imp.tsx:4:10)\n"
		"    at valueOf (<rda helpers>:317:30)\n"
		"    at <eval> (res/layouts/imp.tsx:40:1)\n";
	CHECK_STR(map.ownFrames(stack), "    at compute (res/layouts/helpers.ts:2:10)");
	CHECK_STR(map.ownFrames("    at valueOf (<rda helpers>:317:30)"), "");
	CHECK_STR(SourceMap().ownFrames(stack), "");
}

TEST(a_map_that_will_not_read_changes_nothing) {
	for (const char* broken : { "", "not json", "{\"sources\": [\"a.ts\"], \"mappings\": \"AA!A\"}" }) {
		const SourceMap map(broken, "res/layouts/imp.tsx", 1);
		CHECK(map.empty());
		CHECK_STR(map.rewrite(kThrown), kThrown);
	}
}

TEST(a_source_given_as_a_file_url_is_named_beside_the_entry) {
	// What esbuild writes for a file with no relative way to it from where it ran -- one
	// on another drive. Percent-encoded, and with Windows's drive after the third slash.
	const std::filesystem::path elsewhere = std::filesystem::current_path() / "rda map test" / "helpers.ts";
	const std::u8string generic = elsewhere.generic_u8string();
	std::string url = "file://";
	if (generic.empty() || generic.front() != u8'/') url += "/";
	for (const char8_t c : generic) url += (c == u8' ') ? std::string("%20") : std::string(1, static_cast<char>(c));

	const std::string json =
		"{\"version\": 3, \"sources\": [\"" + url + "\", \"res/layouts/imp.tsx\"], "
		"\"mappings\": \";AAAO,SAAS,KAAK,GAAQ;AAC3B,SAAO,EAAE;AACX;;;ACAe,SAAR,IAAqB;AAC1B,QAAM,IAAI;"
		"AACV,SAAO,kBAAC,WAAM,IAAG,KAAI,MAAM,KAAK,CAAC,GAAG;AACtC;\", \"names\": []}";
	const SourceMap map(json, "res/layouts/imp.tsx", 1);

	// Named the way the entry was, relative: from res/layouts, two up and across.
	SourceMap::Origin origin;
	CHECK(map.find(4, 10, origin));
	CHECK_STR(origin.file, "rda map test/helpers.ts");
	CHECK_EQ(origin.line, 2u);
}
