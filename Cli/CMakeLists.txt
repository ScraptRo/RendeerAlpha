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
#
# Every file in commands/ is compiled, with no list to add it to: a command registers itself, so
# that adding one touches nothing shared. README.md is how to write one. An object library rather
# than a static one, because nothing refers to a command by name, and a linker drops whatever
# nothing refers to.
file(GLOB RDA_CLI_COMMANDS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/commands/*.cpp")
add_library(rda_commands OBJECT
	core/Command.cpp
	core/Common.cpp
	core/Forward.cpp
	core/Starters.cpp
	${RDA_CLI_COMMANDS})
target_include_directories(rda_commands PUBLIC core PRIVATE preview)
target_link_libraries(rda_commands PUBLIC rendeer)

# What `rda --check-commands` holds the registered commands against.
set(RDA_CLI_COMMAND_NAMES "")
foreach(command_file IN LISTS RDA_CLI_COMMANDS)
	get_filename_component(command_name "${command_file}" NAME)
	list(APPEND RDA_CLI_COMMAND_NAMES "${command_name}")
endforeach()
list(JOIN RDA_CLI_COMMAND_NAMES "," RDA_CLI_COMMAND_NAMES)
set_source_files_properties(core/Command.cpp PROPERTIES
	COMPILE_DEFINITIONS "RDA_CLI_COMMAND_FILES=\"${RDA_CLI_COMMAND_NAMES}\"")
# `rda doctor` opens the Vulkan loader by name at run time, so that this tool still links no
# GPU code. dlopen is in libdl on older glibc; the list is empty where it is part of libc.
target_link_libraries(rda_commands PRIVATE ${CMAKE_DL_LIBS})
# Which engine this is, for `rda doctor`: the project's version, so it is never a second
# number to keep in step.
target_compile_definitions(rda_commands PRIVATE RDA_VERSION_STRING="${RendeerAlpha_VERSION}")

# What `rda init` writes, as the files themselves under templates/ so that each reads and
# edits as what it is -- a Program.cs, a CMakeLists.txt -- and not as a string inside a
# .cpp. They are embedded with the same function the JavaScript preludes are: it takes
# the bytes of any file and does not care that the name says js. Only the commands need
# them, and the one command at that, so the include directory is theirs and not the tools'.
rda_embed_js(InitState        "${CMAKE_CURRENT_SOURCE_DIR}/templates/common/state.ts")
rda_embed_js(InitTheme        "${CMAKE_CURRENT_SOURCE_DIR}/templates/common/theme.ts")
rda_embed_js(InitLayout       "${CMAKE_CURRENT_SOURCE_DIR}/templates/common/main.tsx")
rda_embed_js(InitTsconfig     "${CMAKE_CURRENT_SOURCE_DIR}/templates/common/tsconfig.json")
rda_embed_js(InitGitignore    "${CMAKE_CURRENT_SOURCE_DIR}/templates/common/gitignore")
rda_embed_js(InitPython       "${CMAKE_CURRENT_SOURCE_DIR}/templates/python/app.py")
rda_embed_js(InitNode         "${CMAKE_CURRENT_SOURCE_DIR}/templates/node/app.mjs")
rda_embed_js(InitNodePackage  "${CMAKE_CURRENT_SOURCE_DIR}/templates/node/package.json")
rda_embed_js(InitCSharp       "${CMAKE_CURRENT_SOURCE_DIR}/templates/csharp/Program.cs")
rda_embed_js(InitCSharpProject "${CMAKE_CURRENT_SOURCE_DIR}/templates/csharp/project.csproj")
rda_embed_js(InitCppMain      "${CMAKE_CURRENT_SOURCE_DIR}/templates/cpp/main.cpp")
rda_embed_js(InitCppCMake     "${CMAKE_CURRENT_SOURCE_DIR}/templates/cpp/CMakeLists.txt")
# What `rda widgets` prints about a widget is the documentation's own section on it, so that
# there is one description to keep true.
set(RDA_ELEMENT_DOCS "${CMAKE_CURRENT_SOURCE_DIR}/../docs/frontend/elements")
rda_embed_js(DocContainers "${RDA_ELEMENT_DOCS}/containers.md")
rda_embed_js(DocText       "${RDA_ELEMENT_DOCS}/text-and-input.md")
rda_embed_js(DocChoices    "${RDA_ELEMENT_DOCS}/choices-and-pages.md")
rda_embed_js(DocList       "${RDA_ELEMENT_DOCS}/list.md")
rda_embed_js(DocDocking    "${RDA_ELEMENT_DOCS}/docking.md")
rda_embed_js(DocStream     "${RDA_ELEMENT_DOCS}/stream.md")
rda_embed_js(DocViewport   "${RDA_ELEMENT_DOCS}/viewport.md")
target_include_directories(rda_commands PRIVATE "${RDA_GENERATED_DIR}")

add_executable(rda main.cpp)
target_link_libraries(rda PRIVATE rda_commands)

add_executable(rda-preview
	preview/PreviewMain.cpp
	preview/Preview.cpp
	preview/PreviewOptions.cpp
	preview/InputScript.cpp
	preview/Editor.cpp)
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
