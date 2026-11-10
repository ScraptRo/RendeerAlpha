#include "TestHarness.h"
#include <Layout/JsxSource.h>

#include <string>

// Reading a .tsx for where things are, and changing it without reformatting it.
//
// The editor in `rda prototype` writes to files a person also writes to, so most of these are about
// what must not change: comments, blank lines, indentation and line endings all come through.
using namespace RDA::Layout;
using namespace RDA::Layout::Jsx;

namespace {

	const char* kLayout = R"x(// A comment with <stack> in it, which is not an element.
import { Card } from "./card"

const limit: Array<number> = [1, 2]

export default function Main() {
  const small = limit.length < 3
  return (
    <stack id="root" arrange="vertical">
      {/* a comment between elements */}
      <label id="title" text="a < b" />
      <panel id="body" editable>
        <button id="save" text="Save"
                height={30}
                onClick={() => commands.save()} />
        <button id="cancel" text={() => state.title} />
      </panel>
      {small ? <label id="note" text="short" /> : null}
    </stack>
  )
}
)x";

	int indexOf(const Document& document, const std::string& path, const std::string& type) {
		return document.find(path, type);
	}
}

TEST(jsx_finds_the_elements_and_how_they_nest) {
	const Document document(kLayout);
	const int root = indexOf(document, "root", "stack");
	CHECK(root >= 0);
	const Element& stack = document.elements()[static_cast<size_t>(root)];
	CHECK_EQ(stack.children.size(), size_t(2));
	CHECK_STR(document.elements()[static_cast<size_t>(stack.children[0])].tag, "label");
	CHECK_STR(document.elements()[static_cast<size_t>(stack.children[1])].tag, "panel");
}

TEST(jsx_is_not_fooled_by_what_only_looks_like_an_element) {
	// A comment, a comparison, a generic and a string, every one of which has a `<` in it.
	const Document document(kLayout);
	for (const Element& element : document.elements()) {
		CHECK(element.tag != "number");
		CHECK(element.tag != "3");
		CHECK(element.tag != "b");
	}
	CHECK_EQ(document.elements().size(), size_t(6));
}

TEST(jsx_marks_what_code_makes_as_generated) {
	const Document document(kLayout);
	const int note = indexOf(document, "root/note", "label");
	CHECK(note >= 0);
	CHECK(document.elements()[static_cast<size_t>(note)].generated);
	CHECK(!document.elements()[static_cast<size_t>(indexOf(document, "root/title", "label"))].generated);
}

TEST(jsx_reads_each_kind_of_attribute) {
	const Document document(kLayout);
	const int body = indexOf(document, "root/body", "panel");
	CHECK(document.value(body, "editable").present);
	CHECK_STR(document.value(body, "editable").text, "true");
	CHECK(!document.value(body, "variant").present);

	const int save = indexOf(document, "root/body/save", "button");
	CHECK_STR(document.value(save, "text").text, "Save");
	CHECK_STR(document.value(save, "height").text, "30");
	CHECK(!document.value(save, "height").computed);
	CHECK(document.value(save, "onClick").computed);

	const int cancel = indexOf(document, "root/body/cancel", "button");
	CHECK(document.value(cancel, "text").computed);
}

TEST(jsx_reads_a_string_in_braces_as_the_string) {
	const Document document(R"x(export default () => <label id="a" text={"hello"} width={'fill'} />)x");
	CHECK_STR(document.value(0, "text").text, "hello");
	CHECK(!document.value(0, "text").computed);
	CHECK_STR(document.value(0, "width").text, "fill");
}

TEST(jsx_keeps_going_past_a_tag_that_was_never_one) {
	// A generic arrow function: it looks like a tag and has no end.
	const Document document("const f = <T,>(x: T) => x\nexport default () => <label id=\"a\" />\n");
	CHECK_EQ(document.elements().size(), size_t(1));
	CHECK_STR(document.elements()[0].tag, "label");
}

TEST(jsx_finds_nothing_in_a_layout_written_as_objects) {
	const Document document("export default { type: \"label\", text: \"x\" }\n");
	CHECK(document.elements().empty());
}

TEST(jsx_tells_two_ids_apart_by_where_they_are) {
	const Document document(R"x(export default () => (
  <stack id="root">
    <stack id="left"><button id="x" text="L" /></stack>
    <stack id="right"><button id="x" text="R" /></stack>
  </stack>
))x");
	CHECK_STR(document.value(document.find("root/left/x", "button"), "text").text, "L");
	CHECK_STR(document.value(document.find("root/right/x", "button"), "text").text, "R");
}

TEST(jsx_finds_an_element_with_no_id_by_its_tag_and_its_number) {
	const Document document(R"x(export default () => (
  <stack id="root">
    <label text="one" />
    <label text="two" />
  </stack>
))x");
	CHECK_STR(document.value(document.find("root/label", "label"), "text").text, "one");
	CHECK_STR(document.value(document.find("root/label#2", "label"), "text").text, "two");
	CHECK_EQ(document.find("root/label#3", "label"), -1);
}

TEST(jsx_will_not_guess_between_two_it_cannot_tell_apart) {
	const Document document(R"x(function A() { return <label id="x" /> }
function B() { return <label id="x" /> }
export default () => <stack id="root"><A /><B /></stack>
)x");
	std::string why;
	CHECK_EQ(document.find("root/x", "label", &why), -1);
	CHECK(why.find("2 elements") != std::string::npos);
}

TEST(jsx_will_not_call_an_element_of_another_type_the_same_thing) {
	const Document document(kLayout);
	std::string why;
	CHECK_EQ(document.find("root/title", "button", &why), -1);
	CHECK(why.find("no <button>") != std::string::npos);
}

TEST(jsx_finds_an_element_a_component_holds_under_its_call_site) {
	const Document document(R"x(function Card() { return <panel id="face"><label id="name" /></panel> }
export default () => <stack id="root"><Card id="left" /></stack>
)x");
	CHECK(document.find("root/left/name", "label") >= 0);
}

TEST(jsx_finds_the_editable_root_a_path_cannot_name) {
	const Document one("export function Card() { return <stack id=\"face\" editable><label /></stack> }\n");
	CHECK_EQ(one.find("root/card", "stack"), -1);
	CHECK_EQ(one.editableRoot("stack"), 0);
	CHECK_EQ(one.editableRoot("panel"), -1);

	const Document two("function A() { return <stack editable /> }\nfunction B() { return <stack editable /> }\n");
	CHECK_EQ(two.editableRoot("stack"), -1);

	const Document nested("function A() { return <panel><stack editable /></panel> }\n");
	CHECK_EQ(nested.editableRoot("stack"), -1);
}

TEST(jsx_replaces_an_attribute_and_nothing_else) {
	Document document(kLayout);
	document.setAttribute(indexOf(document, "root/title", "label"), "text", "\"changed\"");
	const std::string expected = std::string(kLayout);
	std::string want = expected;
	want.replace(want.find("text=\"a < b\""), std::string("text=\"a < b\"").size(), "text=\"changed\"");
	CHECK_STR(document.text(), want);
}

TEST(jsx_adds_an_attribute_on_the_line_the_others_are_on) {
	Document document("export default () => <label id=\"a\" text=\"x\" />\n");
	document.setAttribute(0, "height", "{30}");
	CHECK_STR(document.text(), "export default () => <label id=\"a\" text=\"x\" height={30} />\n");
}

TEST(jsx_adds_an_attribute_to_a_tag_with_none) {
	Document document("export default () => <stack />\n");
	document.setAttribute(0, "spacing", "{8}");
	CHECK_STR(document.text(), "export default () => <stack spacing={8} />\n");
}

TEST(jsx_gives_an_attribute_its_own_line_where_they_each_have_one) {
	Document document(kLayout);
	document.setAttribute(indexOf(document, "root/body/save", "button"), "width", "{120}");
	CHECK(document.text().find("                onClick={() => commands.save()}\n                width={120} />")
	      != std::string::npos);
}

TEST(jsx_removes_an_attribute_on_a_shared_line) {
	Document document("export default () => <label id=\"a\" text=\"x\" height={3} />\n");
	document.removeAttribute(0, "text");
	CHECK_STR(document.text(), "export default () => <label id=\"a\" height={3} />\n");
}

TEST(jsx_removes_an_attribute_with_the_line_it_had_to_itself) {
	Document document(kLayout);
	document.removeAttribute(indexOf(document, "root/body/save", "button"), "height");
	CHECK(document.text().find("text=\"Save\"\n                onClick") != std::string::npos);
}

TEST(jsx_inserts_a_child_at_the_end_with_its_brothers_indentation) {
	Document document(kLayout);
	document.insertChild(indexOf(document, "root/body", "panel"), "<label id=\"new\" />", 99);
	CHECK(document.text().find("        <button id=\"cancel\" text={() => state.title} />\n"
	                           "        <label id=\"new\" />\n"
	                           "      </panel>") != std::string::npos);
}

TEST(jsx_inserts_a_child_before_another) {
	Document document(kLayout);
	document.insertChild(indexOf(document, "root/body", "panel"), "<label id=\"new\" />", 1);
	CHECK(document.text().find("        <label id=\"new\" />\n        <button id=\"cancel\"") != std::string::npos);
}

TEST(jsx_opens_up_a_tag_that_closed_itself) {
	Document document("export default () => (\n  <stack id=\"a\" />\n)\n");
	document.insertChild(0, "<label id=\"new\" />", 0);
	CHECK_STR(document.text(),
	          "export default () => (\n  <stack id=\"a\">\n    <label id=\"new\" />\n  </stack>\n)\n");
}

TEST(jsx_inserts_into_an_element_closed_on_its_own_line) {
	Document document("export default () => (\n\t<stack id=\"a\"></stack>\n)\n");
	document.insertChild(0, "<label id=\"new\" />", 0);
	CHECK_STR(document.text(),
	          "export default () => (\n\t<stack id=\"a\">\n\t\t<label id=\"new\" />\n\t</stack>\n)\n");
}

TEST(jsx_indents_every_line_of_a_child_written_on_several) {
	Document document("export default () => (\n  <stack id=\"a\" />\n)\n");
	document.insertChild(0, "<select id=\"s\">\n  <option id=\"o\" />\n</select>", 0);
	CHECK(document.text().find("    <select id=\"s\">\n      <option id=\"o\" />\n    </select>\n") != std::string::npos);
}

TEST(jsx_writes_the_line_endings_the_file_has) {
	Document document("export default () => (\r\n  <stack id=\"a\" />\r\n)\r\n");
	document.insertChild(0, "<label id=\"new\" />", 0);
	CHECK_STR(document.text(),
	          "export default () => (\r\n  <stack id=\"a\">\r\n    <label id=\"new\" />\r\n  </stack>\r\n)\r\n");
}

TEST(jsx_what_it_inserts_can_be_found_again) {
	Document document(kLayout);
	document.insertChild(indexOf(document, "root/body", "panel"), "<label id=\"new\" text=\"x\" />", 0);
	const int added = document.find("root/body/new", "label");
	CHECK(added >= 0);
	CHECK_STR(document.value(added, "text").text, "x");
}

TEST(jsx_removes_an_element_with_its_line) {
	Document document(kLayout);
	document.removeElement(indexOf(document, "root/title", "label"));
	CHECK(document.text().find("id=\"title\"") == std::string::npos);
	CHECK(document.text().find("{/* a comment between elements */}\n      <panel") != std::string::npos);
}

TEST(jsx_removes_an_element_from_a_line_it_shares) {
	Document document("export default () => <stack id=\"a\"><label id=\"b\" /><label id=\"c\" /></stack>\n");
	document.removeElement(document.find("a/b", "label"));
	CHECK_STR(document.text(), "export default () => <stack id=\"a\"><label id=\"c\" /></stack>\n");
}

TEST(jsx_makes_up_an_id_the_file_does_not_have) {
	const Document document("export default () => <stack id=\"button1\"><label id=\"button2\" /></stack>\n");
	CHECK_STR(document.uniqueId("button"), "button3");
	CHECK_STR(document.uniqueId("label"), "label1");
}

TEST(jsx_writes_a_value_the_way_its_type_is_written) {
	std::string out, why;
	CHECK(formatValue(RDA::Layout::PropType::String, "hello", out, why));
	CHECK_STR(out, "\"hello\"");
	CHECK(formatValue(RDA::Layout::PropType::Number, " 12.5 ", out, why));
	CHECK_STR(out, "{12.5}");
	CHECK(formatValue(RDA::Layout::PropType::Bool, "false", out, why));
	CHECK_STR(out, "{false}");
	CHECK(formatValue(RDA::Layout::PropType::Size, "120", out, why));
	CHECK_STR(out, "{120}");
	CHECK(formatValue(RDA::Layout::PropType::Size, "fill-40", out, why));
	CHECK_STR(out, "\"fill-40\"");
}

TEST(jsx_puts_text_with_a_quote_in_it_in_braces) {
	std::string out, why;
	CHECK(formatValue(RDA::Layout::PropType::String, "say \"hi\"", out, why));
	CHECK_STR(out, "{\"say \\\"hi\\\"\"}");
}

TEST(jsx_refuses_a_value_that_is_not_one) {
	std::string out, why;
	CHECK(!formatValue(RDA::Layout::PropType::Number, "wide", out, why));
	CHECK_STR(why, "a number");
	CHECK(!formatValue(RDA::Layout::PropType::Size, "wide", out, why));
	CHECK(!formatValue(RDA::Layout::PropType::Event, "x", out, why));
}
