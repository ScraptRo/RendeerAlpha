cmake_minimum_required(VERSION 3.21)

# RendeerAlpha — glue between an application's backend and its interface.
#
# An interface is written in TypeScript, compiled ahead of time into a flat blueprint,
# and loaded by an application that contains no parser and no JavaScript engine.
#
# Build it with the scripts in scripts/, or directly:
#
#   cmake --preset default
#   cmake --build --preset debug
#
# There is one configuration. There used to be three tiers that each excluded the layer
# above; they are gone because the interface layer is the product, and because the
# footprint they protected is protected by static linking anyway — the linker leaves out
# a compiler that nothing calls. See RendeerAlpha/include/Core/BuildMode.h.

project(RendeerAlpha VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# MSVC puts /W3 in the default C flags. The only C in this project is vendored QuickJS,
# compiled with warnings off; leaving the default in place means every one of its files
# reports D9025 for the pair. Removing it here is tidier than silencing the complaint
# about silencing the warnings.
if(MSVC)
	string(REPLACE "/W3" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

option(RDA_BUILD_SANDBOX "Build the Sandbox example" ON)
option(RDA_BUILD_TESTS   "Build the test runner" ON)
option(RDA_BUILD_TOOLS   "Build the layout toolchain" ON)

# Out-of-process hosting: a host process that owns the device, and applications as
# clients of it over a pipe. Off by default. An application that links the engine
# already has the device in its own process, which is where its backend wants the GPU
# to be; this exists for a preview that has to outlive a crash in what it previews.
option(RDA_ENABLE_REMOTE "Build the out-of-process host and client" OFF)

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
include(cmake/RdaLayouts.cmake)

add_subdirectory(RendeerAlpha)

if(RDA_BUILD_TOOLS)
	add_subdirectory(Cli)
endif()
if(RDA_BUILD_SANDBOX)
	add_subdirectory(Sandbox)
endif()
if(RDA_BUILD_TESTS)
	# At the top level, so `ctest` run from the build directory finds the tests. Called
	# in a subdirectory it registers them somewhere ctest never looks.
	enable_testing()
	add_subdirectory(RuntimeTests)
endif()
