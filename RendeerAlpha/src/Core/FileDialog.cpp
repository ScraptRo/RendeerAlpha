#include <Core/FileDialog.h>
#include <Logger/Logger.h>

#include <string>
#include <vector>

// Asking the person where something is.
//
// The engine has no dialogs of its own and should not grow any: a file chooser that does
// not look like every other file chooser on the machine is worse than no file chooser,
// because the reader already knows how theirs works. So this is a thin call into the
// platform's, and nothing here draws.
//
// It exists because the alternative is what applications were actually doing -- importing
// a second GUI toolkit for one dialog, running it on a thread of its own, and hoping the
// two event loops stay out of each other's way.
//
// Blocking is deliberate and safe. These run on the caller's thread, not the engine's:
// the C ABI calls them without hopping to the loop, so the window goes on drawing behind
// the dialog. Everything else in that ABI marshals to the loop and waits, which here
// would freeze the interface for as long as somebody took to find their file.

#if defined(_WIN32)

	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <shobjidl.h>

namespace {
	std::wstring widen(const std::string& text) {
		if (text.empty()) return {};
		const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
		                                     static_cast<int>(text.size()), nullptr, 0);
		std::wstring out(static_cast<size_t>(size), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
		                    out.data(), size);
		return out;
	}

	std::string narrow(const wchar_t* text) {
		if (!text || !*text) return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1) return {};
		std::string out(static_cast<size_t>(size - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
		return out;
	}

	// One dialog, folders or files. IFileDialog rather than the old GetOpenFileName: it
	// is the one the rest of Windows uses, so it remembers places and looks right.
	bool runDialog(bool folders, const std::string& title, const std::string& start,
	               const std::string& filter, std::string& chosen) {
		// Apartment-threaded, and tolerant of a caller that already initialised: an
		// application may well have done so, and RPC_E_CHANGED_MODE is not our failure.
		const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		const bool weInitialised = SUCCEEDED(init);

		IFileOpenDialog* dialog = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		                              IID_PPV_ARGS(&dialog));
		if (FAILED(hr) || !dialog) {
			if (weInitialised) CoUninitialize();
			return false;
		}

		if (folders) {
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS);
		}

		const std::wstring wideTitle = widen(title);
		if (!wideTitle.empty()) dialog->SetTitle(wideTitle.c_str());

		if (!start.empty()) {
			const std::wstring wideStart = widen(start);
			IShellItem* folder = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(wideStart.c_str(), nullptr,
			                                          IID_PPV_ARGS(&folder))) && folder) {
				dialog->SetFolder(folder);
				folder->Release();
			}
		}

		// "Images|*.png;*.jpg" -- one kind, plus "every file" so the reader is never
		// stuck behind a filter that hides what they came for.
		std::wstring wideDesc, widePattern;
		std::vector<COMDLG_FILTERSPEC> specs;
		if (!folders && !filter.empty()) {
			const size_t bar = filter.find('|');
			wideDesc = widen(bar == std::string::npos ? filter : filter.substr(0, bar));
			widePattern = widen(bar == std::string::npos ? "*.*" : filter.substr(bar + 1));
			specs.push_back(COMDLG_FILTERSPEC{ wideDesc.c_str(), widePattern.c_str() });
		}
		if (!folders) {
			specs.push_back(COMDLG_FILTERSPEC{ L"All files", L"*.*" });
			dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
		}

		// The window is the owner, so the dialog is modal to the application rather than
		// a stray box the reader can lose behind it.
		hr = dialog->Show(GetActiveWindow());
		bool answered = false;
		if (SUCCEEDED(hr)) {
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item)) && item) {
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
					chosen = narrow(path);
					answered = !chosen.empty();
					CoTaskMemFree(path);
				}
				item->Release();
			}
		}
		// Cancelled is HRESULT_FROM_WIN32(ERROR_CANCELLED), and it is an answer: the
		// caller is told "nothing", not "something went wrong".
		dialog->Release();
		if (weInitialised) CoUninitialize();
		return answered;
	}
}

namespace RDA {
	bool pickFolder(const std::string& title, const std::string& start, std::string& chosen) {
		return runDialog(true, title, start, {}, chosen);
	}
	bool pickFile(const std::string& title, const std::string& start,
	              const std::string& filter, std::string& chosen) {
		return runDialog(false, title, start, filter, chosen);
	}
	bool fileDialogsAvailable() { return true; }
}

#else

	#include <array>
	#include <cstdio>

namespace {
	// No X11 or GTK dependency: the desktop's own helper, asked over a pipe. zenity is on
	// GNOME and most things that follow it, kdialog on KDE. Either is a package away, and
	// linking a toolkit to avoid a fork would cost every application that never opens a
	// dialog.
	bool runCommand(const std::string& command, std::string& chosen) {
		FILE* pipe = popen(command.c_str(), "r");
		if (!pipe) return false;
		std::array<char, 4096> buffer{};
		std::string out;
		while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) out += buffer.data();
		const int status = pclose(pipe);
		while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
		if (status != 0 || out.empty()) return false;   // cancelled, or no helper
		chosen = out;
		return true;
	}

	bool haveCommand(const char* name) {
		const std::string probe = std::string("command -v ") + name + " >/dev/null 2>&1";
		return std::system(probe.c_str()) == 0;
	}

	// Single-quoted for the shell, with the one escape that matters inside single quotes.
	std::string quoted(const std::string& text) {
		std::string out = "'";
		for (const char c : text) {
			if (c == '\'') out += "'\\''";
			else out += c;
		}
		return out + "'";
	}

	bool ask(bool folders, const std::string& title, const std::string& start,
	         const std::string& filter, std::string& chosen) {
		if (haveCommand("zenity")) {
			std::string command = "zenity --file-selection";
			if (folders) command += " --directory";
			if (!title.empty()) command += " --title=" + quoted(title);
			if (!start.empty()) command += " --filename=" + quoted(start + "/");
			if (!folders && !filter.empty()) {
				// zenity wants "Name | pat pat"; the ABI's form is "Name|pat;pat".
				const size_t bar = filter.find('|');
				std::string name = (bar == std::string::npos) ? filter : filter.substr(0, bar);
				std::string patterns = (bar == std::string::npos) ? "*" : filter.substr(bar + 1);
				for (char& c : patterns) if (c == ';') c = ' ';
				command += " --file-filter=" + quoted(name + " | " + patterns);
			}
			command += " 2>/dev/null";
			return runCommand(command, chosen);
		}
		if (haveCommand("kdialog")) {
			std::string command = "kdialog";
			command += folders ? " --getexistingdirectory " : " --getopenfilename ";
			command += quoted(start.empty() ? "." : start);
			if (!title.empty()) command += " --title " + quoted(title);
			command += " 2>/dev/null";
			return runCommand(command, chosen);
		}
		RDA_LOG_WARNING("No file dialog on this desktop: install zenity or kdialog, or ask "
		                "for the path another way.");
		return false;
	}
}

namespace RDA {
	bool pickFolder(const std::string& title, const std::string& start, std::string& chosen) {
		return ask(true, title, start, {}, chosen);
	}
	bool pickFile(const std::string& title, const std::string& start,
	              const std::string& filter, std::string& chosen) {
		return ask(false, title, start, filter, chosen);
	}
	bool fileDialogsAvailable() { return haveCommand("zenity") || haveCommand("kdialog"); }
}

#endif
