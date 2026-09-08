#pragma once
#include <filesystem>
#include <string>

// Where the engine itself is on disk, and what that is for.
//
// An application's assets resolve relative to the working directory, which is the right
// rule for an application: its layouts and its theme sit beside it. The engine's own
// assets -- the font it measures text with, the syntax definitions -- do not belong to
// the application, and copying them beside every one was the price of not knowing
// where the engine was. This answers that: the directory holding the module the engine
// code is in, which is the executable for a program that linked it and the shared
// library for a backend that opened it. A staged `bin/` holds both beside a `res/`.
namespace RDA {

	// The directory of the module containing the engine. Empty if the platform would
	// not say, which nothing here has seen but every caller tolerates.
	std::filesystem::path engineModuleDirectory();

	// The running executable, whatever it is, and its directory.
	std::filesystem::path executablePath();
	std::filesystem::path executableDirectory();

	// A path for one of the engine's own files. The path as given if it exists, so an
	// application that copies res/ beside itself is left alone; otherwise the same
	// relative path beside the engine module; otherwise as given, so the error that
	// follows names what was asked for rather than the last place looked.
	std::string resolveEngineAsset(const std::string& path);
}
