#pragma once
#include <string>

// The platform's own file and folder choosers.
//
// A dialog that does not look like every other dialog on the machine is worse than none,
// because the reader already knows how theirs works -- so the engine draws nothing here
// and asks the platform. On Windows that is IFileOpenDialog; elsewhere it is zenity or
// kdialog over a pipe, which keeps a toolkit dependency out of every application that
// never opens one.
//
// **These block the calling thread**, because a dialog is a question. They must not be
// called from the loop thread: the window would stop drawing for as long as somebody took
// to find their file. The C ABI calls them from the caller's own thread for that reason,
// which is the one place in that ABI that does not hop to the loop.
//
// False means the reader cancelled, or there was no chooser to show -- and cancelling is
// an answer rather than a failure, so neither is logged as an error.
namespace RDA {

	bool pickFolder(const std::string& title, const std::string& start, std::string& chosen);

	// `filter` names one kind as "Description|pattern;pattern" -- "Images|*.png;*.jpg".
	// Empty means every file. "All files" is always offered as well, so a filter cannot
	// hide what somebody came for.
	bool pickFile(const std::string& title, const std::string& start,
	              const std::string& filter, std::string& chosen);

	// Whether there is a chooser to show at all. Always true on Windows; elsewhere it
	// depends on what the desktop has installed.
	bool fileDialogsAvailable();
}
