#pragma once
#include <GraphicalObjects/GuiTypes.h>
#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// What a screen reader is told about the interface.
//
// Platform-neutral on purpose: a widget says what it is -- a button called "Save", a
// checkbox that is ticked, a slider at 0.4 of 0..1 -- and the GUI collects that into a tree
// once a frame. Speaking to a particular screen reader is a separate layer that reads the
// tree: UI Automation on Windows (Platform/UiaProvider.cpp), and on any other system
// whatever is written for it later, from the same tree, without a widget changing.
//
// It costs nothing until something asks. The GUI builds the tree only once a platform
// layer has said a client is listening -- on Windows, the first time anything sends the
// window WM_GETOBJECT -- and publishes it only on a frame where it changed.
namespace RDA {

	class Widget;

	// The roles that exist here, not every role an accessibility API has. Named for what
	// they are in this engine; each platform layer maps them onto its own.
	enum class AccessibleRole : uint8_t {
		None,       // not a node: its children hang off whatever is above it
		Window,
		Group,      // a container somebody named with `alt`
		Pane,       // a dock panel
		Text,
		Button,
		CheckBox,
		Slider,
		Edit,       // a single-line field, or a number typed into
		Document,   // a multi-line field
		ComboBox,
		List,
		ListItem,
		Tree,
		TreeItem,
		TabList,
		TabItem,
		Image,
		Menu,       // an open popup
	};

	// What can be done to a node from outside -- which is what a screen reader's user
	// does instead of clicking. Each is applied on the next frame's walk, through the same
	// path a click or a key takes, so nothing done from outside behaves differently.
	enum class AccessibleAction : uint8_t {
		Invoke,     // press it
		Toggle,
		SetValue,   // a string: the text, or a number written as one
		Expand,
		Collapse,
		Select,
		Focus,
		SetSelection,   // a text field's: "start,end" in characters; equal is a caret
		ScrollIntoView, // a row a list holds but is not showing: bring it onto the screen
		// Where several things can be chosen: one more, one fewer, every one, none. The
		// first two are asked of an item, the last two of what holds the items.
		AddToSelection,
		RemoveFromSelection,
		SelectAll,
		ClearSelection,
	};

	enum AccessibleActions : uint32_t {
		kCanInvoke   = 1u << 0,
		kCanToggle   = 1u << 1,
		kCanSetValue = 1u << 2,
		kCanExpand   = 1u << 3,   // and collapse
		kCanSelect   = 1u << 4,
	};

	// One widget's description of itself.
	struct AccessibleInfo {
		AccessibleRole role = AccessibleRole::None;
		std::string    name;
		std::string    value;          // a field's text, a combo box's choice
		bool           focusable = false;
		bool           readOnly = false;
		int8_t         toggled = -1;   // -1 does not toggle, 0 off, 1 on
		int8_t         expanded = -1;  // -1 does not expand, 0 shut, 1 open
		int8_t         selected = -1;  // -1 cannot be selected, 0 no, 1 yes
		// On a list or a tree: more than one of its items can be chosen at once.
		bool           multiSelectable = false;
		// How deep a tree row is, 1 at the top; 0 for anything that is not in a hierarchy.
		// A tree's rows are siblings here, the way a list's are, so this is how a screen
		// reader can say "level 3".
		int            level = 0;
		// A row a list holds but has scrolled out of its view. It is in the tree so a
		// screen reader can count the list and read any of it; it has no widgets, and
		// ScrollIntoView is how one is brought back.
		bool           offscreen = false;
		// Read out when it changes, without the focus going there: 0 not at all, 1 politely
		// -- after whatever is being read -- and 2 at once, interrupting it. What a status
		// line, a toast or an error under a field is.
		uint8_t        live = 0;
		bool           hasRange = false;
		double         rangeMin = 0.0, rangeMax = 0.0, rangeValue = 0.0, rangeStep = 0.0;
		uint32_t       actions = 0;
	};

	// What a screen reader needs to walk a field's text caret by caret: where the caret and
	// the other end of the selection are, where each line starts, and -- for the lines on
	// screen -- where each character is. Everything in characters, not bytes, because that
	// is what both UI Automation and AT-SPI count in; the text itself is the node's value.
	struct AccessibleText {
		bool present = false;
		int  caret = -1;                 // characters; -1 when it is not known
		int  anchor = -1;                // the selection's other end; equal to caret for none
		std::vector<int> lineStarts;     // characters; the first is always 0
		// One line on screen: its first character, where it is, and the left edge of each
		// of its characters plus the right edge of the last -- so `x` has one more entry
		// than the line has characters. Layout units, relative to the window.
		struct Line {
			int   first = 0;
			float y = 0.0f, h = 0.0f;
			std::vector<float> x;
		};
		std::vector<Line> lines;
	};

	// The units a screen reader moves by.
	enum class TextUnit : uint8_t { Character, Word, Line, Paragraph, Document };

	// Moving through a field's text, shared by every platform layer so they agree about what
	// a word is. `text` is the value as codepoints. The next boundary after `at`, or the
	// end; the previous one before it, or the start; and the unit `at` is inside.
	//
	// A word is a run of letters and digits with the spaces after it, a run of punctuation
	// likewise, and -- for Chinese and Japanese, which do not put spaces between words --
	// each Han or kana character on its own: not right, which needs a dictionary, but a
	// step a reader can follow, where treating a whole sentence as one word is not.
	std::vector<uint32_t> textCodepoints(const std::string& utf8);
	std::string           textSlice(const std::vector<uint32_t>& text, int from, int to);
	int  textNext(const std::vector<uint32_t>& text, const AccessibleText& info, TextUnit unit, int at);
	int  textPrevious(const std::vector<uint32_t>& text, const AccessibleText& info, TextUnit unit, int at);
	void textEnclosing(const std::vector<uint32_t>& text, const AccessibleText& info, TextUnit unit,
	                   int at, int& start, int& end);
	// Where character `index` is drawn, or a zero-width box at the end of a line for the
	// position after its last character. False for a character on a line not on screen.
	bool textCharacterBox(const AccessibleText& info, int index, Rect& out);

	// One node of the published tree: the description, where it is, and how it hangs.
	struct AccessibleNode {
		uint32_t       id = 0;          // stable from frame to frame; what a client holds
		int            parent = -1;     // index into the tree; -1 is the window itself
		std::vector<int> children;
		std::string    automationId;    // the widget's path, for test tools
		Rect           bounds{};        // layout units, relative to the window's client area
		bool           focused = false;
		AccessibleInfo info;
		AccessibleText text;             // for a text field; `present` is false otherwise
	};

	struct AccessibleTree {
		std::vector<AccessibleNode> nodes;
		std::vector<int>            roots;     // nodes whose parent is the window
		int                         focused = -1;
		float                       unitScale = 1.0f;  // pixels per layout unit
		std::string                 title;     // the window's
		uint64_t                    version = 0;

		// Every node by its id. Built with the tree, because a list now puts thousands of
		// rows in it and a screen reader asks about one node at a time.
		std::unordered_map<uint32_t, int> byId;

		int find(uint32_t id) const {
			const auto found = byId.find(id);
			return found == byId.end() ? -1 : found->second;
		}
	};

	// The role's name, and the whole tree as indented text -- one node a line, with its
	// states, where it is and its widget path. What `rda shot --a11y` writes, and the way to
	// read the tree on a system no platform layer speaks for yet.
	const char* accessibleRoleName(AccessibleRole role);
	std::string formatAccessibleTree(const AccessibleTree& tree);

	// An action asked for from outside, waiting for the next walk.
	struct AccessibleRequest {
		uint32_t         id = 0;
		AccessibleAction action = AccessibleAction::Invoke;
		std::string      value;
	};

	// What a platform layer needs to know about the window itself, which only the frame
	// loop may ask the window system: where it is on the screen, what it is called, and
	// whether it has the keyboard. UI Automation asks the window directly; AT-SPI has to be
	// told.
	struct AccessibleWindow {
		int         x = 0, y = 0;      // the client area's top left, in screen pixels
		bool        focused = false;
		std::string title;
		bool operator==(const AccessibleWindow& o) const {
			return x == o.x && y == o.y && focused == o.focused && title == o.title;
		}
	};

	// The meeting point between the frame loop and a platform layer, which is called on
	// threads of its own: UI Automation asks from its RPC threads, whenever it likes. The
	// tree is handed over as an immutable snapshot and actions come back as a queue, so
	// neither side ever waits on the other for longer than a pointer copy.
	class AccessibleBridge {
	public:
		// Frame loop: someone is listening, so build the tree.
		bool wanted() const { return mWanted.load(); }
		// Platform layer: someone is listening.
		void want() { mWanted.store(true); }

		void publish(std::shared_ptr<const AccessibleTree> tree) {
			std::lock_guard<std::mutex> held(mLock);
			mTree = std::move(tree);
		}
		std::shared_ptr<const AccessibleTree> tree() const {
			std::lock_guard<std::mutex> held(mLock);
			return mTree;
		}

		void setWindow(const AccessibleWindow& facts) {
			std::lock_guard<std::mutex> held(mLock);
			mWindow = facts;
		}
		AccessibleWindow window() const {
			std::lock_guard<std::mutex> held(mLock);
			return mWindow;
		}

		void request(AccessibleRequest one) {
			std::lock_guard<std::mutex> held(mLock);
			mRequests.push_back(std::move(one));
		}
		std::vector<AccessibleRequest> takeRequests() {
			std::lock_guard<std::mutex> held(mLock);
			std::vector<AccessibleRequest> out;
			out.swap(mRequests);
			return out;
		}
		bool hasRequests() const {
			std::lock_guard<std::mutex> held(mLock);
			return !mRequests.empty();
		}

	private:
		std::atomic<bool> mWanted{ false };
		mutable std::mutex mLock;
		std::shared_ptr<const AccessibleTree> mTree;
		std::vector<AccessibleRequest> mRequests;
		AccessibleWindow mWindow;
	};
}
