#include "Forward.h"

#include <Core/Location.h>

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
}
