#include "TestHarness.h"

#include <cstdio>

// The test runner's entry point, and nothing else.
//
// It used to live at the bottom of the wire-protocol tests, which was fine while those
// were the only tests here and wrong by the time they were a third of them -- deleting
// that file took `main` with it. A file that is only the entry point cannot be lost by
// removing a subject.
int main() {
	std::printf("rendeer tests\n\n");
	return test::runAll();
}
