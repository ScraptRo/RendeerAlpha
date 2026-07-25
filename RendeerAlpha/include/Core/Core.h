#pragma once
#include <vendor/RDA_Library/stack_list.h>
namespace RDA {
	void InitGlfw();
	void endGlfw();

	extern stack_list windowList;
}