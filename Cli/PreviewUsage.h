// What `prototype` and `shot` say of themselves in a usage text, in one place because two
// programs print it: `rda`, whose list of commands still names them -- they are what people
// type, and they are forwarded -- and `rda-preview`, which is what runs them.
#pragma once

namespace Cli {

	inline constexpr const char* kPrototypeUsage =
		"  rda prototype <project> [--layout <name>] [--set n=v] [--rows t=n]\n"
		"                          [--data <file.json>] [--no-build]\n"
		"                                     open the interface with placeholder state\n";

	inline constexpr const char* kShotUsage =
		"  rda shot   <project> [--out <file.png>] [--size WxH] [--scale <n>]\n"
		"                       [--frames <n>] [--every <ms>] [--settle <n>]\n"
		"                       [--hover X,Y|id] [--press X,Y|id] [--scroll <n>]\n"
		"                       [--key <chord>]... [--type <text>]...\n"
		"                       [--drag X1,Y1,X2,Y2 [--hold]] [--files <a;b>]\n"
		"                       [--click [Ctrl+][Shift+]X,Y|id]...\n"
		"                       [--a11y <tree.txt>] [--compose <text>]\n"
		"                                                          + the above\n"
		"                                     the same with no window: writes a .png\n";
}
