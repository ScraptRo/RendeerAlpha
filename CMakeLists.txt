cmake_minimum_required(VERSION 3.21)

# RendeerAlpha — glue between an application's backend and its interface.
#
# An interface is written in TypeScript, compiled ahead of time into a flat blueprint,
# and loaded by an application that contains no parser and no JavaScript engine.
#
# This repository is the engine and its toolchain, and nothing else. Applications live
# outside it and point at it -- docs/setup/ is how one is written, and
# docs/architecture/ is how it consumes what is built here.
#
# Build it once with the bring-up script for the platform -- scripts/windows-bringup.bat
# or scripts/linux-bringup.sh -- which checks the toolchain, builds, tests, and stages
# bin/ for every backend that is not C++. Or directly:
#
#   cmake --preset default
#   cmake --build --preset debug
#   cmake --build build/default --config Debug --target stage
#
# There is one configuration. There used to be three tiers that each excluded the layer
# above; they are gone because the interface layer is the product, and because the
# footprint they protected is protected by static linking anyway — the linker leaves out
# a compiler that nothing calls. See RendeerAlpha/include/Core/BuildMode.h.

# 1.0: the interface layer is what this engine is, four languages have driven it, and the
# behaviour it has now is the behaviour it is promising. It builds and tests from a clone
# on Windows and Linux.
#
# 1.0 is not "nothing left to do" -- the known limits are listed, as they were before, in
# docs/architecture/where-things-stand.md, and none of them is a thing that has to change
# shape to be fixed. It is the point at which a colour is the colour you wrote, a package
# built against ABI 1.0 keeps working, and both of those are commitments rather than
# observations.
#
# The one break on the way here: theme colours used to be written into an sRGB surface
# undecoded, so every one of them reached the screen too light. They are decoded now, which
# means a palette picked by eye against the old behaviour renders differently -- correctly,
# for the first time. See docs/frontend/themes.md.
project(RendeerAlpha VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Everything is compiled position-independent, because the engine's main way out of
# C++ is a shared library: rendeer_c is a .so that carries the whole static engine
# inside it, and on Linux a .so cannot be made from objects that were not built for
# it -- the linker refuses, naming a thread-local in stb_image as the first offender.
# Windows has no such distinction, which is why a year of builds there never noticed.
# The cost on x86-64 is a register-relative addressing mode, which is to say nothing.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC puts /W3 in the default C flags. The only C in this project is vendored QuickJS,
# compiled with warnings off; leaving the default in place means every one of its files
# reports D9025 for the pair. Removing it here is tidier than silencing the complaint
# about silencing the warnings.
if(MSVC)
	string(REPLACE "/W3" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

option(RDA_BUILD_TESTS   "Build the test runner" ON)
option(RDA_BUILD_TOOLS   "Build the layout toolchain" ON)

# An embedded JavaScript interpreter at run time. Off, because "nothing in a shipped
# binary evaluates JavaScript" is a promise this engine makes, and this is the switch
# that qualifies it. See RendeerAlpha/CMakeLists.txt for the rest of the reasoning.
option(RDA_ENABLE_SCRIPTING "Build the run-time JavaScript host" OFF)

# Recompiling layouts from their .tsx needs esbuild, and esbuild needs node. A
# machine building from a source tree may have neither, so this can be turned off:
# the committed blueprints are used as they are, and the build says so rather than
# failing on a tool it was never given.
option(RDA_COMPILE_LAYOUTS "Recompile .tsx layouts during the build" ON)

# Single-config generators need the build type spelled out; multi-config ones ignore it.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# One directory per configuration, so the assets copied beside an executable are shared
# by every executable that needs them.
set(RDA_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RDA_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

include(cmake/RdaDependencies.cmake)
include(cmake/RdaEsbuild.cmake)
include(cmake/RdaLayouts.cmake)
include(cmake/RdaState.cmake)

add_subdirectory(RendeerAlpha)

if(RDA_BUILD_TOOLS)
	add_subdirectory(Cli)
endif()

# ---- bin/: the engine, laid out for everything that is not C++ ----------------------
#
# A C++ application adds this checkout as a subdirectory and gets a library. Every other
# backend -- Python, Node, C#, anything with an FFI -- wants a shared library, the tool
# that compiles its interface, and the engine's own assets, in one place it can point
# at. That place is bin/ at the root of this checkout, and `stage` is what fills it:
#
#   cmake --build build/default --config Debug --target stage
#
# The bring-up scripts run it after the build. It is not an install: nothing goes
# outside this tree, so deleting the checkout deletes everything, and a second checkout
# beside it is a second engine with nothing shared. Only offered when this is the
# project being built -- an application that added the engine as a subdirectory has no
# business staging it.
if(PROJECT_IS_TOP_LEVEL AND RDA_BUILD_TOOLS AND RDA_BUILD_C_API)
	set(RDA_STAGE_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
		"Where `stage` lays out the built engine for backends that are not C++")

	# Which configuration is in there, for anyone looking at the folder later.
	file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/stage/$<CONFIG>/BUILD.txt" CONTENT
		"RendeerAlpha ${PROJECT_VERSION} $<CONFIG> ${CMAKE_SYSTEM_NAME} ${CMAKE_CXX_COMPILER_ID}\n")

	set(_rda_stage_extra "")
	if(MSVC)
		# The import library, for a C or C++ program that links the DLL rather than the
		# static engine; and the symbols, so a crash inside the engine has names.
		set(_rda_stage_extra
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_LINKER_FILE:rendeer_c>" "${RDA_STAGE_DIR}/"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_PDB_FILE:rendeer_c>" "${RDA_STAGE_DIR}/")
	endif()

	add_custom_target(stage
		COMMAND ${CMAKE_COMMAND} -E make_directory "${RDA_STAGE_DIR}/include"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rendeer_c>" "${RDA_STAGE_DIR}/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rda>" "${RDA_STAGE_DIR}/"
		${_rda_stage_extra}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_SOURCE_DIR}/RendeerAlpha/include/RendeerC.h" "${RDA_STAGE_DIR}/include/"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_SOURCE_DIR}/RendeerAlpha/res" "${RDA_STAGE_DIR}/res"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_SOURCE_DIR}/cmake/bin-README.md" "${RDA_STAGE_DIR}/README.md"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_BINARY_DIR}/stage/$<CONFIG>/BUILD.txt" "${RDA_STAGE_DIR}/BUILD.txt"
		COMMAND ${CMAKE_COMMAND} -E echo "Staged the $<CONFIG> engine into ${RDA_STAGE_DIR}"
		DEPENDS rendeer_c rda
		COMMENT "Staging the engine into ${RDA_STAGE_DIR}"
		VERBATIM
	)
endif()
if(RDA_BUILD_TESTS)
	# At the top level, so `ctest` run from the build directory finds the tests. Called
	# in a subdirectory it registers them somewhere ctest never looks.
	enable_testing()
	add_subdirectory(RuntimeTests)

	# The bindings, exercised against a running engine. Needs the shared library they
	# open, and a device and a window, which is why it is its own tier rather than more
	# cases in the runner above.
	#
	# Their fixture is compiled from .tsx at build time, and unlike an application this
	# repository commits no blueprints to fall back on -- so with no esbuild there is
	# nothing for these tests to load. Skipped with a reason, rather than failing the
	# whole build on a tool the machine was never given: a fresh clone without
	# `npm install` used to stop here, which is the one thing a first build must not do.
	if(RDA_BUILD_C_API AND RDA_BUILD_TOOLS AND RDA_ESBUILD_EXECUTABLE)
		add_subdirectory(bindings/tests)
	elseif(NOT RDA_ESBUILD_EXECUTABLE)
		message(STATUS
			"Binding tests: skipped -- their fixture needs esbuild to compile. "
			"Install node, then run 'npm install' in this checkout, to enable them.")
	else()
		message(STATUS
			"Binding tests: skipped -- they need RDA_BUILD_C_API (the library) and "
			"RDA_BUILD_TOOLS (the generators)")
	endif()
endif()
