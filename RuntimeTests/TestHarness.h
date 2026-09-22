#pragma once
#include <cstdio>
#include <string>
#include <vector>

// A test harness small enough to read in one sitting.
//
// No framework is vendored for this. The engine vendors what it cannot write (Vulkan,
// GLFW, stb, shaderc, QuickJS) and writes what it can, and a registry of functions that
// returns a non-zero exit code is firmly in the second category. It is also the only
// shape a build server needs: run the exe, look at the code.
//
// The macros here are the preprocessor doing what only the preprocessor can — capturing
// the file, the line and the *text* of the expression that failed. That is worth a macro;
// wrapping ordinary logic in one is not.
namespace test {

	struct Case {
		const char* name;
		void (*fn)();
	};

	inline std::vector<Case>& registry() {
		static std::vector<Case> cases;
		return cases;
	}

	// Failures of the current case, collected rather than thrown: one assertion failing
	// is rarely the whole story, and stopping at the first hides the shape of the break.
	inline std::vector<std::string>& failures() {
		static std::vector<std::string> f;
		return f;
	}

	struct Registrar {
		Registrar(const char* name, void (*fn)()) { registry().push_back({ name, fn }); }
	};

	inline void fail(const char* file, int line, const std::string& what) {
		char buffer[1024];
		std::snprintf(buffer, sizeof(buffer), "    %s:%d: %s", file, line, what.c_str());
		failures().push_back(buffer);
	}

	inline int runAll() {
		size_t passed = 0;
		std::vector<std::string> failedNames;
		for (const Case& c : registry()) {
			failures().clear();
			c.fn();
			if (failures().empty()) {
				std::printf("  PASS  %s\n", c.name);
				++passed;
			} else {
				std::printf("  FAIL  %s\n", c.name);
				for (const std::string& f : failures()) std::printf("%s\n", f.c_str());
				failedNames.push_back(c.name);
			}
		}
		std::printf("\n%zu/%zu passed\n", passed, registry().size());
		if (!failedNames.empty()) {
			std::printf("failed:\n");
			for (const std::string& n : failedNames) std::printf("  %s\n", n.c_str());
		}
		return failedNames.empty() ? 0 : 1;
	}
}

#define TEST(name)                                                    \
	static void name();                                               \
	static ::test::Registrar registrar_##name(#name, &name);          \
	static void name()

#define CHECK(cond)                                                   \
	do {                                                              \
		if (!(cond)) ::test::fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
	} while (0)

// The same, for two strings. CHECK_EQ goes through std::to_string, which has nothing
// to say about one -- and a comparison that fails without printing what it got is the
// kind of test that costs an hour to read.
#define CHECK_STR(actual, expected)                                   \
	do {                                                              \
		const std::string actual_ = (actual);                         \
		const std::string expected_ = (expected);                     \
		if (actual_ != expected_) {                                   \
			::test::fail(__FILE__, __LINE__,                          \
				std::string(#actual) + "\n        got  " + actual_ +    \
				"\n        want " + expected_);                        \
		}                                                             \
	} while (0)

#define CHECK_EQ(actual, expected)                                    \
	do {                                                              \
		const auto actual_ = (actual);                                \
		const auto expected_ = (expected);                            \
		if (!(actual_ == expected_)) {                                \
			::test::fail(__FILE__, __LINE__,                          \
				std::string(#actual) + " == " + #expected + " (got " +\
				std::to_string(actual_) + ", want " +                 \
				std::to_string(expected_) + ")");                     \
		}                                                             \
	} while (0)
