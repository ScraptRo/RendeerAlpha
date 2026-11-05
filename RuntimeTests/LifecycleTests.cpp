#include "TestHarness.h"

#include <Core/Signals.h>
#include <GraphicalObjects/Widgets/Containers/Container.h>
#include <Layout/Blueprint.h>
#include <Layout/LayoutHost.h>
#include <Layout/LayoutLoader.h>
#include <Layout/Router.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// onOpen and onClose against the real LayoutHost and Router, which is why this is linked with
// the engine. The handlers are compiled by hand rather than by the compiler, so that these
// cases need neither esbuild nor a device: each appends a letter to `state.log`, and the order
// of the letters is what is being tested.
namespace {

	using namespace RDA;
	using namespace RDA::Layout;

	// `state.log = state.log + letter`
	Program appends(const std::string& letter) {
		Program program;
		program.signals = { "log" };
		program.texts = { letter };
		program.code = {
			{ Op::LoadSignal, 0 }, { Op::PushText, 0 }, { Op::Concat, 0 }, { Op::StoreSignal, 0 }, { Op::Halt, 0 },
		};
		return program;
	}

	// `state.log = state.log + state.route`
	Program appendsRoute() {
		Program program;
		program.signals = { "log", "route" };
		program.code = {
			{ Op::LoadSignal, 0 }, { Op::LoadSignal, 1 }, { Op::Concat, 0 }, { Op::StoreSignal, 0 }, { Op::Halt, 0 },
		};
		return program;
	}

	std::filesystem::path folder() {
		static const std::filesystem::path path = [] {
			std::filesystem::path made = std::filesystem::temp_directory_path() / "rda-lifecycle-tests";
			std::filesystem::create_directories(made);
			return made;
		}();
		return path;
	}

	std::string write(const std::string& name, const Program& open, const Program& close,
	                  const char* type = "stack") {
		BlueprintBuilder builder;
		const uint32_t node = builder.addNode(builder.intern(type), builder.intern(name), kNoParent);
		builder.addBinding(node, builder.intern("onOpen"), BindingKind::Event, open);
		builder.addBinding(node, builder.intern("onClose"), BindingKind::Event, close);
		const std::vector<uint8_t> bytes = builder.finish();
		const std::string path = (folder() / (name + ".rdab")).string();
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return path;
	}

	// A screen called `name`, opening with the upper-case letter and closing with the lower.
	std::string screen(const std::string& name, const std::string& open, const std::string& close) {
		return write(name, appends(open), appends(close));
	}

	struct Fixture {
		uint32_t  log = 0;
		Container parent{ "parent" };
		Fixture() {
			closeOpenLayouts();
			signals().clear();
			log = signals().define("log", std::string_view(""));
			// What declaring routes creates, and what a Router reads to know where it is.
			signals().define("route", std::string_view("a"));
		}
		~Fixture() {
			closeOpenLayouts();
			signals().clear();
		}
		std::string said() const { return std::string(signals().text(log)); }
	};

	std::vector<Router::Route> twoScreens() {
		return { { "a", "a", {} }, { "b", "b", {} } };
	}
}

TEST(a_layout_opens_when_it_is_opened) {
	Fixture fx;
	LayoutHost host;
	CHECK(host.open(fx.parent, screen("a", "A", "a")));
	CHECK_STR(fx.said(), "A");
}

TEST(a_reload_runs_neither_handler_and_the_layout_stays_open) {
	Fixture fx;
	LayoutHost host;
	host.open(fx.parent, screen("a", "A", "a"));
	CHECK(host.reload());
	CHECK(host.reload());
	CHECK_STR(fx.said(), "A");

	closeOpenLayouts();
	CHECK_STR(fx.said(), "Aa");                  // once, and open all along
}

TEST(a_reload_closes_with_the_handler_it_brought) {
	Fixture fx;
	LayoutHost host;
	const std::string path = screen("a", "A", "a");
	host.open(fx.parent, path);
	screen("a", "A", "z");                       // the file saved with a new onClose
	CHECK(host.reload());
	closeOpenLayouts();
	CHECK_STR(fx.said(), "Az");
}

TEST(opening_another_layout_on_a_host_closes_the_one_it_had) {
	Fixture fx;
	LayoutHost host;
	host.open(fx.parent, screen("a", "A", "a"));
	host.open(fx.parent, screen("b", "B", "b"));
	CHECK_STR(fx.said(), "AaB");
}

TEST(a_layout_that_will_not_load_leaves_the_open_one_open) {
	Fixture fx;
	LayoutHost host;
	host.open(fx.parent, screen("a", "A", "a"));
	CHECK(!host.open(fx.parent, (folder() / "no-such-layout.rdab").string()));
	CHECK_STR(fx.said(), "A");
}

TEST(each_handler_runs_at_most_once) {
	Fixture fx;
	LayoutHost host;
	host.open(fx.parent, screen("a", "A", "a"), {}, LayoutHost::Announce::Later);
	CHECK_STR(fx.said(), "");                    // built, and nothing said yet
	host.announceOpen();
	host.announceOpen();
	CHECK_STR(fx.said(), "A");
	host.announceClose();
	host.announceClose();
	closeOpenLayouts();
	CHECK_STR(fx.said(), "Aa");
}

TEST(a_layout_with_neither_handler_opens_and_closes_quietly) {
	Fixture fx;
	BlueprintBuilder builder;
	builder.addNode(builder.intern("stack"), builder.intern("plain"), kNoParent);
	const std::vector<uint8_t> bytes = builder.finish();
	const std::string path = (folder() / "plain.rdab").string();
	std::ofstream(path, std::ios::binary | std::ios::trunc)
		.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	LayoutHost host;
	CHECK(host.open(fx.parent, path));
	closeOpenLayouts();
	CHECK_STR(fx.said(), "");
}

TEST(a_root_popup_keeps_the_onclose_it_means) {
	Fixture fx;
	LayoutHost host;
	CHECK(host.open(fx.parent, write("p", appends("P"), appends("p"), "popup")));
	CHECK_STR(fx.said(), "P");                   // onOpen is the layout's, on any root
	closeOpenLayouts();
	CHECK_STR(fx.said(), "P");                   // onClose is the popup's own event here
}

TEST(a_host_that_has_gone_is_not_called) {
	Fixture fx;
	{
		LayoutHost host;
		host.open(fx.parent, screen("a", "A", "a"));
	}
	closeOpenLayouts();                          // nothing left to reach, and nothing to crash on
	CHECK_STR(fx.said(), "A");
}

TEST(exit_closes_everything_still_open) {
	Fixture fx;
	LayoutHost one, two;
	one.open(fx.parent, screen("a", "A", "a"));
	two.open(fx.parent, screen("b", "B", "b"));
	closeOpenLayouts();
	CHECK_STR(fx.said(), "ABab");
	closeOpenLayouts();
	CHECK_STR(fx.said(), "ABab");
}

TEST(a_route_change_closes_the_old_screen_and_then_opens_the_new_one) {
	Fixture fx;
	screen("a", "A", "a");
	screen("b", "B", "b");
	Router router;
	CHECK(router.open(fx.parent, twoScreens(), folder().string()));
	CHECK_STR(fx.said(), "A");
	CHECK(router.navigate("b"));
	CHECK_STR(fx.said(), "A");                   // the swap waits for update()
	CHECK(router.update());
	CHECK_STR(fx.said(), "AaB");
}

TEST(a_cross_fade_keeps_the_same_order) {
	Fixture fx;
	screen("a", "A", "a");
	screen("b", "B", "b");
	Router router;
	router.setTransitionMs(200.0f);
	router.open(fx.parent, twoScreens(), folder().string());
	router.navigate("b");
	router.update();
	CHECK_STR(fx.said(), "AaB");                 // the old one is still fading, and has closed
	closeOpenLayouts();
	CHECK_STR(fx.said(), "AaBb");                // and is not closed a second time
}

TEST(going_back_closes_and_opens_like_going_forward) {
	Fixture fx;
	screen("a", "A", "a");
	screen("b", "B", "b");
	Router router;
	router.open(fx.parent, twoScreens(), folder().string());
	router.navigate("b");
	router.update();
	CHECK(router.back());
	CHECK(router.update());
	CHECK_STR(fx.said(), "AaBbA");
}

TEST(onopen_sees_the_route_it_is_opening_going_back_too) {
	Fixture fx;
	write("a", appendsRoute(), appends("-"));
	write("b", appendsRoute(), appends("-"));
	Router router;
	router.open(fx.parent, twoScreens(), folder().string());
	router.navigate("b");
	router.update();
	router.back();
	router.update();
	CHECK_STR(fx.said(), "a-b-a");
}

TEST(a_route_that_will_not_load_runs_neither_handler) {
	Fixture fx;
	screen("a", "A", "a");
	std::filesystem::remove(folder() / "b.rdab");
	Router router;
	router.open(fx.parent, twoScreens(), folder().string());
	router.navigate("b");
	CHECK(!router.update());
	CHECK_STR(fx.said(), "A");
	closeOpenLayouts();
	CHECK_STR(fx.said(), "Aa");
}

TEST(a_reload_of_the_screen_showing_runs_neither_handler) {
	Fixture fx;
	screen("a", "A", "a");
	screen("b", "B", "b");
	Router router;
	router.open(fx.parent, twoScreens(), folder().string());
	CHECK(router.host().reload());
	CHECK_STR(fx.said(), "A");
}
