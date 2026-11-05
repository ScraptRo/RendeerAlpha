#pragma once
#include <Layout/Expression.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// The run-time half of a layout's functions: QuickJS, loaded with the bytecode a blueprint
// carries, reached from the VM through two instructions. See
// docs/architecture/runtime-code.md.
//
// Nothing here exists until a blueprint with a script section is loaded. A process whose
// layouts define no functions never creates a JavaScript runtime at all, which is the
// part of the old promise -- nothing evaluates JavaScript -- that still holds for it.
//
// State is never kept here. A function reads and writes signals and tables, which are the
// engine's, through `state` and `tables`; what it holds between calls is only what its
// layout's top level declared. So a rebuilt layout can throw its module away and load the
// new one without anything the reader did being lost.
//
// Everything runs on the loop thread, where bindings and handlers already run.
namespace RDA::Layout {

	class Blueprint;
	struct BlueprintScript;

	inline constexpr uint32_t kNoScript = 0xFFFFFFFFu;

	// One layout's functions, loaded. Owned by the layout instance that loaded it; when it
	// goes, its functions go with it, and a handler that still names one reports that the
	// layout is gone rather than running into freed memory.
	class ScriptModule {
	public:
		~ScriptModule();
		ScriptModule(const ScriptModule&) = delete;
		ScriptModule& operator=(const ScriptModule&) = delete;

		// What a program's "$name" and "%3" slots resolve to: an id the VM's two
		// instructions carry. kNoScript for a name the module does not have.
		uint32_t function(std::string_view name) const;
		uint32_t binding(uint32_t index) const;
		const std::string& name() const { return mName; }
		// One "file:line" per line of the module, for turning an error's line back into
		// a line of the .tsx; and the module's own JavaScript values, for the host.
		const std::vector<std::string>& lineMap() const { return mLineMap; }
		void* state() const { return mState; }
		// Which module it is, for what it started -- a timer goes when its module does.
		uint32_t id() const { return mId; }

	private:
		friend class ScriptHost;
		friend std::shared_ptr<ScriptModule> loadModule(const BlueprintScript&, const std::string&,
		                                                const char*, std::string&);
		ScriptModule() = default;
		std::string                               mName;
		uint32_t                                  mId = 0;
		std::unordered_map<std::string, uint32_t> mFunctions;
		std::vector<uint32_t>                     mBindings;
		std::vector<std::string>                  mLineMap;   // module line -> "file:line"
		void*                                     mState = nullptr;  // the module's JS values
	};

	class ScriptHost {
	public:
		// Loads a blueprint's functions, starting the runtime if this is the first. Null,
		// with the reason, when it carries none or they cannot be loaded -- bytecode from a
		// different QuickJS, say, which is refused rather than read.
		std::shared_ptr<ScriptModule> load(const Blueprint& blueprint, const std::string& name,
		                                   std::string& error);

		// ---- state.ts's code: computed signals, watches and actions --------------------
		//
		// Loads what state.ts compiled to. Its computed signals are worked out at once and
		// kept current from then on, its watches start from the values as they are, and its
		// actions start answering their commands. Replaces
		// whatever state code was loaded before, and every signal keeps its value, which is
		// also how state.ts reloads while the interface runs. False, with the reason, when
		// the code cannot be read -- and then whatever was loaded before stays.
		bool loadState(const BlueprintScript& script, std::string& error);
		// The same, from the bytes a generated state module carries.
		bool loadState(const uint8_t* bytes, size_t size, std::string& error);
		// Takes it away: computed signals keep their last values and stop following what
		// they read, watches stop, and actions stop answering.
		void unloadState();
		bool stateLoaded() const;
		// Once a frame, before bindings are applied: the timers that are due go off, a
		// computed signal that read a table is checked against it -- a table has no
		// observers -- and last, with all of that settled, watches run.
		void tick();

		// How long until the next timer is due, in seconds; zero when one already is, and
		// negative when there is none. An idle loop waits no longer than this, so a timer
		// goes off on time rather than whenever something else wakes the window.
		double secondsToNextTimer() const;
		// How many timers are waiting, for whoever wants to know.
		size_t timerCount() const;

		// What run-time code is allowed to do, decided by whoever runs it.
		enum class Mode : uint8_t {
			// A value binding: reads only. A binding runs whenever what it read changes, so
			// writing state or calling a command from one is refused.
			Value,
			// A handler, and anything with no frame at all: reads and writes.
			Handler,
		};

		// While one of these is alive, every signal and table that run-time code reads is
		// recorded in it -- which is how a binding that calls a function learns what it
		// depends on. Frames nest; the innermost one records.
		class Frame {
		public:
			explicit Frame(Mode mode);
			~Frame();
			Frame(const Frame&) = delete;
			Frame& operator=(const Frame&) = delete;

			Mode mode() const { return mMode; }
			const std::vector<uint32_t>& signals() const { return mSignals; }
			// Each table read, with the version it had when it was read.
			const std::vector<std::pair<uint32_t, uint64_t>>& tables() const { return mTables; }

			// Called by the natives as run-time code reads.
			void noteSignal(uint32_t id) {
				for (const uint32_t one : mSignals) if (one == id) return;
				mSignals.push_back(id);
			}
			void noteTable(uint32_t id, uint64_t version) {
				for (const auto& one : mTables) if (one.first == id) return;
				mTables.emplace_back(id, version);
			}

		private:
			friend class ScriptHost;
			Mode                                        mMode;
			std::vector<uint32_t>                       mSignals;
			std::vector<std::pair<uint32_t, uint64_t>>  mTables;
			Frame*                                      mOuter = nullptr;
		};

		// Whether the runtime has been started -- whether anything has needed it yet.
		bool running() const;
		// How long one call may run before it is stopped, in milliseconds. 50 unless
		// RDA_SCRIPT_BUDGET_MS says otherwise.
		double budgetMs() const;

		// Called through the VM's hooks; public so a test can call them directly.
		bool call(uint32_t id, const Value* args, size_t argc, Value& out, std::string& error);
		bool run(uint32_t id, const RowRef* row, const Value* event, Value& out, std::string& error);

		// For the natives: the frame that is recording now, or null.
		Frame* frame() const { return mFrame; }

	private:
		friend class ScriptModule;
		Frame* mFrame = nullptr;
	};

	ScriptHost& scriptHost();

	// Reads a script section into a module of its own, starting the runtime if it must.
	// What a layout's load and state.ts's both come down to; `remedy` is what a stale one
	// is told to do.
	std::shared_ptr<ScriptModule> loadModule(const BlueprintScript& script, const std::string& name,
	                                         const char* remedy, std::string& error);
}
