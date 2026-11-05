#pragma once
#include <functional>
#include <string>

// QuickJS types, forward declared so the 65 KB quickjs.h stays out of every file that
// merely wants to run a script.
struct JSRuntime;
struct JSContext;

namespace RDA {

	// What came back from evaluating a snippet. A script that throws is not an engine
	// error — it is a result with `ok` false — so an editor can print it and carry on.
	struct ScriptResult {
		bool        ok = false;
		std::string value; // the completion value, stringified
		std::string error; // message and stack when ok is false
		// How many of the changes the script asked for were actually made. Fewer than it
		// asked for means something it named had gone by the time the queue was applied;
		// zero after a failure means the run changed nothing, which is the point.
		size_t      commandsApplied = 0;
	};

	// An embedded JavaScript runtime (QuickJS).
	//
	// Deliberately sandboxed: none of QuickJS's libc module is compiled in, so scripts
	// have no file, process or network access at all. Everything a script can reach is
	// something the engine chose to hand it, which is the property that makes running
	// arbitrary text from an editor panel reasonable.
	//
	// One runtime owns one context for now. Isolating scripts from each other (a context
	// per document, or per plugin) is a later step and does not change this interface.
	class ScriptEngine {
	public:
		ScriptEngine() = default;
		~ScriptEngine();

		ScriptEngine(const ScriptEngine&) = delete;
		ScriptEngine& operator=(const ScriptEngine&) = delete;

		bool init();
		void shutdown();
		bool isReady() const { return mContext != nullptr; }

		// Runs `code` as a global script. `name` is what appears in stack traces.
		ScriptResult eval(const std::string& code, const std::string& name = "<input>");

		// Where console.log() goes. Unset means the output is discarded — the engine
		// never assumes a console exists.
		void setPrintHandler(std::function<void(const std::string&)> handler) {
			mPrint = std::move(handler);
		}
		void print(const std::string& text) const { if (mPrint) mPrint(text); }

		// Installs the engine's own classes and globals: Vec3, Material, Entity, and the
		// `scene` object they hang off. Separate from init() so a host that only wants a
		// bare sandbox does not get them.
		bool bindEngine();

		// The runtime's version, for logging what is actually embedded.
		static const char* version();

		// The context, for binding code that lives in other translation units.
		JSContext* context() const { return mContext; }

	private:
		JSRuntime* mRuntime = nullptr;
		JSContext* mContext = nullptr;
		std::function<void(const std::string&)> mPrint;

		// What to do with the changes a script asked for, once it has finished: applied
		// when it succeeded, thrown away when it did not. Returns how many were made.
		//
		// A hook rather than a direct call, because this class is also the bare sandbox a
		// *client* embeds -- JsApp compiles it and has no scene to change. bindEngine()
		// installs one; a host that never binds the engine never acquires the dependency.
		std::function<size_t(bool succeeded)> mCommit;
	};

	// Releases every material a script built with createMaterial().
	//
	// A host has to call this while the renderer is still alive — before its own teardown,
	// not from a static destructor. Scripts create materials that the scene then borrows by
	// pointer, so nothing else knows when the last one stopped being drawn; the host does.
	// Waits for the GPU itself, since a material's descriptor set may still be bound.
	//
	// Safe to call more than once, and when no script ever ran.
	void releaseScriptMaterials();
}
