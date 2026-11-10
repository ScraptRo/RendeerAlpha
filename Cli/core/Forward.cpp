#include "Forward.h"

#include <Core/Location.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#else
	#include <signal.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

namespace Cli {

	namespace {

#if defined(_WIN32)
		constexpr const char* kPreviewName = "rda-preview.exe";
#else
		constexpr const char* kPreviewName = "rda-preview";
#endif

		// Beside this program, where `stage` puts it and where the build puts it: the same
		// directory the engine looks in for its own files. Not the PATH, and not the working
		// directory -- a copy of `rda` that has wandered off from its neighbour should say so,
		// rather than start whichever `rda-preview` happens to be nearest.
		fs::path previewPath() {
			return RDA::executableDirectory() / kPreviewName;
		}

		bool previewIsThere(const fs::path& preview) {
			std::error_code ec;
			return !preview.empty() && fs::is_regular_file(preview, ec);
		}

		int missing(const fs::path& preview) {
			std::printf("rda: `prototype` and `shot` run in rda-preview, and it is not beside rda "
			            "(looked for %s). It is built with the rest of the tools and staged into "
			            "bin/ with them.\n", shown(preview).c_str());
			return 1;
		}

#if defined(_WIN32)
		// Ctrl+C is delivered to every process on the console. The child decides what it means
		// -- a window with a prototype in it closes -- and this one waits for it, rather than
		// ending first and leaving a prompt on the screen above a window that is still shutting
		// down. A handler, not "ignore": the ignore flag is inherited and the child would
		// inherit a Ctrl+C that does nothing.
		BOOL WINAPI keepWaiting(DWORD event) {
			return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
		}

		// Everything after the program's own name, as the shell wrote it.
		std::wstring argumentsOf(const std::wstring& line) {
			size_t at = 0;
			if (!line.empty() && line[0] == L'"') {
				const size_t close = line.find(L'"', 1);
				at = close == std::wstring::npos ? line.size() : close + 1;
			} else {
				while (at < line.size() && line[at] != L' ' && line[at] != L'\t') ++at;
			}
			return line.substr(at);
		}

		void lendToChild(DWORD which) {
			const HANDLE handle = GetStdHandle(which);
			if (handle && handle != INVALID_HANDLE_VALUE) {
				SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
			}
		}
#endif
	}

	int forwardToPreview(const Args& line) {
		const fs::path preview = previewPath();
		if (!previewIsThere(preview)) return missing(preview);

#if defined(_WIN32)
		(void)line;
		std::wstring command = L"\"" + preview.wstring() + L"\"" + argumentsOf(GetCommandLineW());

		lendToChild(STD_INPUT_HANDLE);
		lendToChild(STD_OUTPUT_HANDLE);
		lendToChild(STD_ERROR_HANDLE);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

		// Suspended until it is in a job that dies with this process: a tool run under a
		// timeout is killed by its parent, and a window left open by a program nobody is waiting
		// for any more is the worse way for that to end.
		PROCESS_INFORMATION child{};
		if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED,
		                    nullptr, nullptr, &startup, &child)) {
			std::printf("rda: cannot start %s (error %lu)\n", shown(preview).c_str(), GetLastError());
			return 1;
		}
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		if (job) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
			AssignProcessToJobObject(job, child.hProcess);
		}
		SetConsoleCtrlHandler(keepWaiting, TRUE);
		ResumeThread(child.hThread);

		WaitForSingleObject(child.hProcess, INFINITE);
		DWORD code = 1;
		GetExitCodeProcess(child.hProcess, &code);
		CloseHandle(child.hThread);
		CloseHandle(child.hProcess);
		if (job) CloseHandle(job);
		return static_cast<int>(code);
#else
		// Replaced, not started: there is nothing for this process to do afterwards, and the
		// streams, the signals and the exit code are then the child's own.
		const std::string path = preview.string();
		std::vector<std::string> words;
		words.push_back(path);
		words.insert(words.end(), line.begin(), line.end());
		std::vector<char*> argv;
		for (std::string& word : words) argv.push_back(word.data());
		argv.push_back(nullptr);
		execv(path.c_str(), argv.data());
		std::printf("rda: cannot start %s\n", shown(preview).c_str());
		return 1;
#endif
	}

	// ---- a child to wait for ----------------------------------------------------------------

	namespace {
		std::atomic<bool> gInterrupted{ false };

#if defined(_WIN32)
		// One argument in the form CreateProcess parses back into the same argument: quoted when it
		// has a space or a quote, quotes escaped, and the backslashes before a quote doubled.
		std::wstring quoteArgument(const std::wstring& argument) {
			if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
			std::wstring out = L"\"";
			size_t backslashes = 0;
			for (const wchar_t c : argument) {
				if (c == L'\\') { ++backslashes; continue; }
				if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
				else           out.append(backslashes, L'\\');
				backslashes = 0;
				out += c;
			}
			out.append(backslashes * 2, L'\\');
			return out + L"\"";
		}

		std::wstring widen(const std::string& text) {
			if (text.empty()) return {};
			const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring out(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
			return out;
		}

		BOOL WINAPI noteInterrupt(DWORD event) {
			if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
			gInterrupted = true;
			return TRUE;
		}
#else
		void noteInterrupt(int) { gInterrupted = true; }
#endif
	}

	void watchForInterrupt() {
#if defined(_WIN32)
		SetConsoleCtrlHandler(noteInterrupt, TRUE);
#else
		struct sigaction action{};
		action.sa_handler = noteInterrupt;
		sigemptyset(&action.sa_mask);
		sigaction(SIGINT, &action, nullptr);
		sigaction(SIGTERM, &action, nullptr);
#endif
	}

	bool interrupted() { return gInterrupted; }

	struct Child::Handles {
#if defined(_WIN32)
		HANDLE process = nullptr;
		HANDLE job = nullptr;
#else
		pid_t pid = -1;
#endif
	};

	bool Child::start(const std::vector<std::string>& command, const fs::path& directory) {
		stop();
		if (command.empty()) return false;
		mExitCode = 0;
		mHandles = new Handles();

#if defined(_WIN32)
		std::wstring line;
		for (const std::string& word : command) line += (line.empty() ? L"" : L" ") + quoteArgument(widen(word));

		lendToChild(STD_INPUT_HANDLE);
		lendToChild(STD_OUTPUT_HANDLE);
		lendToChild(STD_ERROR_HANDLE);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

		// Suspended until it is in a job that dies with this process, as forwardToPreview does
		// it, and for the same reason: a backend left open by a tool nobody is waiting for any
		// more is a window on somebody's desktop with nothing to close it.
		PROCESS_INFORMATION child{};
		const std::wstring where = directory.wstring();
		if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr,
		                    where.empty() ? nullptr : where.c_str(), &startup, &child)) {
			std::printf("rda: cannot start `%s` (error %lu) -- is %s installed and on the PATH?\n",
			            command.front().c_str(), GetLastError(), command.front().c_str());
			delete mHandles;
			mHandles = nullptr;
			return false;
		}
		mHandles->job = CreateJobObjectW(nullptr, nullptr);
		if (mHandles->job) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(mHandles->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
			AssignProcessToJobObject(mHandles->job, child.hProcess);
		}
		mHandles->process = child.hProcess;
		ResumeThread(child.hThread);
		CloseHandle(child.hThread);
		return true;
#else
		std::vector<std::string> words = command;
		std::vector<char*> argv;
		for (std::string& word : words) argv.push_back(word.data());
		argv.push_back(nullptr);
		const std::string where = directory.string();

		// Standard streams are flushed first: what is buffered now would otherwise be written
		// twice, once by each side of the fork.
		std::fflush(stdout);
		const pid_t pid = fork();
		if (pid < 0) {
			std::printf("rda: cannot start `%s`\n", command.front().c_str());
			delete mHandles;
			mHandles = nullptr;
			return false;
		}
		if (pid == 0) {
			if (!where.empty() && chdir(where.c_str()) != 0) _exit(127);
			execvp(argv[0], argv.data());
			// Past this, the program was not found; 127 is the shell's word for it.
			_exit(127);
		}
		mHandles->pid = pid;
		return true;
#endif
	}

	bool Child::running() {
		if (!mHandles) return false;
#if defined(_WIN32)
		DWORD code = 0;
		if (WaitForSingleObject(mHandles->process, 0) == WAIT_TIMEOUT) return true;
		GetExitCodeProcess(mHandles->process, &code);
		mExitCode = static_cast<int>(code);
		CloseHandle(mHandles->process);
		if (mHandles->job) CloseHandle(mHandles->job);
#else
		int status = 0;
		const pid_t done = waitpid(mHandles->pid, &status, WNOHANG);
		if (done == 0) return true;
		mExitCode = (done > 0 && WIFEXITED(status)) ? WEXITSTATUS(status)
		          : (done > 0 && WIFSIGNALED(status)) ? 128 + WTERMSIG(status) : 1;
#endif
		delete mHandles;
		mHandles = nullptr;
		return false;
	}

	void Child::stop() {
		if (!mHandles) return;
#if defined(_WIN32)
		// There is no polite way to ask a console program to end that does not also ask this
		// one: a Ctrl+C event goes to the whole console. So it is given the moment it would
		// have had to finish by itself -- the Ctrl+C that brought us here reached it too --
		// and then the job it is in is ended, with anything it started.
		if (WaitForSingleObject(mHandles->process, 1500) == WAIT_TIMEOUT) {
			if (mHandles->job) TerminateJobObject(mHandles->job, 1);
			else TerminateProcess(mHandles->process, 1);
			WaitForSingleObject(mHandles->process, 3000);
		}
#else
		kill(mHandles->pid, SIGTERM);
		for (int waited = 0; waited < 30; ++waited) {
			int status = 0;
			if (waitpid(mHandles->pid, &status, WNOHANG) != 0) { delete mHandles; mHandles = nullptr; return; }
			usleep(50 * 1000);
		}
		kill(mHandles->pid, SIGKILL);
		int status = 0;
		waitpid(mHandles->pid, &status, 0);
#endif
		// Taken as ended: whatever it left as its code is not what anyone asked about.
		running();
		delete mHandles;
		mHandles = nullptr;
	}
}
