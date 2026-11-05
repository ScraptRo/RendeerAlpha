// A window with the layout in it and nothing behind it.
//
// An interface is written against state, tables and commands that a backend supplies,
// which means that until somebody writes the backend there is nothing to look at: a
// list with no rows is an empty box, and a detail screen with no signal set is a
// screen full of zeroes. That is the wrong way round while the screen is the thing
// being designed.
//
// So this defines everything the declaration promises, puts placeholder contents in
// it, and opens the window. Editing a .tsx rebuilds it in place, the same hot reload
// an application gets.
#include "Preview.h"

#include "Commands.h"

// `prototype` opens a window, so this is the one part of the toolchain that links the runtime
// -- the device, the window, the renderer -- and it is why it is a program of its own,
// `rda-preview`. Nothing else here touches any of it.
#include <RendeerAlpha.h>
#include <Core/FileStamp.h>
#include <Core/Png.h>
#include <Core/Signals.h>
#include <Layout/Blueprint.h>
#include <GraphicalObjects/Gui.h>
#include <Layout/LayoutHost.h>
#include <Layout/ModuleTransform.h>
#include <Layout/Prototype.h>
#include <Layout/Router.h>
#include <Layout/ScriptHost.h>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace Cli {

	namespace {

		// ---- state.ts, watched --------------------------------------------------------------

		// state.ts while a prototype runs: what it and its imports were when last read, so a
		// save is noticed and its code swapped in with every value kept.
		class StateWatch {
		public:
			bool watching() const { return !mFile.empty(); }
			const fs::path& file() const { return mFile; }

			void watch(const fs::path& file) {
				mFile = file;
				remember();
			}

			// The imports are found again each time, so a module split off since the last
			// read is watched from now on.
			void remember() {
				mFiles.watch(RDA::Layout::importedFiles(mFile.string()));
			}

			bool changed() { return mFiles.changed(); }

			// A few times a second rather than every frame: a stat of each file it imports is
			// cheap, and a save is a human pressing a key.
			bool due(float dt) {
				mSinceCheck += dt;
				if (mSinceCheck < 0.25f) return false;
				mSinceCheck = 0.0f;
				return true;
			}

		private:
			fs::path       mFile;
			RDA::FileWatch mFiles;
			float          mSinceCheck = 0.0f;
		};

		// Printed rather than logged: this is the tool talking, not the engine, and the
		// whole point of pressing the button is to see that it asked for something.
		// Flushed, because stdout to a pipe is block-buffered and this process does not
		// exit -- it sits there with a window open, and a line nobody sees until then is
		// a line that told nobody anything. With what it was asked with: `[3,"x"]` is
		// printed as the call it was, commands.rename(3,"x").
		void reportCommand(const char* name, const char* args) {
			std::string inside = args ? args : "[]";
			if (inside.size() >= 2 && inside.front() == '[' && inside.back() == ']')
				inside = inside.substr(1, inside.size() - 2);
			std::printf("rda: commands.%s(%s)\n", name, inside.c_str());
			// Said the first time and not every time: a line like the one above reads as the
			// command having been carried out, and nothing is -- there is no backend here to
			// carry it out. A script that clicked a button and saw it print was told it worked.
			static bool said = false;
			if (!said) {
				said = true;
				std::printf("rda: no backend is attached, so a command only prints its call "
				            "and nothing else happens\n");
			}
			std::fflush(stdout);
		}

		// ---- widgets named on the command line -----------------------------------------------

		// Which of these ids `target` means: the whole id when one is exactly that, otherwise
		// every id that ends with it after a slash. `add` is the end of root/buttons/add and not
		// of root/buttons/padding, which is why the slash is part of the match.
		std::vector<std::string> idsMatching(const std::vector<std::string>& ids, const std::string& target) {
			std::vector<std::string> exact, ending;
			const std::string tail = "/" + target;
			for (const std::string& id : ids) {
				if (id == target) exact.push_back(id);
				else if (id.size() > tail.size() && id.compare(id.size() - tail.size(), tail.size(), tail) == 0)
					ending.push_back(id);
			}
			return exact.empty() ? ending : exact;
		}

		std::string listOf(const std::vector<std::string>& ids, size_t most) {
			std::string out;
			for (size_t i = 0; i < ids.size() && i < most; ++i) out += (i ? ", " : "") + ids[i];
			if (ids.size() > most) out += ", ... (" + std::to_string(ids.size() - most) + " more)";
			return out;
		}

		// What to say when `target` does not name exactly one of `ids`; empty when it does.
		std::string refusalFor(const std::vector<std::string>& ids, const std::string& target,
		                       const std::string& where) {
			const std::vector<std::string> found = idsMatching(ids, target);
			if (found.size() == 1) return {};
			if (found.empty()) {
				return "`" + target + "` is not the id of any widget in " + where + ". Its ids are: " +
				       listOf(ids, 14);
			}
			return "`" + target + "` is the end of " + std::to_string(found.size()) + " widgets' ids in " +
			       where + " -- say more of the path: " + listOf(found, 8);
		}

		// ---- the session --------------------------------------------------------------------

		// What `prototype` and `shot` do, from the options to the exit code.
		//
		// The Router and the host outlive the callbacks that make them, so they belong to
		// the session rather than to any one of those -- the same reason an application
		// keeps its router at file scope.
		class PreviewSession {
		public:
			PreviewSession(PreviewOptions options, bool shooting)
				: mOptions(std::move(options)), mShooting(shooting), mPointer(mOptions.pointer) {}

			int run() {
				if (!locateProject()) return 1;

				// Compiled first, and by the same code `rda build` runs: a prototype of a layout
				// that has not been compiled since it was edited is a prototype of the wrong
				// thing.
				if (mOptions.compile && cmdBuild({ "build", mOptions.project.string(), "--no-detect" }) != 0) return 1;

				if (!readState()) return 1;
				if (!findCompiledFiles()) return 1;
				if (!checkTargets()) return 1;
				if (!enterProject()) return 1;

				RDA::AppConfig config;
				configure(config);
				std::fflush(stdout);
				rendeerRun(config);
				return outcome();
			}

		private:
			// ---- before the window ----------------------------------------------------------

			bool locateProject() {
				std::error_code ec;
				mOptions.project = fs::absolute(mOptions.project, ec);
				mRes = mOptions.project / "res";
				if (!fs::is_directory(mRes, ec)) {
					std::printf("rda: no res/ directory in %s -- a project keeps its declaration, "
					            "theme and layouts there\n", shown(mOptions.project).c_str());
					return false;
				}
				return true;
			}

			bool readState() {
				std::error_code ec;
				const fs::path stateFile = mRes / "state.ts";
				if (!fs::is_regular_file(stateFile, ec)) return true;

				// state.ts's code runs here exactly as it would under a backend: compiled now,
				// loaded once its signals exist, and reloaded when it is saved.
				if (!loadStateWithCode(stateFile.string(), mSchema)) return false;
				mWatch.watch(stateFile);
				return true;
			}

			bool findCompiledFiles() {
				const std::vector<fs::path> themes = filesIn(mRes / "themes", ".rdth");
				mBlueprints = filesIn(mRes / "layouts", ".rdab");
				if (mBlueprints.empty()) {
					std::printf("rda: nothing to show -- %s holds no compiled layouts\n",
					            shown(mRes / "layouts").c_str());
					return false;
				}
				if (!themes.empty()) {
					mThemePath = "res/themes/" + themes.front().filename().string();
				}
				return true;
			}

			// Every widget the command line aims at, checked against the compiled layout before a
			// window opens: a name that matches nothing, or too much, is a refusal here and not a
			// picture of nothing having been clicked. The ids are in the blueprint, so this needs no
			// device -- which is also what lets it be tested where there is none.
			//
			// Only when the layout is known. With routes and no --layout, which screen opens
			// depends on state the window has not read yet, and the check is left to the moment
			// the widget is looked for.
			bool checkTargets() {
				std::vector<std::string> targets = mOptions.script.targets();
				if (!mOptions.pointerTarget.empty()) targets.push_back(mOptions.pointerTarget);
				if (targets.empty() || !mShooting) return true;

				std::string stem = mOptions.opening;
				if (!mSchema.routes.empty()) {
					stem.clear();
					for (const RDA::Layout::StateRoute& route : mSchema.routes) {
						if (route.name == mOptions.opening) stem = route.layout;
					}
				} else if (stem.empty() && mBlueprints.size() == 1) {
					stem = mBlueprints.front().stem().string();
				}
				if (stem.empty()) return true;

				const fs::path blueprintPath = mRes / "layouts" / (stem + ".rdab");
				RDA::Layout::Blueprint blueprint;
				std::string error;
				if (!RDA::Layout::loadBlueprintFile(blueprintPath.string(), blueprint, error)) {
					// Not this check's to report: the opening will say so, with more to go on.
					return true;
				}
				std::vector<std::string> ids;
				for (size_t i = 0; i < blueprint.nodeCount(); ++i) {
					ids.emplace_back(blueprint.string(blueprint.node(i).id));
				}
				const std::string where = shown(mRes / "layouts" / (stem + ".tsx"));
				for (const std::string& target : targets) {
					const std::string refusal = refusalFor(ids, target, where);
					if (!refusal.empty()) {
						std::printf("rda: %s\n", refusal.c_str());
						return false;
					}
				}
				return true;
			}

			// Everything a layout names -- an image, an icon, a mesh -- is relative to the
			// working directory, exactly as it will be when the application runs. So the tool
			// moves into the project rather than trying to rewrite paths it does not own.
			bool enterProject() {
				std::error_code ec;
				fs::current_path(mOptions.project, ec);
				if (ec) {
					std::printf("rda: cannot work in %s\n", shown(mOptions.project).c_str());
					return false;
				}
				return true;
			}

			void configure(RDA::AppConfig& config) {
				config.app.name = (mShooting ? "rda shot - " : "rda prototype - ")
				                + mOptions.project.filename().string();
				config.app.appVersion = 1;
				config.gui.enabled = true;
				if (mShooting) {
					// Built every frame and never shown. The window still exists, because the
					// device and the pipelines are chosen against a surface.
					config.offscreen = true;
					if (mOptions.width > 0 && mOptions.height > 0) {
						config.windowWidth = mOptions.width;
						config.windowHeight = mOptions.height;
					}
					// A shot says what it wants a unit to be worth, so two machines produce the
					// same picture. Left alone it asks the display, like anything else.
					if (mOptions.scale > 0.0f) config.gui.uiScale = mOptions.scale;
				}
				if (!mThemePath.empty()) config.gui.themePath = mThemePath;
				config.redrawMode = RDA::RedrawMode::OnDemand;
				config.viewportMode = RDA::ViewportMode::Fullscreen;
				config.threadMode = RDA::ThreadMode::Caller;

				// The callbacks run inside rendeerRun, which returns after they are done with,
				// so they may hold this session by address.
				config.onStart = [this] { onStart(); };
				config.onUpdate = [this](float dt) { onUpdate(dt); };
				// The frame, whole. Everything in onUpdate decided *whether* to take a picture;
				// this is the only moment at which there is a finished one to take.
				config.onFrameEnd = [this] { onFrameEnd(); };
				config.onShutdown = [this] { mRouter = RDA::Layout::Router{}; };
			}

			int outcome() const {
				if (mShooting && !mRefused && mShot.taken == 0) {
					std::printf("rda: nothing was captured -- the window closed before the first "
					            "frame settled\n");
					return 1;
				}
				return mRefused ? 1 : 0;
			}

			// ---- the window opens -----------------------------------------------------------

			// Every refusal happens inside onStart, which cannot return a code -- it can only
			// stop the loop. Without this the tool exited 0 after saying it would not do the
			// thing, which is the one way a message is worse than useless: a script believes it.
			void refuse() {
				mRefused = true;
				rendeerStop();
			}

			void onStart() {
				if (!defineState()) return;
				if (!fillPlaceholders()) return;
				RDA::Gui& gui = getMainWindow()->gui();
				if (!mSchema.routes.empty()) openRoutes(gui);
				else                         openLayout(gui);
			}

			bool defineState() {
				const RDA::Layout::PrototypeResult defined =
					RDA::Layout::definePrototypeState(mSchema, reportCommand);
				if (!defined.ok) {
					std::printf("rda: %s\n", defined.error.c_str());
					refuse();
					return false;
				}
				std::printf("rda: %zu signals, %zu tables, %zu commands%s\n",
				            defined.signals, defined.tables, defined.commands, stateSummary(mSchema).c_str());
				if (!mSchema.code.empty()) {
					std::string error;
					if (!RDA::Layout::scriptHost().loadState(mSchema.code.data(), mSchema.code.size(), error)) {
						std::printf("rda: %s\n", error.c_str());
						refuse();
						return false;
					}
				}
				return true;
			}

			// A file first, then generated rows, then single values: each is more
			// specific than the one before, so the last word goes to the shortest thing
			// somebody typed.
			bool fillPlaceholders() {
				if (!mOptions.dataFile.empty()) {
					const RDA::Layout::PrototypeResult loaded =
						RDA::Layout::loadPrototypeData(mOptions.dataFile);
					if (!loaded.ok) {
						std::printf("rda: %s\n%s\n", mOptions.dataFile.c_str(), loaded.error.c_str());
						refuse();
						return false;
					}
					std::printf("rda: %s -- %zu signals, %zu tables, %zu rows\n",
					            mOptions.dataFile.c_str(), loaded.signals, loaded.tables, loaded.rows);
				}
				for (const std::string& one : mOptions.rows) {
					std::string error;
					if (!RDA::Layout::fillPrototypeRows(one, error)) {
						std::printf("rda: --rows %s: %s\n", one.c_str(), error.c_str());
						refuse();
						return false;
					}
				}
				for (const std::string& one : mOptions.sets) {
					std::string error;
					if (!RDA::Layout::setPrototypeValue(one, error)) {
						std::printf("rda: --set %s: %s\n", one.c_str(), error.c_str());
						refuse();
						return false;
					}
				}
				return true;
			}

			// Declared routes, so the whole application's navigation works and `--layout`
			// picks which screen to open on.
			void openRoutes(RDA::Gui& gui) {
				std::vector<RDA::Layout::Router::Route> routes;
				for (const RDA::Layout::StateRoute& declared : mSchema.routes) {
					RDA::Layout::Router::Route route;
					route.name = declared.name;
					route.layout = declared.layout;
					route.params = declared.params;
					routes.push_back(std::move(route));
				}
				const std::string& opening = mOptions.opening;
				if (!opening.empty()) {
					bool known = false;
					for (const auto& route : routes) known = known || route.name == opening;
					if (!known) {
						std::printf("rda: no route called `%s`\n", opening.c_str());
						refuse();
						return;
					}
					RDA::signals().set(RDA::signals().find("route"), std::string_view(opening));
				}
				mRouter.setTransitionMs(180.0f);
				if (!mRouter.open(gui.retained(), std::move(routes), "res/layouts", "res/layouts")) {
					std::printf("rda: %s\n", mRouter.lastError().c_str());
					refuse();
					return;
				}
				mRouted = true;
				std::printf("rda: showing %s -- edit a .tsx and it rebuilds\n",
				            mRouter.current().c_str());
				std::fflush(stdout);
			}

			// No routes: one layout, named or the only one there is.
			void openLayout(RDA::Gui& gui) {
				std::string stem = mOptions.opening;
				if (stem.empty()) {
					if (mBlueprints.size() > 1) {
						std::printf("rda: %zu layouts and no routes declared -- say which with "
						            "--layout <name>\n", mBlueprints.size());
						refuse();
						return;
					}
					stem = mBlueprints.front().stem().string();
				}
				const std::string blueprint = "res/layouts/" + stem + ".rdab";
				const std::string source = "res/layouts/" + stem + ".tsx";
				if (!mHost.open(gui.retained(), blueprint, source)) {
					std::printf("rda: %s\n", mHost.lastError().c_str());
					refuse();
					return;
				}
				std::printf("rda: showing %s -- edit it and it rebuilds\n", source.c_str());
				std::fflush(stdout);
			}

			// ---- every frame ----------------------------------------------------------------

			void onUpdate(float dt) {
				// Both restructure the widget tree, which is why neither happens inside a
				// widget's own callback.
				if (mRouted) mRouter.update();
				else         mHost.reloadIfChanged();

				if (!mShooting) {
					if (mWatch.watching() && mWatch.due(dt) && mWatch.changed()) reloadState();
					return;
				}
				if (mRefused) return;
				advanceShot(dt);
			}

			// Reads state.ts again and swaps its code in. What it newly declares is defined;
			// what already existed keeps its value, which is the point of reloading rather
			// than restarting.
			void reloadState() {
				mWatch.remember();
				RDA::Layout::StateSchema schema = RDA::Layout::loadStateSchema(mWatch.file().string());
				if (!schema.ok) {
					std::printf("rda: state.ts did not reload: %s\n", schema.error.c_str());
					std::fflush(stdout);
					return;
				}
				RDA::Layout::definePrototypeState(schema, reportCommand, /*onlyNew*/ true);
				std::string error;
				if (!schema.hasCode()) {
					RDA::Layout::scriptHost().unloadState();
				} else if (!withStateCode(schema, mWatch.file().string()) ||
				           !RDA::Layout::scriptHost().loadState(schema.code.data(), schema.code.size(), error)) {
					if (!error.empty()) std::printf("rda: state.ts did not reload: %s\n", error.c_str());
					std::fflush(stdout);
					return;
				}
				std::printf("rda: state.ts reloaded%s\n", stateSummary(schema).c_str());
				std::fflush(stdout);
				rendeerRequestRedraw();
			}

			// ---- the shot -------------------------------------------------------------------

			// Decides, a frame at a time, whether this is the one to take.
			void advanceShot(float dt) {
				// The first frame's dt is not a frame's worth of time: it carries everything
				// between the clock starting and the interface existing -- compiling the
				// project, opening a window, baking a font -- which here was several seconds.
				// Counting it would satisfy any settle time before the first walk had happened,
				// and a shot would be of an interface that had not been hovered, pressed or
				// animated yet. So the opening frames are not counted, and no single frame may
				// contribute more than a frame plausibly takes.
				++mShot.tick;
				// Asked for from the first frame, so the tree exists by the time anything is
				// captured; it is built by the walk, and only once somebody wants it.
				// Also when a widget is aimed at by name: where it is comes from the same tree.
				if (mShot.tick == 1 && (!mOptions.a11yOut.empty() || aimsAtWidgets()))
					getMainWindow()->describeForAccessibility();
				if (mShot.tick <= 2) return;
				const float ms = (std::min)(dt * 1000.0f, 100.0f);
				mShot.sinceStart += ms;

				if (!placePointerOnTarget()) return;
				holdPointer();

				// The steps, one a frame, before anything is counted as settled: a shortcut's
				// handler writes state, the binding applies on the frame after, and whatever
				// it set moving starts from there. So the settle time and the wait for
				// stillness both begin again once the last step is down.
				if (!mOptions.script.finished()) {
					std::string problem;
					const Resolver resolve = [this](const std::string& target, float& x, float& y,
					                                std::string& why) { return findWidget(target, x, y, why); };
					const Found sent = mOptions.script.sendNext(mPointer, resolve, problem);
					if (sent == Found::No) {
						std::printf("rda: %s\n", problem.c_str());
						refuse();
						return;
					}
					// The layout has not been walked yet: try the same step on the next frame.
					if (sent == Found::Yes) {
						mShot.sinceStart = 0.0f;
						mShot.stillFor = 0.0f;
					}
					return;
				}

				if (!hasWaitedLongEnough(ms)) return;
				mShot.captureNow = true;
			}

			bool aimsAtWidgets() const {
				return !mOptions.pointerTarget.empty() || !mOptions.script.targets().empty();
			}

			// Where the widget `target` is: the centre of its bounds in the tree the last frame
			// published, in the layout units the synthetic pointer is placed in. The tree holds
			// what a screen reader can reach -- buttons, fields, labels, pictures -- so a panel or
			// a stack is not something to aim at, and that is said rather than guessed around.
			Found findWidget(const std::string& target, float& x, float& y, std::string& problem) {
				const auto tree = getMainWindow()->gui().accessibleTree();
				if (!tree) return Found::NotYet;

				std::vector<std::string> ids;
				std::vector<size_t> at;
				for (size_t i = 0; i < tree->nodes.size(); ++i) {
					if (tree->nodes[i].automationId.empty()) continue;
					ids.push_back(tree->nodes[i].automationId);
					at.push_back(i);
				}
				const std::vector<std::string> found = idsMatching(ids, target);
				if (found.empty()) {
					// A screen arriving takes a few frames to be on screen; after that it is wrong.
					if (++mTargetMisses < 40) return Found::NotYet;
					problem = "`" + target + "` is in the layout but there is nothing on screen with that id "
					          "to aim at. Only widgets a screen reader can reach have a place to click (not a "
					          "panel, a stack or a container), and a hidden one has none. On screen: " +
					          listOf(ids, 14);
					return Found::No;
				}
				if (found.size() > 1) {
					problem = refusalFor(ids, target, "the layout on screen");
					return Found::No;
				}
				for (size_t i = 0; i < ids.size(); ++i) {
					if (ids[i] != found.front()) continue;
					const RDA::Rect& box = tree->nodes[at[i]].bounds;
					x = box.x + box.w * 0.5f;
					y = box.y + box.h * 0.5f;
					return Found::Yes;
				}
				return Found::NotYet;
			}

			// --hover and --press given a widget: once it can be found, the pointer is there,
			// as if X,Y had been written. False while still looking, or when it cannot be found.
			bool placePointerOnTarget() {
				if (mOptions.pointerTarget.empty() || mPointer.placed) return true;
				float x = 0.0f, y = 0.0f;
				std::string problem;
				const Found found = findWidget(mOptions.pointerTarget, x, y, problem);
				if (found == Found::No) {
					std::printf("rda: %s\n", problem.c_str());
					refuse();
					return false;
				}
				if (found == Found::NotYet) return false;
				mPointer.placed = true;
				mPointer.x = x;
				mPointer.y = y;
				mPointer.pressed = mOptions.pointerTargetPressed;
				mTargetMisses = 0;
				return true;
			}

			// Held from the first frame, so a hovered widget is hovered by the time
			// anything is captured -- the GUI works out `hot` while it walks, which is
			// after this runs.
			//
			// And parked far outside the window when nothing was asked for. The window is
			// hidden, not absent: the real mouse still has a position inside it, so a shot
			// of a screen nobody is pointing at would hover whatever the pointer happened
			// to be over -- which made the same command produce different pictures on two
			// machines, and on one machine at two moments.
			void holdPointer() {
				const float scrolling = mOptions.scrolling;
				if (mPointer.placed) {
					rendeerSetSyntheticPointer(mPointer.x, mPointer.y, mPointer.pressed);
				} else if (scrolling != 0.0f) {
					// A wheel only reaches what the pointer is over, so asking to scroll and
					// saying nothing about the pointer means the middle of the window -- the
					// alternative was a parked pointer, a wheel nothing heard, and a picture
					// that silently had not scrolled.
					rendeerSetSyntheticPointer(static_cast<float>(mOptions.width > 0 ? mOptions.width : 1280) * 0.5f,
					                           static_cast<float>(mOptions.height > 0 ? mOptions.height : 800) * 0.5f,
					                           false);
				} else {
					rendeerSetSyntheticPointer(-10000.0f, -10000.0f, false);
				}
				// Held until the first picture, so a large number means "to the end". After
				// that it stops, or a sequence would keep scrolling between its frames.
				if (scrolling != 0.0f) rendeerSetSyntheticScroll(mShot.taken == 0 ? scrolling : 0.0f);
			}

			// Whether enough time has passed, and enough stillness, for the picture.
			bool hasWaitedLongEnough(float ms) {
				// The opening moments are thrown away on purpose: a layout's motion starts
				// from nothing and its first frame is a picture of an interface mid-arrival.
				if (mShot.sinceStart < static_cast<float>(mOptions.settleMs)) return false;

				// And then, for a still shot, until nothing is moving any more. Measured in
				// time rather than frames because a window nobody presents is not paced by
				// anything -- a thousand frames here can be a single millisecond of animation.
				if (mOptions.still && mShot.taken == 0) {
					RDA::Gui& gui = getMainWindow()->gui();
					mShot.stillFor = gui.animating() ? 0.0f : (mShot.stillFor + ms);
					// Two frames' worth of quiet, so a transition that has just landed is not
					// caught one frame before its last step.
					if (mShot.stillFor < 32.0f && mShot.sinceStart < 4000.0f) return false;
				}

				// Spaced in real time rather than by frames, for the same reason.
				mShot.waited += ms;
				if (mShot.taken > 0 && mShot.waited < static_cast<float>(mOptions.everyMs)) return false;
				mShot.waited = 0.0f;
				return true;
			}

			void onFrameEnd() {
				if (!mShot.captureNow) return;
				mShot.captureNow = false;
				std::vector<unsigned char> pixels;
				unsigned w = 0, h = 0;
				if (!rendeerCaptureFrame(pixels, w, h)) {
					std::printf("rda: the frame would not come back\n");
					refuse();
					return;
				}

				const fs::path target = pictureName();
				if (!RDA::Png::write(target.string(), pixels.data(), w, h)) {
					std::printf("rda: cannot write %s\n", target.string().c_str());
					refuse();
					return;
				}
				std::printf("rda: %s  (%ux%u)\n", target.string().c_str(), w, h);
				std::fflush(stdout);

				writeTree();

				if (++mShot.taken >= mOptions.frames) rendeerStop();
			}

			// One frame keeps the name it was given; several are numbered, because a
			// sequence that overwrote itself would leave only its last frame.
			fs::path pictureName() const {
				fs::path target = mOptions.out;
				if (mOptions.frames > 1) {
					const fs::path stem = target.stem();
					const fs::path ext = target.extension().empty() ? ".png" : target.extension();
					target = target.parent_path() / (stem.string() + "-" + std::to_string(mShot.taken)
					                                 + ext.string());
				}
				return target;
			}

			// The tree, beside the picture of the same frame.
			void writeTree() const {
				const std::string& file = mOptions.a11yOut;
				if (file.empty()) return;
				const auto tree = getMainWindow()->gui().accessibleTree();
				std::ofstream text(file, std::ios::trunc);
				if (tree && text) {
					text << RDA::formatAccessibleTree(*tree);
					std::printf("rda: %s  (%zu nodes)\n", file.c_str(), tree->nodes.size());
				} else {
					std::printf("rda: no accessibility tree to write to %s\n", file.c_str());
				}
			}

			// What the shot has done so far. Kept here rather than in locals because the
			// callbacks that advance it are called by the loop, one frame at a time.
			struct ShotProgress {
				int   taken = 0;
				int   tick = 0;
				float sinceStart = 0.0f;    // milliseconds the interface has been running
				float waited = 0.0f;        // since the last frame of a sequence
				float stillFor = 0.0f;      // how long nothing has moved
				// Set when it is time, acted on in onFrameEnd: a frame is not finished when
				// onUpdate runs, and the focus ring is one of the things added after it.
				bool  captureNow = false;
			};

			PreviewOptions mOptions;
			bool           mShooting;
			// Where the synthetic pointer is: the one --hover or --press gave it, and wherever
			// the steps have carried it since.
			Pointer        mPointer;

			fs::path                 mRes;
			RDA::Layout::StateSchema mSchema;
			StateWatch               mWatch;
			std::vector<fs::path>    mBlueprints;
			std::string              mThemePath;

			RDA::Layout::Router     mRouter;
			RDA::Layout::LayoutHost mHost;
			bool                    mRouted = false;
			bool                    mRefused = false;
			// Frames spent looking for a widget that is not there yet, before saying it is not.
			int                     mTargetMisses = 0;

			ShotProgress mShot;
		};

		int preview(const Args& args, bool shooting) {
			PreviewOptions options;
			if (!parsePreviewOptions(args, shooting, options)) return 1;
			return PreviewSession(std::move(options), shooting).run();
		}
	}

	int cmdPrototype(const Args& args) { return preview(args, false); }
	int cmdShot(const Args& args)      { return preview(args, true); }
}
