# The engine library.
#
# One list, grouped by what the files do. Everything here is always compiled; the only
# conditional part is out-of-process hosting, which changes how an application is
# deployed rather than what it can do.

set(RDA_SOURCES
	src/RendeerAlpha.cpp
	src/Core/LoopWork.cpp
	src/Core/Signals.cpp
	src/Core/Commands.cpp
	src/Core/Json.cpp
	src/Core/Tables.cpp
	src/Core/Png.cpp
	src/Core/Utf8.cpp
	src/Core/Colour.cpp
	src/GraphicalObjects/MeshAsset.cpp
	src/Core/Location.cpp
	src/Core/FileDialog.cpp
	src/Core/Svg.cpp
	src/Core/Route.cpp
	src/GraphicalObjects/Images.cpp
	src/GraphicalObjects/Streams.cpp
	src/GraphicalObjects/Effects.cpp
	src/Logger/Logger.cpp

	src/GraphicalSrc/DeviceHandler.cpp
	src/GraphicalSrc/Swapchain.cpp
	src/GraphicalSrc/WindownSurface.cpp
	src/GraphicalSrc/Renderer.cpp
	src/GraphicalSrc/FrameBuffer.cpp
	src/GraphicalSrc/Shader.cpp
	src/GraphicalSrc/ShaderCompiler.cpp
	src/GraphicalSrc/GraphicsPipeline.cpp
	src/GraphicalSrc/Descriptors.cpp
	src/GraphicalSrc/MemoryBuffer.cpp

	src/GraphicalObjects/Window.cpp
	src/GraphicalObjects/WindowEdges.cpp
	src/GraphicalObjects/Input.cpp
	src/GraphicalObjects/Texture.cpp
	src/GraphicalObjects/Viewports.cpp
	src/GraphicalObjects/ViewportEvents.cpp

	src/vendor/stb_image/stb_image.cpp
	src/vendor/stb_truetype/stb_truetype.cpp
	src/vendor/tinyxml2/tinyxml2.cpp
	src/vendor/vma/vk_mem_alloc.cpp
)

# The 3D scene path. The direction of travel is for this to leave the engine entirely
# and live in the application that wants a 3D view, which is what it was always for.
list(APPEND RDA_SOURCES
	src/GraphicalSrc/ShadowPass.cpp
	src/GraphicalSrc/ForwardPass.cpp
	src/GraphicalSrc/SceneFrame.cpp
	src/GraphicalSrc/MaterialResources.cpp
	src/GraphicalObjects/Material.cpp
	src/GraphicalObjects/Scene.cpp
	src/GraphicalObjects/Mesh.cpp
	src/GraphicalObjects/Primitives.cpp
)

# The interface layer: the reason the engine exists.
list(APPEND RDA_SOURCES
	src/GraphicalSrc/FontAtlas.cpp
	src/GraphicalSrc/GuiRenderer.cpp
	src/GraphicalObjects/Gui.cpp
	src/GraphicalObjects/Motion.cpp
	src/GraphicalObjects/GuiWidgets.cpp
	src/GraphicalObjects/GuiTheme.cpp
	src/GraphicalObjects/Shortcut.cpp
	src/Platform/Accessibility.cpp
	src/Platform/AccessibilityLinux.cpp
	src/Platform/TextInput.cpp
	src/Platform/WindowFrame.cpp
	src/GraphicalObjects/Accessible.cpp
	src/GraphicalObjects/Widget.cpp
	src/GraphicalObjects/Syntax.cpp
	src/GraphicalObjects/Docking.cpp
	src/GraphicalObjects/DockTree.cpp
	src/GraphicalObjects/TileGrid.cpp

	src/Layout/Blueprint.cpp
	src/Layout/WidgetSchema.cpp
	src/Layout/ThemeSchema.cpp
	src/Layout/LayoutLoader.cpp
	src/Layout/ListView.cpp
	src/Layout/TreeView.cpp
	src/Layout/GridView.cpp
	src/Layout/TableOrder.cpp
	src/Layout/Selection.cpp
	src/Layout/LayoutHost.cpp
	src/Layout/Router.cpp
	src/Layout/Probe.cpp
	src/Layout/Expression.cpp
	src/Layout/Bindings.cpp
	# What a layout's functions run on: QuickJS, created the first time a blueprint that
	# carries bytecode is loaded, and never otherwise. See docs/architecture/runtime-code.md.
	src/Layout/ScriptHost.cpp
)

# ---- scripting at run time, off by default ----------------------------------------
# An embedded JavaScript interpreter, so an application can run script it was handed
# rather than only what it was compiled with.
#
# Off because of the claim this engine makes: a shipped binary evaluates no JavaScript.
# TypeScript is a build-time language here and what ships is a blueprint and a loader.
# Turning this on makes that "unless you asked for it" -- a weaker promise, and one
# worth making deliberately rather than by default.
#
# It used to be compiled into every build and called by nothing, which is not the same
# as being available.
set(RDA_SOURCES_SCRIPTING
	src/Scripting/Script.cpp
	src/Scripting/ScriptBindings.cpp
	src/Scripting/ScriptQueue.cpp
)

# The authoring layer. Compiled in, but linked into an application only if that
# application calls it -- which is why a program that merely loads blueprints does not
# carry the compiler or the JavaScript engine behind it.
list(APPEND RDA_SOURCES
	src/Layout/LayoutCompiler.cpp
	src/Layout/ModuleTransform.cpp
	src/Layout/ThemeCompiler.cpp
	src/Layout/MeshCompiler.cpp
	src/Layout/TypeEmitter.cpp
	src/Layout/StateSchema.cpp
	src/Layout/ExpressionParser.cpp
	# Which of a layout's functions run at run time, and the bytecode they ship as.
	src/Layout/ScriptCompiler.cpp
	# Placeholder state for a declaration with no backend behind it yet. Here rather than
	# with the loader, because it reads JSON with the same engine the compiler uses.
	src/Layout/Prototype.cpp
	# The theme and its meshes, reloaded when they change. In the authoring list because
	# that is exactly what it references -- with hot reload off it compiles to nothing and
	# brings neither compiler with it.
	src/Layout/StyleWatch.cpp
)

set(RDA_SOURCES_QUICKJS
	src/vendor/quickjs/quickjs.c
	src/vendor/quickjs/libregexp.c
	src/vendor/quickjs/libunicode.c
	src/vendor/quickjs/dtoa.c
)

if(RDA_ENABLE_SCRIPTING)
	list(APPEND RDA_SOURCES ${RDA_SOURCES_SCRIPTING})
	message(STATUS "Run-time scripting: enabled (this build evaluates JavaScript)")
endif()

# QuickJS as its own target rather than per-source flags: it is third-party C compiled
# with warnings off, and a warning level set per source fights the one the generator
# already put on the target.
add_library(rendeer_quickjs OBJECT ${RDA_SOURCES_QUICKJS})
target_include_directories(rendeer_quickjs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(rendeer_quickjs PRIVATE _GNU_SOURCE _CRT_SECURE_NO_WARNINGS)
if(WIN32)
	target_compile_definitions(rendeer_quickjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
if(MSVC)
	set_target_properties(rendeer_quickjs PROPERTIES COMPILE_OPTIONS "/W0")
else()
	target_compile_options(rendeer_quickjs PRIVATE -w)
endif()

add_library(rendeer STATIC ${RDA_SOURCES})
add_library(rendeer::rendeer ALIAS rendeer)
target_link_libraries(rendeer PRIVATE rendeer_quickjs)

# acorn, as bytes, for the layout compiler: it parses a bundled layout to find which
# functions a binding reaches. Generated at configure time from the vendored file, and
# an array rather than a string literal because MSVC refuses a literal past 64 KB.
# Regenerated whenever acorn.js is newer than the header.
set(RDA_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RDA_GENERATED_DIR "${RDA_GENERATED_DIR}" PARENT_SCOPE)
set(_rda_acorn "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/acorn/acorn.js")
set(_rda_acorn_header "${RDA_GENERATED_DIR}/AcornSource.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rda_acorn}")
if(NOT EXISTS "${_rda_acorn_header}" OR "${_rda_acorn}" IS_NEWER_THAN "${_rda_acorn_header}")
	file(READ "${_rda_acorn}" _rda_hex HEX)
	string(LENGTH "${_rda_hex}" _rda_hex_length)
	math(EXPR _rda_acorn_bytes "${_rda_hex_length} / 2")
	string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _rda_hex "${_rda_hex}")
	# Thirty-two to a line, so no compiler meets a line a megabyte long.
	string(REPEAT "0x[0-9a-f][0-9a-f]," 32 _rda_row)
	string(REGEX REPLACE "(${_rda_row})" "\\1\n" _rda_hex "${_rda_hex}")
	file(WRITE "${_rda_acorn_header}"
		"// Generated from src/vendor/acorn/acorn.js by RendeerAlpha/CMakeLists.txt. Do not edit.\n"
		"#pragma once\n#include <cstddef>\n"
		"static const unsigned char kAcornSource[] = {\n${_rda_hex}0x00 };\n"
		"static const size_t kAcornSourceSize = ${_rda_acorn_bytes};\n")
endif()
target_include_directories(rendeer PRIVATE "${RDA_GENERATED_DIR}")

# PUBLIC, so an application that links this is compiled as C++20 without having to know
# that it must. CMAKE_CXX_STANDARD covers this repository's own targets and nothing else,
# which held only while every application lived inside it.
target_compile_features(rendeer PUBLIC cxx_std_20)

target_include_directories(rendeer PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	# Public headers reach engine-internal ones (Window.h -> Swapchain.h -> Datatypes.h),
	# so a consumer needs src/ on its include path too. Worth fixing one day; recorded
	# here rather than quietly worked around in every consumer.
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	${RDA_VULKAN_INCLUDE}
	${RDA_GLFW_INCLUDE}
	${RDA_GLM_INCLUDE}
)

# _DEBUG is something MSVC defines by itself in a debug configuration; no other compiler
# does. The logger and the Vulkan validation layers are both gated on it, so without this
# a Debug build elsewhere would quietly have neither -- and the first thing anyone does
# on a new platform is look for the log.
target_compile_definitions(rendeer PUBLIC
	$<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:_DEBUG>)

# Hot reload watches files for changes -- worth the stat() calls while developing, not
# in a shipped build. PUBLIC because an application decides whether to watch anything,
# and because it is what pulls the layout compiler into the binary.
target_compile_definitions(rendeer PUBLIC $<$<CONFIG:Debug>:RDA_ENABLE_HOT_RELOAD>)

# Hot reload recompiles a layout from inside the running application, so it has to find
# esbuild from wherever that application was started. Walking up from the working
# directory only ever worked for an application living inside this repository; the path
# the build itself used works for one anywhere. Debug only, because that is the only
# configuration that can compile a layout at all.
if(RDA_ESBUILD_EXECUTABLE)
	target_compile_definitions(rendeer PUBLIC
		$<$<CONFIG:Debug>:RDA_ESBUILD_DEFAULT="${RDA_ESBUILD_EXECUTABLE}">)
endif()

target_link_libraries(rendeer PUBLIC
	${RDA_VULKAN_LIBS}
	${RDA_GLFW_LIBS}
	$<$<CONFIG:Debug>:${RDA_SHADERC_DEBUG}>
	$<$<NOT:$<CONFIG:Debug>>:${RDA_SHADERC_RELEASE}>
)

if(WIN32)
	# NOMINMAX because windows.h defines min and max as macros, which breaks every
	# std::numeric_limits<T>::max() it is included ahead of -- vulkan.hpp included.
	target_compile_definitions(rendeer PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
	# UI Automation, which is how a screen reader reads a window. A system library on
	# every Windows there is; PUBLIC because this is a static library and the application
	# is what links.
	target_link_libraries(rendeer PUBLIC uiautomationcore imm32)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(rendeer PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(MSVC)
	target_compile_options(rendeer PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3>)
	# The prebuilt GLFW is built against the release CRT. In a debug build that clashes
	# with MSVCRTD unless the release one is ignored; GLFW manages its own allocations,
	# so mixing is safe here.
	target_link_options(rendeer INTERFACE $<$<CONFIG:Debug>:/NODEFAULTLIB:MSVCRT>)
	# 4099: the prebuilt shaderc and glslang libraries reference PDBs the SDK omits.
	target_link_options(rendeer INTERFACE /ignore:4099)
else()
	target_compile_options(rendeer PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wno-unused-parameter>)
endif()

# ---- the engine as a C ABI, for backends that are not C++ --------------------------
#
# A shared library, because that is what an FFI can load: ctypes, P/Invoke and every
# other binding mechanism opens a .dll or .so, and a static library is not one. It is
# the same engine -- the shared object links `rendeer` whole -- reached through a
# handful of C functions instead of C++ headers.
#
# Off for a C++ application, which has no use for it and should not pay to build it.
option(RDA_BUILD_C_API "Build the C ABI shared library, for non-C++ backends" ON)

if(RDA_BUILD_C_API)
	add_library(rendeer_c SHARED src/RendeerC.cpp)
	target_link_libraries(rendeer_c PRIVATE rendeer)

	# A shared library links even with symbols it cannot resolve: they are left for
	# dlopen to find, or for the first call into them to fail on. That is how a
	# libshaderc_combined.a that was not in fact combined got through this link and was
	# only reported by the first executable to link the engine. Refusing undefined
	# symbols here makes this library the place that failure is named, which is the one
	# place it is readable -- every backend that opens it would otherwise see it as
	# "cannot load rendeer_c".
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		target_link_options(rendeer_c PRIVATE -Wl,--no-undefined)
	endif()
	target_include_directories(rendeer_c PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
	# Exported on the way out, imported on the way in: the same header serves both, and
	# only the library itself defines this.
	target_compile_definitions(rendeer_c PRIVATE RDA_C_BUILDING)
	# Not named "rendeer": on Windows a shared library's import library would then be
	# rendeer.lib, which is what the static library is already called.
	set_target_properties(rendeer_c PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${RDA_OUTPUT_DIR}"
		LIBRARY_OUTPUT_DIRECTORY "${RDA_OUTPUT_DIR}"
		CXX_VISIBILITY_PRESET hidden)
endif()

# Assets resolve relative to the working directory, not the executable, so they are
# copied beside whatever is about to run. The font is not optional: without it the GUI
# cannot measure anything and start-up fails.
add_custom_command(TARGET rendeer POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/res" "${RDA_OUTPUT_DIR}/res"
	COMMENT "Copying engine res/ beside the binaries"
	VERBATIM
)
