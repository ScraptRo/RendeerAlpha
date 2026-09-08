# The layout toolchain: `rda layout` compiles a .tsx into a blueprint, `rda types`
# emits the definitions layouts are written against. Both need the widget schema and
# QuickJS, which is why this is a program and not a shell script.
#
# The intent is that a build invokes it rather than a person doing so by hand, the way
# shaders are compiled without anyone thinking about it.
add_executable(rda rda.cpp)
target_link_libraries(rda PRIVATE rendeer)

# Its own directory, apart from the one runtime outputs share. A directory that holds
# the engine's shared library is a directory a Python package `rda/` may be copied into
# -- the binding tests do exactly that -- and on Linux a tool named `rda` in the same
# place is where that copy has to go: it fails onto the executable with "Error copying
# directory", which names neither. Everything that runs the tool does so through
# $<TARGET_FILE:rda>, so where it lives is nobody else's concern; `stage` copies it into
# bin/ beside the library, where no package is ever put.
set_target_properties(rda PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/$<CONFIG>")
