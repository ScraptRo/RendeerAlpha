#pragma once
#include <Layout/Blueprint.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct JSContext;

// The build-time half of code that runs: which of a layout's functions a binding reaches,
// and the bytecode they ship as. See docs/architecture/runtime-code.md.
//
// Internal to the layout compiler, like ModuleTransform.h. It works inside the QuickJS
// context the layout is evaluated in, because two of its answers are only there: the
// value a top-level constant ended up with, and the function a name was bound to.
namespace RDA::Layout {

	// An ES-module bundle, made evaluable as a script: the export clause esbuild ends it
	// with is taken off, and the whole is wrapped so that evaluating it leaves
	// `__rdaModule.default` -- the layout's builder -- and `__rdaModule.get(name)`, which
	// reads any top-level name of the bundle after it has run.
	bool wrapBundle(const std::string& bundle, std::string& script, std::string& error);
	// The same, also saying which local name each export is bound to -- esbuild renames a
	// top-level name apart when two files both declare it, and the export clause is the one
	// place that says so. Evaluating leaves `__rdaModule.exports` too, by exported name.
	bool wrapBundle(const std::string& bundle, std::string& script,
	                std::unordered_map<std::string, std::string>& exports, std::string& error);
	// How many lines the wrapping puts in front of the bundle: the script's line N is the
	// bundle's N - 1, which is what an exception thrown while it is evaluated has to be
	// mapped back by. See SourceMap.h.
	constexpr size_t kWrapLinesBefore = 1;

	// A value that reads state, without the arrow that says so, given the arrow: `width={state.gap}`
	// becomes `width={() => (state.gap)}`, so that nothing after this has two cases. Done on the
	// bundle, so it reaches what a layout imports, and before it is evaluated, because `state` does
	// not exist then. Adds no lines, so every line the source map knows stays where it was.
	bool withImplicitBindings(std::string& bundle, std::string& error);

	class ScriptPlanner {
	public:
		// `ctx` is the context the wrapped bundle was evaluated in, and must outlive this.
		// `entry` is the layout's own path, for naming files in messages.
		ScriptPlanner(JSContext* ctx, std::string bundle, std::string sourceMap,
		              std::string entry, bool allowRuntimeCode);
		~ScriptPlanner();

		// Whether `name` is a function the layout defines at its top level -- what the
		// expression parser asks of a name followed by `(`.
		bool isFunction(const std::string& name);

		// Functions a grammar-compiled binding calls. Each becomes run-time code, with
		// everything it reaches. False when the code around the binding declares one of
		// those names too -- it would call the top-level one, not the one meant.
		bool noteCalls(const std::string& source, const std::string& rowParam,
		               const std::vector<std::string>& names, std::string& error);

		// A binding the grammar refused, offered as JavaScript instead. On success `slot`
		// is what its one RunScript instruction names; on failure `error` says why it
		// cannot be either -- a name that is nothing, a write in a value binding.
		bool addBinding(const std::string& source, const std::string& rowParam, bool isEvent,
		                const std::string& grammarError, std::string& slot, std::string& error);

		// state.ts's code: a computed value, which is checked as a value binding is, or an
		// action or a watch, which are checked as a handler is and take whatever parameters
		// they like.
		// Before either, the local names its declarations go by -- `state` and the others,
		// unless esbuild renamed them -- so code that names one gets the live one.
		bool setStateAliases(const std::unordered_map<std::string, std::string>& localToRuntime,
		                     std::string& error);
		bool addComputed(const std::string& name, const std::string& source, std::string& error);
		bool addAction(const std::string& name, const std::string& source, std::string& error);
		bool addWatch(const std::string& name, const std::string& source, std::string& error);

		// Whether anything needs run-time code at all.
		bool needed() const {
			return !mCalls.empty() || !mBindings.empty() || !mComputed.empty() || !mActions.empty() ||
			       !mWatches.empty();
		}
		size_t bindingCount() const { return mBindings.size(); }
		size_t callCount() const { return mCalls.size(); }

		// After the tree: the run-time module, written out and compiled. `declared` maps
		// the names the layout gave its own signals to what they are called once scoped.
		bool emit(const std::unordered_map<std::string, std::string>& declared,
		          BlueprintScript& out, std::string& error);

		// Said once each, for the compiler to log: a top-level value a function changes,
		// which starts over on every hot reload.
		const std::vector<std::string>& warnings() const { return mWarnings; }

	private:
		struct Binding {
			std::string source;
			std::string row;
			bool        event = false;
		};

		bool ensureAnalysis(std::string& error);
		bool check(const std::string& source, bool isEvent, bool action, std::string& error);

		struct Named {
			std::string name;
			std::string source;
		};
		std::vector<Named>       mComputed;
		std::vector<Named>       mActions;
		std::vector<Named>       mWatches;
		bool                     mState = false;

		JSContext*               mCtx = nullptr;
		std::string              mBundle;
		std::string              mSourceMap;
		std::string              mEntry;
		bool                     mAllow = true;
		bool                     mAnalysed = false;
		bool                     mAnalysisFailed = false;
		std::string              mAnalysisError;
		std::vector<std::string> mCalls;
		std::vector<Binding>     mBindings;
		std::vector<std::string> mWarnings;
	};
}
