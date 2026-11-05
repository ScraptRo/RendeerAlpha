#include <Platform/Accessibility.h>

#if defined(__linux__)

#include <Core/Wake.h>
#include <GraphicalObjects/AccessibleDiff.h>
#include <Logger/Logger.h>

#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// AT-SPI, spoken over D-Bus by hand.
//
// This is what Orca reads, and what every Linux desktop's accessibility tools inspect. A
// toolkit usually reaches it through ATK and GLib; this engine has neither and is not
// going to grow them for this, so it speaks the protocol itself -- the same choice the
// Windows layer makes, and the same shape: a thread of its own answers questions from the
// latest published snapshot, actions go back as a queue the next walk applies, and nothing
// here ever touches a widget.
//
// libdbus is loaded when a window opens rather than linked. Every Linux desktop has it,
// but a build machine without its headers, or a container with no bus at all, should still
// build this engine and still run it; without the library, or without an accessibility
// bus, or with assistive technology switched off, nothing is registered and nothing is
// ever built. The handful of functions used are declared below from libdbus's public,
// stable C interface.
//
// The object tree on the bus:
//   /org/a11y/atspi/accessible/root    the application
//   /org/a11y/atspi/accessible/frame   the window, named by its title
//   /org/a11y/atspi/accessible/n<id>   one node of the tree, by its stable id
namespace RDA {

	namespace {

		// ---- the part of libdbus this uses ------------------------------------------------

		struct DBusConnection;
		struct DBusMessage;
		using dbus_bool_t = uint32_t;

		struct DBusError {
			const char* name;
			const char* message;
			unsigned int dummy1 : 1, dummy2 : 1, dummy3 : 1, dummy4 : 1, dummy5 : 1;
			void* padding1;
		};
		// The public layout, with room to spare: libdbus writes what it declares and no more,
		// and a larger buffer is harmless where a smaller one would not be.
		union DBusMessageIter {
			struct {
				void* dummy1; void* dummy2; uint32_t dummy3;
				int dummy4, dummy5, dummy6, dummy7, dummy8, dummy9, dummy10, dummy11;
				int pad1; void* pad2; void* pad3;
			} real;
			unsigned char room[256];
		};

		enum DBusHandlerResult { kHandled = 0, kNotYetHandled = 1 };
		using UnregisterFn = void (*)(DBusConnection*, void*);
		using MessageFn = DBusHandlerResult (*)(DBusConnection*, DBusMessage*, void*);
		struct DBusObjectPathVTable {
			UnregisterFn unregister_function;
			MessageFn    message_function;
			void (*pad1)(void*); void (*pad2)(void*); void (*pad3)(void*); void (*pad4)(void*);
		};

		constexpr int kTypeInvalid = 0, kTypeBool = 'b', kTypeInt16 = 'n', kTypeInt32 = 'i',
		              kTypeUInt32 = 'u', kTypeDouble = 'd', kTypeString = 's', kTypeObjectPath = 'o',
		              kTypeArray = 'a', kTypeVariant = 'v', kTypeStruct = 'r', kTypeDictEntry = 'e';
		constexpr int kMethodCall = 1;
		constexpr int kSessionBus = 0;

		struct Dbus {
			void* handle = nullptr;
			dbus_bool_t (*threads_init_default)();
			void (*error_init)(DBusError*);
			void (*error_free)(DBusError*);
			dbus_bool_t (*error_is_set)(const DBusError*);
			DBusConnection* (*bus_get_private)(int, DBusError*);
			DBusConnection* (*connection_open_private)(const char*, DBusError*);
			dbus_bool_t (*bus_register)(DBusConnection*, DBusError*);
			const char* (*bus_get_unique_name)(DBusConnection*);
			void (*connection_close)(DBusConnection*);
			void (*connection_unref)(DBusConnection*);
			void (*connection_set_exit_on_disconnect)(DBusConnection*, dbus_bool_t);
			dbus_bool_t (*connection_register_fallback)(DBusConnection*, const char*,
			                                            const DBusObjectPathVTable*, void*);
			dbus_bool_t (*connection_read_write_dispatch)(DBusConnection*, int);
			dbus_bool_t (*connection_send)(DBusConnection*, DBusMessage*, uint32_t*);
			void (*connection_flush)(DBusConnection*);
			DBusMessage* (*connection_send_with_reply_and_block)(DBusConnection*, DBusMessage*,
			                                                     int, DBusError*);
			DBusMessage* (*message_new_method_call)(const char*, const char*, const char*, const char*);
			DBusMessage* (*message_new_method_return)(DBusMessage*);
			DBusMessage* (*message_new_error)(DBusMessage*, const char*, const char*);
			DBusMessage* (*message_new_signal)(const char*, const char*, const char*);
			void (*message_unref)(DBusMessage*);
			int (*message_get_type)(DBusMessage*);
			const char* (*message_get_path)(DBusMessage*);
			const char* (*message_get_interface)(DBusMessage*);
			const char* (*message_get_member)(DBusMessage*);
			const char* (*message_get_sender)(DBusMessage*);
			dbus_bool_t (*message_iter_init)(DBusMessage*, DBusMessageIter*);
			int (*message_iter_get_arg_type)(DBusMessageIter*);
			void (*message_iter_get_basic)(DBusMessageIter*, void*);
			void (*message_iter_recurse)(DBusMessageIter*, DBusMessageIter*);
			dbus_bool_t (*message_iter_next)(DBusMessageIter*);
			void (*message_iter_init_append)(DBusMessage*, DBusMessageIter*);
			dbus_bool_t (*message_iter_append_basic)(DBusMessageIter*, int, const void*);
			dbus_bool_t (*message_iter_open_container)(DBusMessageIter*, int, const char*, DBusMessageIter*);
			dbus_bool_t (*message_iter_close_container)(DBusMessageIter*, DBusMessageIter*);

			bool load() {
				handle = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
				if (!handle) return false;
				bool ok = true;
				auto get = [&](auto& fn, const char* name) {
					fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(handle, name));
					if (!fn) ok = false;
				};
				get(threads_init_default, "dbus_threads_init_default");
				get(error_init, "dbus_error_init");
				get(error_free, "dbus_error_free");
				get(error_is_set, "dbus_error_is_set");
				get(bus_get_private, "dbus_bus_get_private");
				get(connection_open_private, "dbus_connection_open_private");
				get(bus_register, "dbus_bus_register");
				get(bus_get_unique_name, "dbus_bus_get_unique_name");
				get(connection_close, "dbus_connection_close");
				get(connection_unref, "dbus_connection_unref");
				get(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
				get(connection_register_fallback, "dbus_connection_register_fallback");
				get(connection_read_write_dispatch, "dbus_connection_read_write_dispatch");
				get(connection_send, "dbus_connection_send");
				get(connection_flush, "dbus_connection_flush");
				get(connection_send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
				get(message_new_method_call, "dbus_message_new_method_call");
				get(message_new_method_return, "dbus_message_new_method_return");
				get(message_new_error, "dbus_message_new_error");
				get(message_new_signal, "dbus_message_new_signal");
				get(message_unref, "dbus_message_unref");
				get(message_get_type, "dbus_message_get_type");
				get(message_get_path, "dbus_message_get_path");
				get(message_get_interface, "dbus_message_get_interface");
				get(message_get_member, "dbus_message_get_member");
				get(message_get_sender, "dbus_message_get_sender");
				get(message_iter_init, "dbus_message_iter_init");
				get(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
				get(message_iter_get_basic, "dbus_message_iter_get_basic");
				get(message_iter_recurse, "dbus_message_iter_recurse");
				get(message_iter_next, "dbus_message_iter_next");
				get(message_iter_init_append, "dbus_message_iter_init_append");
				get(message_iter_append_basic, "dbus_message_iter_append_basic");
				get(message_iter_open_container, "dbus_message_iter_open_container");
				get(message_iter_close_container, "dbus_message_iter_close_container");
				if (!ok) { dlclose(handle); handle = nullptr; }
				return ok;
			}
		};

		Dbus& dbus() {
			static Dbus loaded;
			static const bool ready = [] {
				if (!loaded.load()) return false;
				loaded.threads_init_default();
				return true;
			}();
			(void)ready;
			return loaded;
		}

		// ---- writing a message ------------------------------------------------------------

		// Appending, one container at a time. Every open is matched by a close in the same
		// scope, which is what keeps a reply well formed whatever path a handler takes.
		class Out {
		public:
			explicit Out(DBusMessage* message) { dbus().message_iter_init_append(message, &mIter); }
			Out(DBusMessageIter* parent, int type, const char* signature) : mParent(parent) {
				dbus().message_iter_open_container(parent, type, signature, &mIter);
			}
			~Out() { if (mParent) dbus().message_iter_close_container(mParent, &mIter); }
			Out(const Out&) = delete;
			Out& operator=(const Out&) = delete;

			DBusMessageIter* iter() { return &mIter; }
			void str(const std::string& s) { const char* p = s.c_str(); dbus().message_iter_append_basic(&mIter, kTypeString, &p); }
			void path(const std::string& s) { const char* p = s.c_str(); dbus().message_iter_append_basic(&mIter, kTypeObjectPath, &p); }
			void i32(int32_t v) { dbus().message_iter_append_basic(&mIter, kTypeInt32, &v); }
			void u32(uint32_t v) { dbus().message_iter_append_basic(&mIter, kTypeUInt32, &v); }
			void i16(int16_t v) { dbus().message_iter_append_basic(&mIter, kTypeInt16, &v); }
			void boolean(bool v) { dbus_bool_t b = v ? 1 : 0; dbus().message_iter_append_basic(&mIter, kTypeBool, &b); }
			void dbl(double v) { dbus().message_iter_append_basic(&mIter, kTypeDouble, &v); }

		private:
			DBusMessageIter* mParent = nullptr;
			DBusMessageIter  mIter{};
		};

		// ---- reading one --------------------------------------------------------------------

		class In {
		public:
			explicit In(DBusMessage* message) { mHas = dbus().message_iter_init(message, &mIter) != 0; }
			explicit In(DBusMessageIter* parent) { dbus().message_iter_recurse(parent, &mIter); mHas = true; }
			int type() { return mHas ? dbus().message_iter_get_arg_type(&mIter) : kTypeInvalid; }
			std::string str() {
				std::string out;
				if (type() == kTypeString || type() == kTypeObjectPath) {
					const char* p = nullptr;
					dbus().message_iter_get_basic(&mIter, &p);
					if (p) out = p;
				}
				next();
				return out;
			}
			int32_t i32() {
				int32_t v = 0;
				if (type() == kTypeInt32) dbus().message_iter_get_basic(&mIter, &v);
				else if (type() == kTypeUInt32) { uint32_t u = 0; dbus().message_iter_get_basic(&mIter, &u); v = static_cast<int32_t>(u); }
				next();
				return v;
			}
			uint32_t u32() { return static_cast<uint32_t>(i32()); }
			// A variant holding a number or a string, which is everything Properties.Set is
			// sent here.
			bool variant(double& number, std::string& text) {
				if (type() != kTypeVariant) return false;
				DBusMessageIter inner;
				dbus().message_iter_recurse(&mIter, &inner);
				const int t = dbus().message_iter_get_arg_type(&inner);
				bool ok = true;
				if (t == kTypeDouble) dbus().message_iter_get_basic(&inner, &number);
				else if (t == kTypeInt32) { int32_t v = 0; dbus().message_iter_get_basic(&inner, &v); number = v; }
				else if (t == kTypeString) { const char* p = nullptr; dbus().message_iter_get_basic(&inner, &p); text = p ? p : ""; }
				else ok = false;
				next();
				return ok;
			}
			void next() { if (mHas) mHas = dbus().message_iter_next(&mIter) != 0; }

		private:
			DBusMessageIter mIter{};
			bool mHas = false;
		};

		// ---- what AT-SPI calls things -------------------------------------------------------
		//
		// Values read out of libatspi's own enum registry rather than remembered: the list in
		// the header has grown in the middle more than once.
		constexpr uint32_t kRoleApplication = 75, kRoleFrame = 23, kRolePanel = 39, kRoleLabel = 29,
		                   kRolePushButton = 43, kRoleCheckBox = 7, kRoleSlider = 51, kRoleEntry = 79,
		                   kRoleText = 61, kRoleComboBox = 11, kRoleList = 31, kRoleListItem = 32,
		                   kRoleTree = 65, kRoleTreeItem = 91, kRolePageTabList = 38, kRolePageTab = 37,
		                   kRoleImage = 27, kRolePopupMenu = 41, kRoleGrouping = 99;
		constexpr uint32_t kStateActive = 1, kStateChecked = 4, kStateCollapsed = 5, kStateEditable = 7,
		                   kStateEnabled = 8, kStateExpandable = 9, kStateExpanded = 10,
		                   kStateFocusable = 11, kStateFocused = 12, kStateMultiLine = 17,
		                   kStateMultiSelectable = 18,
		                   kStateSelectable = 22, kStateSelected = 23, kStateSensitive = 24,
		                   kStateShowing = 25, kStateSingleLine = 26, kStateVisible = 30,
		                   kStateCheckable = 41, kStateReadOnly = 43;

		uint32_t roleOf(AccessibleRole role) {
			switch (role) {
			case AccessibleRole::Window:   return kRoleFrame;
			case AccessibleRole::Group:    return kRoleGrouping;
			case AccessibleRole::Pane:     return kRolePanel;
			case AccessibleRole::Text:     return kRoleLabel;
			case AccessibleRole::Button:   return kRolePushButton;
			case AccessibleRole::CheckBox: return kRoleCheckBox;
			case AccessibleRole::Slider:   return kRoleSlider;
			case AccessibleRole::Edit:     return kRoleEntry;
			case AccessibleRole::Document: return kRoleText;
			case AccessibleRole::ComboBox: return kRoleComboBox;
			case AccessibleRole::List:     return kRoleList;
			case AccessibleRole::ListItem: return kRoleListItem;
			case AccessibleRole::Tree:     return kRoleTree;
			case AccessibleRole::TreeItem: return kRoleTreeItem;
			case AccessibleRole::TabList:  return kRolePageTabList;
			case AccessibleRole::TabItem:  return kRolePageTab;
			case AccessibleRole::Image:    return kRoleImage;
			case AccessibleRole::Menu:     return kRolePopupMenu;
			default:                       return kRolePanel;
			}
		}

		const char* roleNameOf(uint32_t role) {
			switch (role) {
			case kRoleApplication: return "application";
			case kRoleFrame:       return "frame";
			case kRolePanel:       return "panel";
			case kRoleLabel:       return "label";
			case kRolePushButton:  return "push button";
			case kRoleCheckBox:    return "check box";
			case kRoleSlider:      return "slider";
			case kRoleEntry:       return "entry";
			case kRoleText:        return "text";
			case kRoleComboBox:    return "combo box";
			case kRoleList:        return "list";
			case kRoleListItem:    return "list item";
			case kRoleTree:        return "tree";
			case kRoleTreeItem:    return "tree item";
			case kRolePageTabList: return "page tab list";
			case kRolePageTab:     return "page tab";
			case kRoleImage:       return "image";
			case kRolePopupMenu:   return "popup menu";
			case kRoleGrouping:    return "grouping";
			default:               return "unknown";
			}
		}

		constexpr const char* kPrefix = "/org/a11y/atspi/accessible";
		constexpr const char* kRootPath = "/org/a11y/atspi/accessible/root";
		constexpr const char* kFramePath = "/org/a11y/atspi/accessible/frame";
		constexpr const char* kNullPath = "/org/a11y/atspi/null";
		constexpr const char* kAccessible = "org.a11y.atspi.Accessible";
		constexpr const char* kComponent = "org.a11y.atspi.Component";
		constexpr const char* kAction = "org.a11y.atspi.Action";
		constexpr const char* kValue = "org.a11y.atspi.Value";
		constexpr const char* kText = "org.a11y.atspi.Text";
		constexpr const char* kEditableText = "org.a11y.atspi.EditableText";
		constexpr const char* kSelection = "org.a11y.atspi.Selection";
		constexpr const char* kApplication = "org.a11y.atspi.Application";
		constexpr const char* kProperties = "org.freedesktop.DBus.Properties";

		std::string nodePath(uint32_t id) { return std::string(kPrefix) + "/n" + std::to_string(id); }

		// One action a node offers, in the order AT-SPI numbers them. The first is what a
		// screen reader means by "activate this".
		struct Offered { const char* name; AccessibleAction action; };
		std::vector<Offered> actionsOf(const AccessibleInfo& info) {
			std::vector<Offered> out;
			if (info.toggled >= 0 && (info.actions & kCanToggle)) out.push_back({ "toggle", AccessibleAction::Toggle });
			else if (info.actions & kCanInvoke) out.push_back({ "click", AccessibleAction::Invoke });
			if (info.actions & kCanSelect) out.push_back({ "select", AccessibleAction::Select });
			if (info.actions & kCanExpand) {
				if (info.expanded > 0) out.push_back({ "collapse", AccessibleAction::Collapse });
				else                   out.push_back({ "expand", AccessibleAction::Expand });
			}
			return out;
		}

		bool hasText(const AccessibleInfo& info) {
			return info.role == AccessibleRole::Edit || info.role == AccessibleRole::Document;
		}
		bool hasSelection(const AccessibleInfo& info) {
			return info.role == AccessibleRole::TabList || info.role == AccessibleRole::Tree ||
			       info.role == AccessibleRole::List;
		}

		// Characters, not bytes: AT-SPI counts text in characters.
		size_t utf8Length(const std::string& s) {
			size_t n = 0;
			for (const unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
			return n;
		}
		// ---- an event waiting to go out -----------------------------------------------------

		struct Pending {
			std::string path;
			std::string iface;       // org.a11y.atspi.Event.Object or .Window
			std::string member;      // StateChanged, PropertyChange, ChildrenChanged, Activate...
			std::string detail;      // "focused", "accessible-value"...
			int32_t     detail1 = 0;
			int32_t     detail2 = 0;
			char        kind = 'i';  // what the variant holds: i, d, s, or r for a reference
			int32_t     number = 0;
			double      real = 0.0;
			std::string text;        // a string, or for 'r' the referenced path
		};

		// ---- the host --------------------------------------------------------------------------

		class AtspiHost final : public AccessibilityHost {
		public:
			AtspiHost(std::shared_ptr<AccessibleBridge> bridge, DBusConnection* bus, std::string address)
				: mBridge(std::move(bridge)), mBus(bus), mAddress(std::move(address)) {
				mName = dbus().bus_get_unique_name(mBus);
				static const DBusObjectPathVTable vtable = { nullptr, &AtspiHost::onMessage,
				                                             nullptr, nullptr, nullptr, nullptr };
				dbus().connection_register_fallback(mBus, kPrefix, &vtable, this);
				embed();
				mThread = std::thread([this] { run(); });
			}

			~AtspiHost() override {
				mStop.store(true);
				if (mThread.joinable()) mThread.join();
				// Out of the registry before the connection goes, so no client is left
				// holding a reference to an application that is not there.
				DBusMessage* call = dbus().message_new_method_call("org.a11y.atspi.Registry", kRootPath,
				                                                   "org.a11y.atspi.Socket", "Unembed");
				if (call) {
					Out args(call);
					{ Out ref(args.iter(), kTypeStruct, nullptr); ref.str(mName); ref.path(kRootPath); }
					dbus().connection_send(mBus, call, nullptr);
					dbus().connection_flush(mBus);
					dbus().message_unref(call);
				}
				dbus().connection_close(mBus);
				dbus().connection_unref(mBus);
			}

			void frameDone() override {
				const AccessibleWindow window = mBridge->window();
				std::vector<Pending> out;
				// The window gaining and losing the keyboard, which is how a screen reader
				// knows which application it is in. Said whether or not anything has asked for
				// the tree yet: it is what makes a screen reader ask.
				if (window.focused != mWindowFocused) {
					mWindowFocused = window.focused;
					Pending p;
					p.path = kFramePath;
					p.iface = "org.a11y.atspi.Event.Window";
					p.member = window.focused ? "Activate" : "Deactivate";
					p.kind = 's';
					p.text = window.title;
					out.push_back(std::move(p));
					Pending s;
					s.path = kFramePath;
					s.iface = "org.a11y.atspi.Event.Object";
					s.member = "StateChanged";
					s.detail = "active";
					s.detail1 = window.focused ? 1 : 0;
					out.push_back(std::move(s));
				}

				if (mBridge->wanted()) {
					const auto tree = mBridge->tree();
					if (tree && tree != mLast) {
						diff(mLast, tree, out);
						mLast = tree;
					}
				}
				if (out.empty()) return;
				std::lock_guard<std::mutex> held(mPendingLock);
				for (Pending& one : out) mPending.push_back(std::move(one));
			}

		private:
			std::shared_ptr<AccessibleBridge> mBridge;
			DBusConnection* mBus = nullptr;
			std::string mAddress;
			std::string mName;               // our unique name on the accessibility bus
			std::string mParentName;         // the registry's desktop, which we hang off
			std::string mParentPath = kNullPath;
			// The registry's own name. It reads a little of every application it is given --
			// its name, its children -- and that is not anybody listening, so its questions
			// are answered without turning the tree on.
			std::string mRegistry;
			int32_t     mAppId = 0;          // the registry sets this through Properties
			std::thread mThread;
			std::atomic<bool> mStop{ false };

			std::mutex mPendingLock;
			std::vector<Pending> mPending;

			// Loop-thread state, for working out what changed.
			std::shared_ptr<const AccessibleTree> mLast;
			uint32_t mFocused = 0;
			bool     mWindowFocused = false;

			// ---- registration ----

			void embed() {
				DBusMessage* call = dbus().message_new_method_call("org.a11y.atspi.Registry", kRootPath,
				                                                   "org.a11y.atspi.Socket", "Embed");
				if (!call) return;
				{
					Out args(call);
					Out ref(args.iter(), kTypeStruct, nullptr);
					ref.str(mName);
					ref.path(kRootPath);
				}
				DBusError error;
				dbus().error_init(&error);
				DBusMessage* reply = dbus().connection_send_with_reply_and_block(mBus, call, 2000, &error);
				dbus().message_unref(call);
				if (!reply) {
					RDA_LOG_WARNING("accessibility: the AT-SPI registry did not answer ("
					                << (error.message ? error.message : "no reason") << ")");
					dbus().error_free(&error);
					return;
				}
				if (const char* from = dbus().message_get_sender(reply)) mRegistry = from;
				// (so): the desktop we are now a child of.
				{
					DBusMessageIter top;
					if (dbus().message_iter_init(reply, &top) &&
					    dbus().message_iter_get_arg_type(&top) == kTypeStruct) {
						In ref(&top);
						mParentName = ref.str();
						mParentPath = ref.str();
					}
				}
				dbus().message_unref(reply);
				RDA_LOG_INFO("accessibility: registered with AT-SPI as " << mName);
			}

			// ---- the dispatch thread ----

			void run() {
				while (!mStop.load()) {
					// A tenth of a second at most between looking for events to send and
					// noticing it is time to stop, which is all the latency either has.
					dbus().connection_read_write_dispatch(mBus, 100);
					std::vector<Pending> sending;
					{
						std::lock_guard<std::mutex> held(mPendingLock);
						sending.swap(mPending);
					}
					for (const Pending& one : sending) emit(one);
					if (!sending.empty()) dbus().connection_flush(mBus);
				}
			}

			void emit(const Pending& one) {
				DBusMessage* signal = dbus().message_new_signal(one.path.c_str(), one.iface.c_str(),
				                                                one.member.c_str());
				if (!signal) return;
				{
					Out args(signal);
					args.str(one.detail);
					args.i32(one.detail1);
					args.i32(one.detail2);
					{
						const char* sig = one.kind == 'd' ? "d" : one.kind == 's' ? "s"
						                : one.kind == 'r' ? "(so)" : "i";
						Out v(args.iter(), kTypeVariant, sig);
						if (one.kind == 'd') v.dbl(one.real);
						else if (one.kind == 's') v.str(one.text);
						else if (one.kind == 'r') { Out ref(v.iter(), kTypeStruct, nullptr); ref.str(mName); ref.path(one.text); }
						else v.i32(one.number);
					}
					{ Out props(args.iter(), kTypeArray, "{sv}"); }
				}
				dbus().connection_send(mBus, signal, nullptr);
				dbus().message_unref(signal);
			}

			// ---- working out what changed (loop thread) ----

			// AT-SPI's announcement: the text, and how urgently -- 1 polite, 2 assertive, the
			// same numbers its live-politeness enum uses. What Orca reads for a live region.
			void announce(std::vector<Pending>& out, const AccessibleNode& node) {
				const std::string& said = node.info.value.empty() ? node.info.name : node.info.value;
				if (said.empty()) return;
				Pending p;
				p.path = nodePath(node.id);
				p.iface = "org.a11y.atspi.Event.Object";
				p.member = "Announcement";
				p.detail1 = node.info.live;
				p.kind = 's';
				p.text = said;
				out.push_back(std::move(p));
			}

			void state(std::vector<Pending>& out, uint32_t id, const char* what, bool on) {
				Pending p;
				p.path = nodePath(id);
				p.iface = "org.a11y.atspi.Event.Object";
				p.member = "StateChanged";
				p.detail = what;
				p.detail1 = on ? 1 : 0;
				out.push_back(std::move(p));
			}

			void diff(const std::shared_ptr<const AccessibleTree>& before,
			          const std::shared_ptr<const AccessibleTree>& now, std::vector<Pending>& out) {
				// Nodes that came and went, told to their parents -- which is what makes a
				// client drop what it remembered of them. Bounded: a screen that was replaced
				// wholesale is one notice on the window, not a thousand.
				const AccessibleDiff delta = diffAccessibleTrees(before.get(), *now);
				std::vector<Pending> changes;
				auto childChange = [&](const AccessibleTree& tree, const AccessibleNode& node, const char* how) {
					Pending p;
					p.path = node.parent >= 0 ? nodePath(tree.nodes[static_cast<size_t>(node.parent)].id) : kFramePath;
					p.iface = "org.a11y.atspi.Event.Object";
					p.member = "ChildrenChanged";
					p.detail = how;
					p.kind = 'r';
					p.text = nodePath(node.id);
					changes.push_back(std::move(p));
				};
				for (const AccessibleNode* gone : delta.removed) childChange(*before, *gone, "remove");
				for (const AccessibleChange& change : delta.changes) {
					if (change.before) continue;
					childChange(*now, *change.now, "add");
					// A live region that has just appeared saying something is read out.
					if (before && change.now->info.live) announce(out, *change.now);
				}
				if (changes.size() <= 64) {
					for (Pending& p : changes) out.push_back(std::move(p));
				} else {
					Pending p;
					p.path = kFramePath;
					p.iface = "org.a11y.atspi.Event.Object";
					p.member = "ChildrenChanged";
					p.detail = "add";
					p.kind = 'r';
					p.text = kFramePath;
					out.push_back(std::move(p));
				}

				// What changed on the nodes that stayed.
				for (const AccessibleChange& change : delta.changes) {
					if (!change.before) continue;
					const AccessibleNode& node = *change.now;
					const AccessibleInfo& a = change.before->info;
					const AccessibleInfo& b = node.info;
					if (change.toggled) state(out, node.id, "checked", b.toggled > 0);
					if (change.expanded) state(out, node.id, "expanded", b.expanded > 0);
					if (change.selected) state(out, node.id, "selected", b.selected > 0);
					if (b.live && (change.name || change.value)) announce(out, node);
					if (change.name) {
						Pending p;
						p.path = nodePath(node.id);
						p.iface = "org.a11y.atspi.Event.Object";
						p.member = "PropertyChange";
						p.detail = "accessible-name";
						p.kind = 's';
						p.text = b.name;
						out.push_back(std::move(p));
					}
					// What was typed or deleted, and where the caret went -- the two a screen
					// reader reads a field by. One deletion and one insertion: whatever lies
					// between the unchanged start and the unchanged end.
					const AccessibleText& tb = node.text;
					if (tb.present && change.value) {
						const std::vector<uint32_t> was = textCodepoints(a.value);
						const std::vector<uint32_t> is = textCodepoints(b.value);
						size_t head = 0;
						while (head < was.size() && head < is.size() && was[head] == is[head]) ++head;
						size_t tail = 0;
						while (tail < was.size() - head && tail < is.size() - head &&
						       was[was.size() - 1 - tail] == is[is.size() - 1 - tail]) ++tail;
						auto textChange = [&](const char* how, const std::vector<uint32_t>& from, size_t length) {
							Pending p;
							p.path = nodePath(node.id);
							p.iface = "org.a11y.atspi.Event.Object";
							p.member = "TextChanged";
							p.detail = how;
							p.detail1 = static_cast<int32_t>(head);
							p.detail2 = static_cast<int32_t>(length);
							p.kind = 's';
							p.text = textSlice(from, static_cast<int>(head), static_cast<int>(head + length));
							out.push_back(std::move(p));
						};
						if (was.size() - head - tail > 0) textChange("delete", was, was.size() - head - tail);
						if (is.size() - head - tail > 0) textChange("insert", is, is.size() - head - tail);
					}
					if (change.caret) {
						Pending p;
						p.path = nodePath(node.id);
						p.iface = "org.a11y.atspi.Event.Object";
						p.member = "TextCaretMoved";
						p.detail1 = (std::max)(0, tb.caret);
						out.push_back(std::move(p));
					}
					if (change.anchor || (change.caret && tb.anchor != tb.caret)) {
						Pending p;
						p.path = nodePath(node.id);
						p.iface = "org.a11y.atspi.Event.Object";
						p.member = "TextSelectionChanged";
						out.push_back(std::move(p));
					}
					if (change.range || change.value) {
						Pending p;
						p.path = nodePath(node.id);
						p.iface = "org.a11y.atspi.Event.Object";
						p.member = "PropertyChange";
						p.detail = "accessible-value";
						p.kind = 'd';
						p.real = b.rangeValue;
						out.push_back(std::move(p));
					}
				}

				// Where the keyboard went, last, so a client that reads the focused node reads
				// it as it now is.
				if (delta.focused != mFocused) {
					if (mFocused && now->find(mFocused) >= 0) state(out, mFocused, "focused", false);
					mFocused = delta.focused;
					if (mFocused) state(out, mFocused, "focused", true);
				}
			}

			// ---- answering (dispatch thread) ----

			// Which object a path names: the application, the window, or a node.
			enum class Kind { Root, Frame, Node, None };
			struct Target {
				Kind kind = Kind::None;
				std::shared_ptr<const AccessibleTree> tree;
				const AccessibleNode* node = nullptr;
				int index = -1;
			};

			Target resolve(const char* path) {
				Target t;
				if (!path) return t;
				t.tree = mBridge->tree();
				if (std::strcmp(path, kRootPath) == 0) { t.kind = Kind::Root; return t; }
				if (std::strcmp(path, kFramePath) == 0) { t.kind = Kind::Frame; return t; }
				const std::string prefix = std::string(kPrefix) + "/n";
				if (std::strncmp(path, prefix.c_str(), prefix.size()) != 0 || !t.tree) return t;
				const uint32_t id = static_cast<uint32_t>(std::strtoul(path + prefix.size(), nullptr, 10));
				t.index = t.tree->find(id);
				if (t.index < 0) return t;
				t.node = &t.tree->nodes[static_cast<size_t>(t.index)];
				t.kind = Kind::Node;
				return t;
			}

			void ref(Out& into, const std::string& name, const std::string& path) {
				Out r(into.iter(), kTypeStruct, nullptr);
				r.str(name);
				r.path(path);
			}
			void refNode(Out& into, const AccessibleTree& tree, int index) {
				ref(into, mName, nodePath(tree.nodes[static_cast<size_t>(index)].id));
			}

			const std::vector<int>* childrenOf(const Target& t) {
				static const std::vector<int> none;
				if (t.kind == Kind::Frame) return t.tree ? &t.tree->roots : &none;
				if (t.kind == Kind::Node) return &t.node->children;
				return &none;
			}

			std::string nameOf(const Target& t) {
				if (t.kind == Kind::Root || t.kind == Kind::Frame) return mBridge->window().title;
				return t.node ? t.node->info.name : std::string();
			}

			uint32_t roleOfTarget(const Target& t) {
				if (t.kind == Kind::Root) return kRoleApplication;
				if (t.kind == Kind::Frame) return kRoleFrame;
				return t.node ? roleOf(t.node->info.role) : kRolePanel;
			}

			void parentOf(Out& into, const Target& t) {
				if (t.kind == Kind::Root) { ref(into, mParentName, mParentPath); return; }
				if (t.kind == Kind::Frame || (t.node && t.node->parent < 0)) {
					ref(into, mName, t.kind == Kind::Frame ? kRootPath : kFramePath);
					return;
				}
				if (t.node) { refNode(into, *t.tree, t.node->parent); return; }
				ref(into, "", kNullPath);
			}

			int indexInParent(const Target& t) {
				if (t.kind == Kind::Frame) return 0;
				if (t.kind != Kind::Node) return -1;
				const std::vector<int>& row = t.node->parent >= 0
					? t.tree->nodes[static_cast<size_t>(t.node->parent)].children : t.tree->roots;
				for (size_t i = 0; i < row.size(); ++i) if (row[i] == t.index) return static_cast<int>(i);
				return -1;
			}

			void states(Out& into, const Target& t) {
				uint64_t bits = 0;
				auto set = [&](uint32_t s) { bits |= (uint64_t{ 1 } << s); };
				const AccessibleWindow window = mBridge->window();
				set(kStateEnabled); set(kStateSensitive);
				// A row scrolled out of its list's view is there to be read, not seen.
				if (t.kind != Kind::Node || !t.node->info.offscreen) { set(kStateShowing); set(kStateVisible); }
				if (t.kind == Kind::Frame && window.focused) set(kStateActive);
				if (t.kind == Kind::Node) {
					const AccessibleInfo& info = t.node->info;
					if (info.focusable) set(kStateFocusable);
					if (t.node->focused && window.focused) set(kStateFocused);
					if (info.toggled >= 0) { set(kStateCheckable); if (info.toggled > 0) set(kStateChecked); }
					if (info.expanded >= 0) { set(kStateExpandable); set(info.expanded > 0 ? kStateExpanded : kStateCollapsed); }
					if (info.selected >= 0) { set(kStateSelectable); if (info.selected > 0) set(kStateSelected); }
					if (info.role == AccessibleRole::Edit) set(kStateSingleLine);
					if (info.role == AccessibleRole::Document) set(kStateMultiLine);
					if (info.multiSelectable) set(kStateMultiSelectable);
					if (hasText(info)) {
						if (info.readOnly) set(kStateReadOnly);
						else set(kStateEditable);
					}
				}
				Out array(into.iter(), kTypeArray, "u");
				array.u32(static_cast<uint32_t>(bits & 0xFFFFFFFFu));
				array.u32(static_cast<uint32_t>(bits >> 32));
			}

			std::vector<const char*> interfacesOf(const Target& t) {
				std::vector<const char*> out{ kAccessible };
				if (t.kind == Kind::Root) { out.push_back(kApplication); return out; }
				out.push_back(kComponent);
				if (t.kind != Kind::Node) return out;
				const AccessibleInfo& info = t.node->info;
				if (!actionsOf(info).empty()) out.push_back(kAction);
				if (info.hasRange) out.push_back(kValue);
				if (hasText(info)) { out.push_back(kText); if (!info.readOnly) out.push_back(kEditableText); }
				if (hasSelection(info)) out.push_back(kSelection);
				return out;
			}

			// Bounds in pixels: on the screen (coordinate type 0) or in the window (1, 2).
			void extents(const Target& t, uint32_t coords, int& x, int& y, int& w, int& h) {
				x = y = w = h = 0;
				const float k = (t.tree && t.tree->unitScale > 0.0f) ? t.tree->unitScale : 1.0f;
				const AccessibleWindow window = mBridge->window();
				if (t.kind == Kind::Node) {
					x = static_cast<int>(t.node->bounds.x * k);
					y = static_cast<int>(t.node->bounds.y * k);
					w = static_cast<int>(t.node->bounds.w * k);
					h = static_cast<int>(t.node->bounds.h * k);
				} else if (t.tree) {
					// The window: as big as everything in it, from its own corner.
					for (const AccessibleNode& n : t.tree->nodes) {
						w = std::max(w, static_cast<int>((n.bounds.x + n.bounds.w) * k));
						h = std::max(h, static_cast<int>((n.bounds.y + n.bounds.h) * k));
					}
				}
				if (coords == 0) { x += window.x; y += window.y; }
			}

			void request(const Target& t, AccessibleAction action, std::string value = {}) {
				if (t.kind != Kind::Node) return;
				AccessibleRequest one;
				one.id = t.node->id;
				one.action = action;
				one.value = std::move(value);
				mBridge->request(std::move(one));
				RDA::Wake::requestFrame();
			}

			// A property, as the variant Properties.Get answers with. False for one this
			// object does not have.
			bool property(Out& into, const Target& t, const std::string& iface, const std::string& name) {
				// One variant, opened and closed around whatever fills it.
				auto put = [&](const char* signature, auto&& fill) {
					Out v(into.iter(), kTypeVariant, signature);
					fill(v);
					return true;
				};
				auto text = [&](const std::string& value) { return put("s", [&](Out& v) { v.str(value); }); };
				auto integer = [&](int32_t value) { return put("i", [&](Out& v) { v.i32(value); }); };
				auto real = [&](double value) { return put("d", [&](Out& v) { v.dbl(value); }); };

				if (iface == kAccessible) {
					if (name == "Name") return text(nameOf(t));
					if (name == "Description" || name == "HelpText" || name == "Locale") return text("");
					if (name == "AccessibleId") return text(t.node ? t.node->automationId : std::string());
					if (name == "Parent") return put("(so)", [&](Out& v) { parentOf(v, t); });
					if (name == "ChildCount")
						return integer(t.kind == Kind::Root ? 1 : static_cast<int32_t>(childrenOf(t)->size()));
				}
				if (iface == kApplication && t.kind == Kind::Root) {
					if (name == "ToolkitName") return text("RendeerAlpha");
					if (name == "Version") return text("1");
					if (name == "AtspiVersion") return text("2.1");
					if (name == "Id") return integer(mAppId);
				}
				if (t.kind != Kind::Node) return false;
				const AccessibleInfo& info = t.node->info;
				if (iface == kValue && info.hasRange) {
					if (name == "CurrentValue") return real(info.rangeValue);
					if (name == "MinimumValue") return real(info.rangeMin);
					if (name == "MaximumValue") return real(info.rangeMax);
					if (name == "MinimumIncrement") return real(info.rangeStep);
					if (name == "Text") return text(info.value);
				}
				if (iface == kText && hasText(info)) {
					if (name == "CharacterCount") return integer(static_cast<int32_t>(utf8Length(info.value)));
					if (name == "CaretOffset") return integer((std::max)(0, t.node->text.caret));
				}
				if (iface == kAction && name == "NActions") return integer(static_cast<int32_t>(actionsOf(info).size()));
				if (iface == kSelection && name == "NSelectedChildren") {
					int n = 0;
					for (const int c : t.node->children) if (t.tree->nodes[static_cast<size_t>(c)].info.selected > 0) ++n;
					return integer(n);
				}
				return false;
			}

			static DBusHandlerResult onMessage(DBusConnection*, DBusMessage* message, void* self) {
				return static_cast<AtspiHost*>(self)->handle(message);
			}

			DBusHandlerResult handle(DBusMessage* message) {
				if (dbus().message_get_type(message) != kMethodCall) return kNotYetHandled;
				const char* ifaceRaw = dbus().message_get_interface(message);
				const char* memberRaw = dbus().message_get_member(message);
				if (!memberRaw) return kNotYetHandled;
				const std::string iface = ifaceRaw ? ifaceRaw : "";
				const std::string member = memberRaw;
				const Target t = resolve(dbus().message_get_path(message));

				// Somebody is looking: from now on the interface describes itself. Not the
				// registry, which looks at everything and reads to nobody.
				const char* sender = dbus().message_get_sender(message);
				const bool registry = sender && !mRegistry.empty() && mRegistry == sender;
				if (!registry && !mBridge->wanted()) {
					mBridge->want();
					RDA_LOG_INFO("accessibility: a client asked; describing the interface from now on");
					RDA::Wake::requestFrame();
				}

				if (t.kind == Kind::None) return fail(message, "org.freedesktop.DBus.Error.UnknownObject", "gone");

				DBusMessage* reply = dbus().message_new_method_return(message);
				if (!reply) return kHandled;
				bool known = true;
				{
					Out out(reply);
					In in(message);

					if (iface == kProperties && member == "Get") {
						const std::string onIface = in.str(), name = in.str();
						if (!property(out, t, onIface, name)) known = false;
					} else if (iface == kProperties && member == "GetAll") {
						const std::string onIface = in.str();
						Out dict(out.iter(), kTypeArray, "{sv}");
						static const char* const names[] = { "Name", "Description", "Parent", "ChildCount",
							"Locale", "AccessibleId", "ToolkitName", "Version", "AtspiVersion", "Id",
							"CurrentValue", "MinimumValue", "MaximumValue", "MinimumIncrement",
							"CharacterCount", "CaretOffset", "NActions", "NSelectedChildren" };
						for (const char* name : names) {
							// Probed into a scratch message first, so a property this object
							// does not have leaves no half-written entry behind.
							DBusMessage* probe = dbus().message_new_method_return(message);
							bool has = false;
							{ Out scratch(probe); has = property(scratch, t, onIface, name); }
							dbus().message_unref(probe);
							if (!has) continue;
							Out entry(dict.iter(), kTypeDictEntry, nullptr);
							entry.str(name);
							property(entry, t, onIface, name);
						}
					} else if (iface == kProperties && member == "Set") {
						const std::string onIface = in.str(), name = in.str();
						double number = 0.0;
						std::string text;
						in.variant(number, text);
						if (onIface == kApplication && name == "Id") mAppId = static_cast<int32_t>(number);
						else if (onIface == kValue && name == "CurrentValue") {
							char buffer[64];
							std::snprintf(buffer, sizeof(buffer), "%.17g", number);
							request(t, AccessibleAction::SetValue, buffer);
						} else known = false;
					} else if (iface == "org.freedesktop.DBus.Introspectable" && member == "Introspect") {
						std::string xml = "<node>";
						for (const char* name : interfacesOf(t)) xml += std::string("<interface name=\"") + name + "\"/>";
						xml += "</node>";
						out.str(xml);
					} else if (iface == kAccessible || iface.empty()) {
						known = accessible(out, in, t, member);
					} else if (iface == kComponent) {
						known = component(out, in, t, member);
					} else if (iface == kAction && t.kind == Kind::Node) {
						known = action(out, in, t, member);
					} else if (iface == kText && t.kind == Kind::Node && hasText(t.node->info)) {
						known = text(out, in, t, member);
					} else if (iface == kEditableText && t.kind == Kind::Node && member == "SetTextContents") {
						request(t, AccessibleAction::SetValue, in.str());
						out.boolean(true);
					} else if (iface == kSelection && t.kind == Kind::Node) {
						known = selection(out, in, t, member);
					} else if (iface == kApplication && member == "GetLocale") {
						out.str("");
					} else {
						known = false;
					}
				}
				if (!known) {
					dbus().message_unref(reply);
					return fail(message, "org.freedesktop.DBus.Error.UnknownMethod", member.c_str());
				}
				dbus().connection_send(mBus, reply, nullptr);
				dbus().message_unref(reply);
				return kHandled;
			}

			DBusHandlerResult fail(DBusMessage* message, const char* name, const char* text) {
				DBusMessage* error = dbus().message_new_error(message, name, text);
				if (error) { dbus().connection_send(mBus, error, nullptr); dbus().message_unref(error); }
				return kHandled;
			}

			bool accessible(Out& out, In& in, const Target& t, const std::string& member) {
				if (member == "GetRole") { out.u32(roleOfTarget(t)); return true; }
				if (member == "GetRoleName" || member == "GetLocalizedRoleName") { out.str(roleNameOf(roleOfTarget(t))); return true; }
				if (member == "GetState") { states(out, t); return true; }
				if (member == "GetIndexInParent") { out.i32(indexInParent(t)); return true; }
				if (member == "GetApplication") { ref(out, mName, kRootPath); return true; }
				if (member == "GetAttributes") {
					// A tree row's depth, the attribute Orca reads to say "level 3".
					Out dict(out.iter(), kTypeArray, "{ss}");
					if (t.kind == Kind::Node && t.node->info.live) {
						Out entry(dict.iter(), kTypeDictEntry, nullptr);
						entry.str("live");
						entry.str(t.node->info.live == 2 ? "assertive" : "polite");
					}
					if (t.kind == Kind::Node && t.node->info.level > 0) {
						Out entry(dict.iter(), kTypeDictEntry, nullptr);
						entry.str("level");
						entry.str(std::to_string(t.node->info.level));
					}
					return true;
				}
				if (member == "GetRelationSet") { Out set(out.iter(), kTypeArray, "(ua(so))"); return true; }
				if (member == "GetInterfaces") {
					Out list(out.iter(), kTypeArray, "s");
					for (const char* name : interfacesOf(t)) list.str(name);
					return true;
				}
				if (member == "GetChildren") {
					Out list(out.iter(), kTypeArray, "(so)");
					if (t.kind == Kind::Root) ref(list, mName, kFramePath);
					else for (const int c : *childrenOf(t)) refNode(list, *t.tree, c);
					return true;
				}
				if (member == "GetChildAtIndex") {
					const int at = in.i32();
					if (t.kind == Kind::Root) { ref(out, at == 0 ? mName : "", at == 0 ? kFramePath : kNullPath); return true; }
					const std::vector<int>& kids = *childrenOf(t);
					if (at >= 0 && at < static_cast<int>(kids.size())) refNode(out, *t.tree, kids[static_cast<size_t>(at)]);
					else ref(out, "", kNullPath);
					return true;
				}
				return false;
			}

			bool component(Out& out, In& in, const Target& t, const std::string& member) {
				if (member == "GetExtents") {
					int x, y, w, h;
					extents(t, in.u32(), x, y, w, h);
					Out r(out.iter(), kTypeStruct, nullptr);
					r.i32(x); r.i32(y); r.i32(w); r.i32(h);
					return true;
				}
				if (member == "GetPosition") {
					int x, y, w, h;
					extents(t, in.u32(), x, y, w, h);
					out.i32(x); out.i32(y);
					return true;
				}
				if (member == "GetSize") {
					int x, y, w, h;
					extents(t, 1, x, y, w, h);
					out.i32(w); out.i32(h);
					return true;
				}
				if (member == "Contains") {
					const int px = in.i32(), py = in.i32();
					int x, y, w, h;
					extents(t, in.u32(), x, y, w, h);
					out.boolean(px >= x && py >= y && px < x + w && py < y + h);
					return true;
				}
				if (member == "GetAccessibleAtPoint") {
					const int px = in.i32(), py = in.i32();
					const uint32_t coords = in.u32();
					// The last node that holds the point, which is the deepest and the one in front.
					int found = -1;
					if (t.tree) {
						for (size_t i = t.tree->nodes.size(); i-- > 0;) {
							Target probe;
							probe.kind = Kind::Node;
							probe.tree = t.tree;
							probe.node = &t.tree->nodes[i];
							int x, y, w, h;
							extents(probe, coords, x, y, w, h);
							if (px >= x && py >= y && px < x + w && py < y + h) { found = static_cast<int>(i); break; }
						}
					}
					if (found >= 0) refNode(out, *t.tree, found);
					else ref(out, "", kNullPath);
					return true;
				}
				if (member == "GetLayer") { out.u32(3); return true; }        // a widget
				if (member == "GetMDIZOrder") { out.i16(0); return true; }
				if (member == "GetAlpha") { out.dbl(1.0); return true; }
				if (member == "GrabFocus") { request(t, AccessibleAction::Focus); out.boolean(t.kind == Kind::Node); return true; }
				if (member == "ScrollTo" || member == "ScrollToPoint") {
					const bool row = t.kind == Kind::Node && (t.node->info.role == AccessibleRole::ListItem ||
					                                          t.node->info.role == AccessibleRole::TreeItem);
					if (row) request(t, AccessibleAction::ScrollIntoView);
					out.boolean(row);
					return true;
				}
				if (member == "SetExtents" || member == "SetPosition" || member == "SetSize") { out.boolean(false); return true; }
				return false;
			}

			bool action(Out& out, In& in, const Target& t, const std::string& member) {
				const std::vector<Offered> offered = actionsOf(t.node->info);
				if (member == "GetNActions") { out.i32(static_cast<int32_t>(offered.size())); return true; }
				if (member == "GetActions") {
					Out list(out.iter(), kTypeArray, "(sss)");
					for (const Offered& o : offered) { Out e(list.iter(), kTypeStruct, nullptr); e.str(o.name); e.str(o.name); e.str(""); }
					return true;
				}
				const int at = in.i32();
				const bool valid = at >= 0 && at < static_cast<int>(offered.size());
				if (member == "GetName" || member == "GetLocalizedName" || member == "GetDescription") {
					out.str(valid && member != "GetDescription" ? offered[static_cast<size_t>(at)].name : "");
					return true;
				}
				if (member == "GetKeyBinding") { out.str(""); return true; }
				if (member == "DoAction") {
					if (valid) request(t, offered[static_cast<size_t>(at)].action);
					out.boolean(valid);
					return true;
				}
				return false;
			}

			// AT-SPI's two ways of naming a unit: the old boundary types (character, word start
			// and end, sentence start and end, line start and end) and the newer granularity
			// (character, word, sentence, line, paragraph). A sentence is taken as a line:
			// finding sentences needs a language's rules, and a line is what a field has.
			static RDA::TextUnit unitOfBoundary(uint32_t type) {
				switch (type) {
				case 0:  return RDA::TextUnit::Character;
				case 1: case 2: return RDA::TextUnit::Word;
				default: return RDA::TextUnit::Line;
				}
			}
			static RDA::TextUnit unitOfGranularity(uint32_t granularity) {
				switch (granularity) {
				case 0:  return RDA::TextUnit::Character;
				case 1:  return RDA::TextUnit::Word;
				case 4:  return RDA::TextUnit::Paragraph;
				default: return RDA::TextUnit::Line;
				}
			}

			// A box in layout units, as the coordinates a client asked for.
			void boxOut(Out& out, const Target& t, const Rect& box, uint32_t coords) {
				const float k = (t.tree && t.tree->unitScale > 0.0f) ? t.tree->unitScale : 1.0f;
				int x = static_cast<int>(box.x * k), y = static_cast<int>(box.y * k);
				if (coords == 0) { const AccessibleWindow w = mBridge->window(); x += w.x; y += w.y; }
				out.i32(x); out.i32(y);
				out.i32(static_cast<int>(box.w * k)); out.i32(static_cast<int>(box.h * k));
			}

			void selectText(const Target& t, int from, int to) {
				request(t, AccessibleAction::SetSelection, std::to_string(from) + "," + std::to_string(to));
			}

			bool text(Out& out, In& in, const Target& t, const std::string& member) {
				const std::vector<uint32_t> value = textCodepoints(t.node->info.value);
				const AccessibleText& info = t.node->text;
				const int count = static_cast<int>(value.size());
				const int caret = textCaretIn(info, count);
				const int anchor = textAnchorIn(info, count);

				if (member == "GetText") {
					const int from = in.i32(), to = in.i32();
					out.str(textSlice(value, from, to < 0 ? count : to));
					return true;
				}
				if (member == "GetCharacterAtOffset") {
					const int at = in.i32();
					out.i32(at >= 0 && at < count ? static_cast<int32_t>(value[static_cast<size_t>(at)]) : 0);
					return true;
				}
				// The unit at an offset, the one before it and the one after it: what a
				// screen reader says as the caret moves by characters, words and lines.
				const bool at = member == "GetTextAtOffset" || member == "GetStringAtOffset";
				const bool before = member == "GetTextBeforeOffset";
				const bool after = member == "GetTextAfterOffset";
				if (at || before || after) {
					const int offset = std::clamp(in.i32(), 0, count);
					const uint32_t kind = in.u32();
					const RDA::TextUnit unit = member == "GetStringAtOffset" ? unitOfGranularity(kind) : unitOfBoundary(kind);
					int s0 = 0, e0 = 0;
					textEnclosing(value, info, unit, offset, s0, e0);
					if (before) {
						if (s0 == 0) { e0 = 0; }
						else { const int e1 = s0; textEnclosing(value, info, unit, s0 - 1, s0, e0); e0 = e1; }
					} else if (after) {
						if (e0 >= count) { s0 = e0 = count; }
						else { textEnclosing(value, info, unit, e0, s0, e0); }
					}
					out.str(textSlice(value, s0, e0));
					out.i32(s0);
					out.i32(e0);
					return true;
				}
				if (member == "GetCharacterExtents") {
					const int offset = in.i32();
					const uint32_t coords = in.u32();
					Rect box{};
					textCharacterBox(info, offset, box);
					Out r(out.iter(), kTypeStruct, nullptr);
					boxOut(r, t, box, coords);
					return true;
				}
				if (member == "GetRangeExtents") {
					const int from = in.i32(), to = in.i32();
					const uint32_t coords = in.u32();
					// Everything from the first character to the last, as one box.
					Rect total{};
					bool any = false;
					for (int i = from; i < (std::max)(to, from + 1); ++i) {
						Rect box{};
						if (!textCharacterBox(info, i, box)) continue;
						if (!any) { total = box; any = true; continue; }
						const float x1 = (std::max)(total.x + total.w, box.x + box.w);
						const float y1 = (std::max)(total.y + total.h, box.y + box.h);
						total.x = (std::min)(total.x, box.x);
						total.y = (std::min)(total.y, box.y);
						total.w = x1 - total.x;
						total.h = y1 - total.y;
					}
					Out r(out.iter(), kTypeStruct, nullptr);
					boxOut(r, t, total, coords);
					return true;
				}
				if (member == "GetOffsetAtPoint") {
					int px = in.i32(), py = in.i32();
					const uint32_t coords = in.u32();
					const float k = (t.tree && t.tree->unitScale > 0.0f) ? t.tree->unitScale : 1.0f;
					if (coords == 0) { const AccessibleWindow w = mBridge->window(); px -= w.x; py -= w.y; }
					const float ux = px / k, uy = py / k;
					int found = -1;
					for (const AccessibleText::Line& line : info.lines) {
						if (uy < line.y || uy >= line.y + line.h) continue;
						for (size_t i = 0; i + 1 < line.x.size(); ++i)
							if (ux >= line.x[i] && ux < line.x[i + 1]) { found = line.first + static_cast<int>(i); break; }
						break;
					}
					out.i32(found);
					return true;
				}
				if (member == "GetNSelections") { out.i32(caret != anchor ? 1 : 0); return true; }
				if (member == "GetSelection") {
					in.i32();
					out.i32((std::min)(caret, anchor));
					out.i32((std::max)(caret, anchor));
					return true;
				}
				if (member == "SetSelection") {
					in.i32();
					const int from = in.i32(), to = in.i32();
					selectText(t, from, to);
					out.boolean(true);
					return true;
				}
				if (member == "AddSelection") {
					const int from = in.i32(), to = in.i32();
					selectText(t, from, to);
					out.boolean(true);
					return true;
				}
				if (member == "RemoveSelection") {
					selectText(t, caret, caret);
					out.boolean(true);
					return true;
				}
				if (member == "SetCaretOffset") {
					const int offset = std::clamp(in.i32(), 0, count);
					selectText(t, offset, offset);
					out.boolean(true);
					return true;
				}
				if (member == "GetDefaultAttributes") { Out dict(out.iter(), kTypeArray, "{ss}"); return true; }
				return false;
			}

			bool selection(Out& out, In& in, const Target& t, const std::string& member) {
				const std::vector<int>& kids = t.node->children;
				std::vector<int> chosen;
				for (const int c : kids) if (t.tree->nodes[static_cast<size_t>(c)].info.selected > 0) chosen.push_back(c);
				if (member == "GetSelectedChild") {
					const int at = in.i32();
					if (at >= 0 && at < static_cast<int>(chosen.size())) refNode(out, *t.tree, chosen[static_cast<size_t>(at)]);
					else ref(out, "", kNullPath);
					return true;
				}
				if (member == "IsChildSelected") {
					const int at = in.i32();
					out.boolean(at >= 0 && at < static_cast<int>(kids.size()) &&
					            t.tree->nodes[static_cast<size_t>(kids[static_cast<size_t>(at)])].info.selected > 0);
					return true;
				}
				const bool multi = t.node->info.multiSelectable;
				auto askOf = [&](int index, AccessibleAction action) {
					Target child = t;
					child.index = index;
					child.node = &t.tree->nodes[static_cast<size_t>(index)];
					request(child, action);
				};
				if (member == "SelectChild") {
					// Adds to what is chosen where several can be; elsewhere it is the one.
					const int at = in.i32();
					const bool ok = at >= 0 && at < static_cast<int>(kids.size());
					if (ok) askOf(kids[static_cast<size_t>(at)],
					              multi ? AccessibleAction::AddToSelection : AccessibleAction::Select);
					out.boolean(ok);
					return true;
				}
				if (member == "DeselectChild" || member == "DeselectSelectedChild") {
					// One by its place among all the items, the other among the chosen ones.
					const int at = in.i32();
					const std::vector<int>& from = (member == "DeselectChild") ? kids : chosen;
					const bool ok = multi && at >= 0 && at < static_cast<int>(from.size());
					if (ok) askOf(from[static_cast<size_t>(at)], AccessibleAction::RemoveFromSelection);
					out.boolean(ok);
					return true;
				}
				if (member == "SelectAll" || member == "ClearSelection") {
					// Asked of the list or tree itself. Where only one can be chosen, neither
					// means anything, and saying no is the answer.
					if (multi) request(t, member == "SelectAll" ? AccessibleAction::SelectAll
					                                            : AccessibleAction::ClearSelection);
					out.boolean(multi);
					return true;
				}
				return false;
			}
		};

		// Whether assistive technology is on. Toolkits register only then, so a desktop
		// with nobody listening costs nothing; RDA_ACCESSIBILITY=1 says to register anyway.
		bool assistiveTechnologyOn(DBusConnection* session) {
			if (const char* forced = std::getenv("RDA_ACCESSIBILITY")) {
				if (forced[0] == '1') return true;
				if (forced[0] == '0') return false;
			}
			for (const char* name : { "IsEnabled", "ScreenReaderEnabled" }) {
				DBusMessage* call = dbus().message_new_method_call("org.a11y.Bus", "/org/a11y/bus", kProperties, "Get");
				if (!call) return false;
				{ Out args(call); args.str("org.a11y.Status"); args.str(name); }
				DBusError error;
				dbus().error_init(&error);
				DBusMessage* reply = dbus().connection_send_with_reply_and_block(session, call, 1000, &error);
				dbus().message_unref(call);
				if (!reply) { dbus().error_free(&error); continue; }
				bool on = false;
				DBusMessageIter top;
				if (dbus().message_iter_init(reply, &top) && dbus().message_iter_get_arg_type(&top) == kTypeVariant) {
					DBusMessageIter inner;
					dbus().message_iter_recurse(&top, &inner);
					if (dbus().message_iter_get_arg_type(&inner) == kTypeBool) {
						dbus_bool_t b = 0;
						dbus().message_iter_get_basic(&inner, &b);
						on = b != 0;
					}
				}
				dbus().message_unref(reply);
				if (on) return true;
			}
			return false;
		}

		// The accessibility bus's address, which the session bus knows. Empty without one.
		std::string accessibilityBus(DBusConnection* session) {
			DBusMessage* call = dbus().message_new_method_call("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
			if (!call) return {};
			DBusError error;
			dbus().error_init(&error);
			DBusMessage* reply = dbus().connection_send_with_reply_and_block(session, call, 1000, &error);
			dbus().message_unref(call);
			if (!reply) { dbus().error_free(&error); return {}; }
			In in(reply);
			std::string address = in.str();
			dbus().message_unref(reply);
			return address;
		}
	}

	std::unique_ptr<AccessibilityHost> AccessibilityHost::attach(GLFWwindow* window,
	                                                             std::shared_ptr<AccessibleBridge> bridge) {
		(void)window;
		auto nothing = [] { return std::make_unique<AccessibilityHost>(); };
		if (!bridge || !dbus().handle) return nothing();

		DBusError error;
		dbus().error_init(&error);
		DBusConnection* session = dbus().bus_get_private(kSessionBus, &error);
		if (!session) { dbus().error_free(&error); return nothing(); }
		dbus().connection_set_exit_on_disconnect(session, 0);
		const bool on = assistiveTechnologyOn(session);
		const std::string address = on ? accessibilityBus(session) : std::string();
		dbus().connection_close(session);
		dbus().connection_unref(session);
		if (!on) {
			RDA_LOG_INFO("accessibility: assistive technology is off; not registering with AT-SPI "
			             "(RDA_ACCESSIBILITY=1 to register anyway)");
			return nothing();
		}
		if (address.empty()) return nothing();

		DBusConnection* bus = dbus().connection_open_private(address.c_str(), &error);
		if (!bus) { dbus().error_free(&error); return nothing(); }
		dbus().connection_set_exit_on_disconnect(bus, 0);
		if (!dbus().bus_register(bus, &error)) {
			dbus().error_free(&error);
			dbus().connection_close(bus);
			dbus().connection_unref(bus);
			return nothing();
		}
		return std::make_unique<AtspiHost>(std::move(bridge), bus, address);
	}
}

#endif
