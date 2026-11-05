cmake_minimum_required(VERSION 3.21)
project(@SLUG@ VERSION 0.1.0 LANGUAGES C CXX)

# Where the RendeerAlpha checkout is. Relative paths are read from this folder.
set(RDA_ENGINE_DIR "@ENGINE@" CACHE PATH "Path to the RendeerAlpha checkout")
get_filename_component(RDA_ENGINE_DIR "${RDA_ENGINE_DIR}" ABSOLUTE
                       BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${RDA_ENGINE_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "No RendeerAlpha checkout at ${RDA_ENGINE_DIR}. "
	                    "Pass -DRDA_ENGINE_DIR=<path>.")
endif()

# The engine's own tests are not your business. Its toolchain is: `rda` is what
# compiles the layouts, and without it there is nothing to build them with.
set(RDA_BUILD_TESTS OFF)
set(RDA_BUILD_TOOLS ON)

# One directory per configuration, holding the executable and its assets together.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

add_subdirectory("${RDA_ENGINE_DIR}" "${CMAKE_BINARY_DIR}/rendeer")

add_executable(@SLUG@ main.cpp)
target_link_libraries(@SLUG@ PRIVATE rendeer)

# A development build is told where the sources are, so hot reload watches the file
# you are editing rather than the copy beside the binary. A release build is not.
target_compile_definitions(@SLUG@ PRIVATE
	$<$<CONFIG:Debug>:APP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">)

rda_add_state(@SLUG@   res/state.ts)
rda_add_theme(@SLUG@   res/themes/app.ts)
rda_add_types(@SLUG@   res/layouts/rda.d.ts res/themes/app.ts res/state.ts)
rda_add_layouts(@SLUG@ res/layouts/main.tsx)

# Assets resolve relative to the working directory, not the executable, so they are
# copied beside it and the program is started from there.
add_custom_command(TARGET @SLUG@ POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/res" "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/res"
	VERBATIM)
