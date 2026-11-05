# The layout toolchain: `rda layout` compiles a .tsx into a blueprint, `rda types`
# emits the definitions layouts are written against. Both need the widget schema and
# QuickJS, which is why this is a program and not a shell script.
#
# The intent is that a build invokes it rather than a person doing so by hand, the way
# shaders are compiled without anyone thinking about it.
#
# Two programs. `rda` is the commands that compile, which need no device and no window --
# a build step on a machine with no GPU is the ordinary case. `rda-preview` is `prototype`
# and `shot`, which open a window and need both; `rda` forwards those two to the one beside
# it, so they are still typed as `rda prototype` and `rda shot`. The preview compiles the
# project before it shows it, with the same code `rda build` runs, so both are made of the
# commands below.
add_library(rda_commands STATIC
	Common.cpp
	CmdCompile.cpp
	CmdState.cpp
	CmdBuild.cpp)
target_link_libraries(rda_commands PUBLIC rendeer)

add_executable(rda
	main.cpp
	Forward.cpp)
target_link_libraries(rda PRIVATE rda_commands)

add_executable(rda-preview
	PreviewMain.cpp
	Preview.cpp
	PreviewOptions.cpp
	InputScript.cpp)
target_link_libraries(rda-preview PRIVATE rda_commands)

# UTF-8 as the process code page, so a non-Latin argument reaches the tool intact. MSVC
# embeds a .manifest listed among the sources; elsewhere there is nothing to do.
if(WIN32)
	target_sources(rda PRIVATE rda.manifest)
	target_sources(rda-preview PRIVATE rda.manifest)
endif()

# Its own directory, apart from the one runtime outputs share. A directory that holds
# the engine's shared library is a directory a Python package `rda/` may be copied into
# -- the binding tests do exactly that -- and on Linux a tool named `rda` in the same
# place is where that copy has to go: it fails onto the executable with "Error copying
# directory", which names neither. Everything that runs the tool does so through
# $<TARGET_FILE:rda>, so where it lives is nobody else's concern; `stage` copies it into
# bin/ beside the library, where no package is ever put. `rda-preview` is in the same
# directory, because `rda` looks for it beside itself.
set_target_properties(rda rda-preview PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/$<CONFIG>")
