#pragma once
#include <Layout/Blueprint.h>
#include <Layout/LayoutLoader.h>
#include <GraphicalObjects/Widget.h>
#include <string>
#include <string_view>
#include <vector>

// A compiled layout, opened without a window, so a test can assert what an interface
// *does* instead of what it looks like.
//
// Everything this needs was already independent of the renderer and nobody had put it
// together: a blueprint is a POD array, instantiating it calls constructors, a handler is
// a compiled program over signals, and signals are plain C++ objects. None of that wants
// a device. What was missing was a way to name a node and fire its handler.
//
//     Probe probe;
//     probe.open("res/layouts/hello.rdab");
//     probe.click("root/top_bar/buttons/add-one");
//     CHECK(State::count() == 1);
//     CHECK(probe.text("root/top_bar/title") == "Clicked 1 time");
//
// The ids are the paths the compiler assigned, the same ones `rda dump` prints -- so a
// test names what the layout named, and renaming a widget breaks the test rather than
// quietly testing nothing.
//
// Call the application's generated State::define() before open(). A binding resolves the
// signals it reads when it is instantiated, and one that does not exist yet is created as
// a number and complained about -- which is a real diagnostic, not something to work
// around here.
//
// What this is not: it does not lay anything out, measure text, or draw. Those need a
// font atlas and a device, and a test that wanted them would be a screenshot test rather
// than this. Everything above the pixels is here.
namespace RDA::Layout {

	class Probe {
	public:
		bool open(const std::string& blueprintPath);
		bool openFrom(Blueprint blueprint);
		void close();
		bool isOpen() const { return mInstance.valid(); }
		const std::string& error() const { return mError; }

		// ---- what is in it ----
		// Every widget's id, in the order the blueprint holds them. Useful on its own: a
		// test that prints these is how you find out what a layout is called.
		std::vector<std::string> ids() const;
		bool     exists(std::string_view id) const { return find(id) != nullptr; }
		Widget*  find(std::string_view id) const;
		// How many nodes the *blueprint* holds, which is not how many widgets exist: a
		// list's row template is one node and forty pooled copies. Asserting on this is
		// how a test says "this screen did not grow a widget per row".
		size_t   nodeCount() const { return mBlueprint.nodeCount(); }

		// ---- doing things to it ----
		//
		// Each of these does what the widget does when a person does it: it runs the
		// handler the layout bound, then applies the bindings that handler's writes
		// reached -- because in a running program the next frame always would, and a test
		// that had to remember that would be a test about frames.
		//
		// Each returns false when there is no such widget, or it is not the kind that does
		// this. Never silently nothing: a test that clicks a label has a bug in it, and
		// finding out four assertions later is worse than finding out here.
		bool click(std::string_view id);
		bool set(std::string_view id, bool value);          // checkbox
		bool set(std::string_view id, double value);        // slider
		bool type(std::string_view id, std::string text);   // text field
		bool choose(std::string_view id, std::string value);// select, by an option's value
		bool showTab(std::string_view id, int index);       // tabs

		// Applies every binding whose signals moved. Only needed after writing a signal
		// directly -- the actions above do it for you.
		size_t settle();

		// ---- reading it back ----
		// A widget's text: what a label or button says, what a field holds, a select's
		// chosen value, a tab's title. Empty for anything with no text of its own.
		std::string text(std::string_view id) const;
		// Whether every ancestor is visible too, which is what decides if it is on screen.
		bool shown(std::string_view id) const;

	private:
		Widget*      require(std::string_view id, const char* what);
		Container    mRoot{ "probe" };
		Blueprint    mBlueprint;
		LayoutInstance mInstance;
		std::string  mError;
	};
}
