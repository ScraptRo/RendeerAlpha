# The engine library.
#
# One list, grouped by what the files do. Most of it is always compiled. The conditional
# parts are the ones that change what a binary does: the run-time JavaScript host
# (RDA_ENABLE_SCRIPTING, off by default), the C ABI library (RDA_BUILD_C_API), and hot
# reload, which is on in Debug and is what pulls the layout compiler into a binary.

set(RDA_SOURCES
	# The running engine, split by what each part is for: see src/Runtime/Engine.h.
	runtime/src/Runtime/Startup.cpp
	runtime/src/Runtime/EventPump.cpp
	runtime/src/Runtime/InputGather.cpp
	runtime/src/Runtime/SyntheticInput.cpp
	runtime/src/Runtime/WindowSignals.cpp
	runtime/src/Runtime/FrameLoop.cpp
	sync/src/Core/LoopWork.cpp
	sync/src/Core/Wake.cpp
	foundation/src/Core/FileStamp.cpp
	state/src/Core/Signals.cpp
	state/src/Core/Commands.cpp
	foundation/src/Core/Json.cpp
	state/src/Core/Tables.cpp
	foundation/src/Core/Png.cpp
	foundation/src/Core/Utf8.cpp
	foundation/src/Core/Colour.cpp
	ui/src/GraphicalObjects/MeshAsset.cpp
	engine/src/GraphicalObjects/Resources.cpp
	foundation/src/Core/Location.cpp
	platform/src/Core/FileDialog.cpp
	foundation/src/Core/Svg.cpp
	foundation/src/Core/Route.cpp
	renderer/src/GraphicalObjects/Images.cpp
	renderer/src/GraphicalObjects/Streams.cpp
	renderer/src/GraphicalObjects/Effects.cpp
	foundation/src/Logger/Logger.cpp

	vulkan/src/GraphicalSrc/DeviceHandler.cpp
	vulkan/src/GraphicalSrc/VulkanInstance.cpp
	vulkan/src/GraphicalSrc/Swapchain.cpp
	vulkan/src/GraphicalSrc/FrameSync.cpp
	vulkan/src/GraphicalSrc/WindownSurface.cpp
	renderer/src/GraphicalSrc/Renderer.cpp
	engine/src/GraphicalSrc/FrameBuffer.cpp
	engine/src/GraphicalSrc/PresentationSurface.cpp
	engine/src/GraphicalSrc/Shader.cpp
	engine/src/GraphicalSrc/ShaderCompiler.cpp
	engine/src/GraphicalSrc/GraphicsPipeline.cpp
	engine/src/GraphicalSrc/Descriptors.cpp
	engine/src/GraphicalSrc/MemoryBuffer.cpp

	runtime/src/GraphicalObjects/Window/Window.cpp
	runtime/src/GraphicalObjects/Window/WindowHosts.cpp
	platform/src/GraphicalObjects/WindowEdges.cpp
	platform/src/GraphicalObjects/Input.cpp
	engine/src/GraphicalObjects/Texture.cpp
	renderer/src/GraphicalObjects/Viewports.cpp
	sync/src/GraphicalObjects/ViewportEvents.cpp

	src/vendor/stb_image/stb_image.cpp
	src/vendor/stb_truetype/stb_truetype.cpp
	src/vendor/tinyxml2/tinyxml2.cpp
	src/vendor/vma/vk_mem_alloc.cpp
)

# The 3D scene path. The direction of travel is for this to leave the engine entirely
# and live in the application that wants a 3D view, which is what it was always for.
list(APPEND RDA_SOURCES
	renderer/src/GraphicalSrc/ShadowPass.cpp
	renderer/src/GraphicalSrc/ForwardPass.cpp
	renderer/src/GraphicalSrc/SceneFrame.cpp
	renderer/src/GraphicalSrc/MaterialResources.cpp
	engine/src/GraphicalObjects/Material.cpp
	renderer/src/GraphicalObjects/Scene.cpp
	engine/src/GraphicalObjects/Mesh.cpp
	renderer/src/GraphicalObjects/Primitives.cpp
)

# The interface layer: the reason the engine exists.
list(APPEND RDA_SOURCES
	renderer/src/GraphicalSrc/Font/FontDiscovery.cpp
	renderer/src/GraphicalSrc/Font/FontBake.cpp
	renderer/src/GraphicalSrc/Font/FontMetrics.cpp
	renderer/src/GraphicalSrc/GlyphAtlasTexture.cpp
	renderer/src/GraphicalSrc/GuiRenderer.cpp
	ui/src/GraphicalObjects/Gui/GuiFrame.cpp
	ui/src/GraphicalObjects/Gui/GuiFacade.cpp
	ui/src/GraphicalObjects/Gui/Canvas.cpp
	ui/src/GraphicalObjects/Gui/CanvasText.cpp
	ui/src/GraphicalObjects/Gui/GuiLabel.cpp
	ui/src/GraphicalObjects/Gui/Interaction.cpp
	ui/src/GraphicalObjects/Gui/Focus.cpp
	ui/src/GraphicalObjects/Gui/Shortcuts.cpp
	ui/src/GraphicalObjects/Gui/DragDrop.cpp
	ui/src/GraphicalObjects/Gui/GuiAccessibility.cpp
	ui/src/GraphicalObjects/Gui/A11yCollector.cpp
	ui/src/GraphicalObjects/Gui/WindowChrome.cpp
	ui/src/GraphicalObjects/Gui/GuiOverlay.cpp
	ui/src/GraphicalObjects/Gui/GuiScrollBar.cpp
	ui/src/GraphicalObjects/Gui/GuiPanel.cpp
	ui/src/GraphicalObjects/Gui/GuiButton.cpp
	ui/src/GraphicalObjects/Gui/GuiCheckbox.cpp
	ui/src/GraphicalObjects/Gui/GuiSlider.cpp
	ui/src/GraphicalObjects/Gui/GuiSplitter.cpp
	ui/src/GraphicalObjects/Gui/GuiTextField.cpp
	ui/src/GraphicalObjects/Motion.cpp
	ui/src/GraphicalObjects/TextEdit/TextEditLayout.cpp
	ui/src/GraphicalObjects/TextEdit/TextEditPointer.cpp
	ui/src/GraphicalObjects/TextEdit/TextEditInput.cpp
	ui/src/GraphicalObjects/TextEdit/TextEditRender.cpp
	ui/src/GraphicalObjects/TextEdit/TextEditAccess.cpp
	ui/src/GraphicalObjects/GuiTheme.cpp
	ui/src/GraphicalObjects/Shortcut.cpp
	platform/src/Platform/Accessibility.cpp
	platform/src/Platform/AccessibilityLinux.cpp
	platform/src/Platform/TextInput.cpp
	platform/src/Platform/WindowFrame.cpp
	platform/src/Platform/WindowSystem.cpp
	platform/src/Platform/OsWindow/OsWindow.cpp
	platform/src/Platform/OsWindow/OsWindowState.cpp
	platform/src/Platform/OsWindow/OsWindowFrameless.cpp
	platform/src/Platform/OsWindow/OsWindowIcon.cpp
	formats/src/GraphicalObjects/Accessible.cpp
	formats/src/GraphicalObjects/AccessibleDiff.cpp
	ui/src/GraphicalObjects/Widgets/WidgetBase.cpp
	ui/src/GraphicalObjects/Widgets/WidgetInternal.cpp
	ui/src/GraphicalObjects/Widgets/Containers/Stack.cpp
	ui/src/GraphicalObjects/Widgets/Containers/Scroll.cpp
	ui/src/GraphicalObjects/Widgets/Containers/Container.cpp
	ui/src/GraphicalObjects/Widgets/Containers/Panel.cpp
	ui/src/GraphicalObjects/Widgets/Containers/Popup.cpp
	ui/src/GraphicalObjects/Widgets/Containers/Tabs.cpp
	ui/src/GraphicalObjects/Widgets/Controls/Label.cpp
	ui/src/GraphicalObjects/Widgets/Controls/Button.cpp
	ui/src/GraphicalObjects/Widgets/Controls/Checkbox.cpp
	ui/src/GraphicalObjects/Widgets/Controls/Slider.cpp
	ui/src/GraphicalObjects/Widgets/Controls/NumberField.cpp
	ui/src/GraphicalObjects/Widgets/Controls/TextField.cpp
	ui/src/GraphicalObjects/Widgets/Controls/Select.cpp
	ui/src/GraphicalObjects/Widgets/Controls/ShortcutKey.cpp
	ui/src/GraphicalObjects/Widgets/Controls/Splitter.cpp
	ui/src/GraphicalObjects/Widgets/Media/Image.cpp
	ui/src/GraphicalObjects/Widgets/Media/Stream.cpp
	ui/src/GraphicalObjects/Widgets/Media/Viewport.cpp
	ui/src/GraphicalObjects/Syntax.cpp
	ui/src/GraphicalObjects/DockWidgets.cpp
	ui/src/GraphicalObjects/DockSpace.cpp
	ui/src/GraphicalObjects/DockPanes.cpp
	ui/src/GraphicalObjects/DockTiles.cpp
	ui/src/GraphicalObjects/DockChrome.cpp
	ui/src/GraphicalObjects/DockTree.cpp
	ui/src/GraphicalObjects/TileGrid.cpp

	formats/src/Layout/Blueprint.cpp
	formats/src/Layout/WidgetSchema.cpp
	formats/src/Layout/ThemeSchema.cpp
	blueprint/src/Layout/Instantiator.cpp
	runtime/src/Layout/LayoutLoader.cpp
	# One record per element: how each is built, configured, bound and listened to.
	blueprint/src/Layout/WidgetRegistry.cpp
	blueprint/src/Layout/Kinds/KindSupport.cpp
	blueprint/src/Layout/Kinds/ContainerKinds.cpp
	blueprint/src/Layout/Kinds/ControlKinds.cpp
	blueprint/src/Layout/Kinds/MediaKinds.cpp
	blueprint/src/Layout/Kinds/DataKinds.cpp
	blueprint/src/Layout/Kinds/DockKinds.cpp
	ui/src/Layout/ListView.cpp
	ui/src/Layout/TreeView.cpp
	ui/src/Layout/GridView.cpp
	state/src/Layout/TableOrder.cpp
	ui/src/Layout/Selection.cpp
	runtime/src/Layout/LayoutHost.cpp
	formats/src/Layout/ImportScan.cpp
	# What hot reload compiles through: a slot, empty unless the toolchain filled it.
	formats/src/Layout/Rebuilder.cpp
	# The theme and its meshes, reloaded when they change.
	runtime/src/Layout/StyleWatch.cpp
	blueprint/src/Layout/ThemeLoader.cpp
	runtime/src/Layout/Router.cpp
	runtime/src/Layout/Probe.cpp
	formats/src/Layout/Expression.cpp
	runtime/src/Layout/Bindings.cpp
	# What a layout's functions run on: QuickJS, created the first time a blueprint that
	# carries bytecode is loaded, and never otherwise. See docs/architecture/runtime-code.md.
	runtime/src/Layout/Script/ScriptRuntime.cpp
	runtime/src/Layout/Script/ScriptValues.cpp
	runtime/src/Layout/Script/ScriptFrame.cpp
	runtime/src/Layout/Script/ScriptNatives.cpp
	runtime/src/Layout/Script/ScriptTimers.cpp
	runtime/src/Layout/Script/ScriptModules.cpp
	runtime/src/Layout/Script/ScriptState.cpp
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
	runtime/src/Scripting/Script.cpp
	runtime/src/Scripting/ScriptBindings.cpp
	runtime/src/Scripting/ScriptQueue.cpp
)

# The authoring layer. Compiled in, but linked into an application only if that
# application calls it -- which is why a program that merely loads blueprints does not
# carry the compiler or the JavaScript engine behind it.
list(APPEND RDA_SOURCES
	toolchain/src/Layout/LayoutCompiler.cpp
	toolchain/src/Layout/ModuleTransform.cpp
	# The one JavaScript engine they all evaluate in, and the one way they say what it threw.
	toolchain/src/Layout/JsContext.cpp
	toolchain/src/Layout/ThemeCompiler.cpp
	toolchain/src/Layout/MeshCompiler.cpp
	toolchain/src/Layout/TypeEmitter.cpp
	# state.ts read into a StateSchema, and the generators that print it as C++, Python,
	# JavaScript and C#. The reader needs esbuild and QuickJS; the generators and the
	# writer they share need nothing, which is how the tests can cover them.
	toolchain/src/Layout/StateReader.cpp
	toolchain/src/Layout/Codegen/CodeWriter.cpp
	toolchain/src/Layout/Codegen/CppStateGen.cpp
	toolchain/src/Layout/Codegen/PythonStateGen.cpp
	toolchain/src/Layout/Codegen/NodeStateGen.cpp
	toolchain/src/Layout/Codegen/CSharpStateGen.cpp
	toolchain/src/Layout/ExpressionParser.cpp
	# Which of a layout's functions run at run time, and the bytecode they ship as.
	toolchain/src/Layout/ScriptCompiler.cpp
	# esbuild's source map, read back, so a file that throws while it is built is
	# reported at the line it was written on rather than the line esbuild printed.
	toolchain/src/Layout/SourceMap.cpp
	# Placeholder state for a declaration with no backend behind it yet. Here rather than
	# with the loader, because it reads JSON with the same engine the compiler uses.
	toolchain/src/Layout/Prototype.cpp
	# Hot reload's compilers, behind Layout/Rebuilder.h. Only a hot-reload build's
	# bring-up references this, so only that build links what it calls.
	toolchain/src/Layout/RebuildTools.cpp
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

# ---- the layers -----------------------------------------------------------------------
#
# The engine is built as one object library per layer, bottom to top -- see
# docs/architecture/layers.md. Each is compiled against its own headers and the public
# headers of the layers below it, and nothing else, so an include that points upward does not
# compile: the layering is a property of the build rather than of review. Every source above
# is in exactly one layer, by the directory it lives in.
#
# What they all share: the language, the third-party headers and the switches decided per
# configuration. An interface target, so it travels with every layer and on to whatever links
# the engine.
add_library(rda_common INTERFACE)

# PUBLIC, so an application that links this is compiled as C++20 without having to know that
# it must. CMAKE_CXX_STANDARD covers this repository's own targets and nothing else, which held
# only while every application lived inside it.
target_compile_features(rda_common INTERFACE cxx_std_20)

target_include_directories(rda_common INTERFACE
	# src/vendor, as <vendor/...>: the container library some public headers use, and the
	# third-party code the engine compiles.
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	${RDA_VULKAN_INCLUDE}
	${RDA_GLM_INCLUDE}
)

# One layer: an object library over every source under <name>/, its own headers public and its
# src/ private, and the layers it may use linked -- which is what puts their public headers on
# its include path, and theirs alone.
function(rda_layer name)
	cmake_parse_arguments(PARSE_ARGV 1 RDA_LAYER "" "" "BELOW")
	set(sources ${RDA_SOURCES})
	list(FILTER sources INCLUDE REGEX "^${name}/")
	add_library(rda_${name} OBJECT ${sources})
	target_include_directories(rda_${name}
		PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${name}/include"
		PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${name}/src")
	target_link_libraries(rda_${name} PUBLIC rda_common)
	foreach(below IN LISTS RDA_LAYER_BELOW)
		target_link_libraries(rda_${name} PUBLIC rda_${below})
	endforeach()
	if(MSVC)
		target_compile_options(rda_${name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3>)
	else()
		target_compile_options(rda_${name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wno-unused-parameter>)
	endif()
endfunction()

# Bottom to top. The Vulkan layer and the platform layer sit side by side: neither may reach
# the other, and the engine above is what puts a window and a device together.
rda_layer(foundation)
rda_layer(sync       BELOW foundation)
rda_layer(state      BELOW sync)
rda_layer(formats    BELOW state)
rda_layer(vulkan     BELOW formats)
rda_layer(platform   BELOW formats)
rda_layer(engine     BELOW vulkan platform)
rda_layer(renderer   BELOW engine)
rda_layer(ui         BELOW renderer)
rda_layer(blueprint  BELOW ui)
rda_layer(runtime    BELOW blueprint)
rda_layer(toolchain  BELOW runtime)
set(RDA_LAYERS foundation sync state formats vulkan platform engine renderer ui blueprint
	runtime toolchain)

# The vendored libraries compiled with the engine -- VMA, stb, tinyxml2 -- beneath every layer,
# which reach them through their own headers. QuickJS has a target of its own, below.
set(_rda_vendor ${RDA_SOURCES})
list(FILTER _rda_vendor INCLUDE REGEX "^src/vendor/")
add_library(rda_vendor OBJECT ${_rda_vendor})
target_link_libraries(rda_vendor PUBLIC rda_common)

# Every layer's headers, private ones included, for the test runner -- which compiles chosen
# sources directly and reaches into some of them.
set(RDA_ENGINE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")
foreach(layer IN LISTS RDA_LAYERS)
	list(APPEND RDA_ENGINE_INCLUDE_DIRS
		"${CMAKE_CURRENT_SOURCE_DIR}/${layer}/include" "${CMAKE_CURRENT_SOURCE_DIR}/${layer}/src")
endforeach()
set(RDA_ENGINE_INCLUDE_DIRS "${RDA_ENGINE_INCLUDE_DIRS}" PARENT_SCOPE)

# The engine is still one static library: every layer's objects linked into it, so an
# application links exactly what it linked before and reaches every public header it did.
add_library(rendeer STATIC)
add_library(rendeer::rendeer ALIAS rendeer)
foreach(layer IN LISTS RDA_LAYERS)
	target_link_libraries(rendeer PUBLIC rda_${layer})
endforeach()
target_link_libraries(rendeer PRIVATE rda_vendor rendeer_quickjs)

# JavaScript the toolchain carries as bytes: acorn, which parses a bundled layout to find
# which functions a binding reaches, the analysis that runs over what it parsed, and the
# preludes -- the helpers a theme, a mesh, a layout and state.ts are written with, the
# `format` and timers that a layout's code may name, and the module that code ships in. Each
# is a real .js file, so an editor can read it and a linter can check it, and is turned into
# a header at configure time:
#
#   rda_embed_js(<name> <file>)
#
# writes ${RDA_GENERATED_DIR}/<name>.h, which defines
#
#   inline constexpr unsigned char k<name>[]      the bytes of the file, then a NUL
#   inline constexpr std::size_t   k<name>Size    how many bytes the file is, without it
#
# so the text of the file can be handed to QuickJS as it stands. An array rather than a
# string literal because MSVC refuses a literal past 64 KB, and inline so that one
# definition serves every file that includes the header. Regenerated whenever the .js is
# newer than the header.
set(RDA_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RDA_GENERATED_DIR "${RDA_GENERATED_DIR}" PARENT_SCOPE)

function(rda_embed_js name file)
	set(header "${RDA_GENERATED_DIR}/${name}.h")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
	if(NOT EXISTS "${header}" OR "${file}" IS_NEWER_THAN "${header}")
		file(RELATIVE_PATH shown "${CMAKE_CURRENT_SOURCE_DIR}" "${file}")
		file(READ "${file}" hex HEX)
		string(LENGTH "${hex}" hex_length)
		math(EXPR bytes "${hex_length} / 2")
		string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
		# Thirty-two to a line, so no compiler meets a line a megabyte long.
		string(REPEAT "0x[0-9a-f][0-9a-f]," 32 row)
		string(REGEX REPLACE "(${row})" "\\1\n" hex "${hex}")
		file(WRITE "${header}"
			"// Generated from ${shown} by rda_embed_js in RendeerAlpha/CMakeLists.txt. Do not edit.\n"
			"#pragma once\n#include <cstddef>\n"
			"inline constexpr unsigned char k${name}[] = {\n${hex}0x00 };\n"
			"inline constexpr std::size_t k${name}Size = ${bytes};\n")
	endif()
endfunction()

# A prelude's file begins with a blank line, on purpose. The string literal each one used to
# be began with the newline after its opening quote, and a stack frame inside a prelude
# (`<mesh>:12:3`) is counted from there -- so keeping it keeps every such line the same.
set(_rda_js "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/src/Layout/js")
rda_embed_js(AcornSource             "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/acorn/acorn.js")
rda_embed_js(ColourPreludeSource     "${_rda_js}/colour.js")
rda_embed_js(LayoutPreludeSource     "${_rda_js}/layout.js")
rda_embed_js(StatePreludeSource      "${_rda_js}/state.js")
rda_embed_js(MeshPreludeSource       "${_rda_js}/mesh.js")
rda_embed_js(FormatPreludeSource     "${_rda_js}/format.js")
rda_embed_js(TimerPreludeSource      "${_rda_js}/timers.js")
rda_embed_js(BuildTimerPreludeSource "${_rda_js}/build-timers.js")
rda_embed_js(RuntimePreludeSource    "${_rda_js}/runtime-prelude.js")
rda_embed_js(AnalyzerSource          "${_rda_js}/analyzer.js")
target_include_directories(rda_toolchain PRIVATE "${RDA_GENERATED_DIR}")

# _DEBUG is something MSVC defines by itself in a debug configuration; no other compiler
# does. The logger and the Vulkan validation layers are both gated on it, so without this
# a Debug build elsewhere would quietly have neither -- and the first thing anyone does
# on a new platform is look for the log.
target_compile_definitions(rda_common INTERFACE
	$<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:_DEBUG>)

# Hot reload watches files for changes -- worth the stat() calls while developing, not
# in a shipped build. PUBLIC because an application decides whether to watch anything,
# and because it is what pulls the layout compiler into the binary.
target_compile_definitions(rda_common INTERFACE $<$<CONFIG:Debug>:RDA_ENABLE_HOT_RELOAD>)

# Hot reload recompiles a layout from inside the running application, so it has to find
# esbuild from wherever that application was started. Walking up from the working
# directory only ever worked for an application living inside this repository; the path
# the build itself used works for one anywhere. Debug only, because that is the only
# configuration that can compile a layout at all.
if(RDA_ESBUILD_EXECUTABLE)
	target_compile_definitions(rda_common INTERFACE
		$<$<CONFIG:Debug>:RDA_ESBUILD_DEFAULT="${RDA_ESBUILD_EXECUTABLE}">)
endif()

# On the shared target rather than on `rendeer`, because a target's headers come with
# linking it and every layer compiles against them. `rendeer` gets them anyway, through its
# layers.
target_link_libraries(rda_common INTERFACE
	${RDA_VULKAN_LIBS}
	$<$<CONFIG:Debug>:${RDA_SHADERC_DEBUG}>
	$<$<NOT:$<CONFIG:Debug>>:${RDA_SHADERC_RELEASE}>
)

# The window system is the platform layer's business, and the Vulkan layer's for the one
# thing it needs it for -- making a surface over a window. Privately, so no layer above them
# can include GLFW: the runtime asks Platform/OsWindow.h and Platform/WindowSystem.h
# instead. The library itself is linked where the engine is, because something has to.
if(RDA_GLFW_INCLUDE)
	target_include_directories(rda_platform PRIVATE ${RDA_GLFW_INCLUDE})
	target_include_directories(rda_vulkan PRIVATE ${RDA_GLFW_INCLUDE})
endif()
target_link_libraries(rda_platform PRIVATE ${RDA_GLFW_LIBS})
target_link_libraries(rda_vulkan PRIVATE ${RDA_GLFW_LIBS})
target_link_libraries(rendeer PUBLIC ${RDA_GLFW_LIBS})

if(WIN32)
	# NOMINMAX because windows.h defines min and max as macros, which breaks every
	# std::numeric_limits<T>::max() it is included ahead of -- vulkan.hpp included.
	target_compile_definitions(rda_common INTERFACE NOMINMAX _CRT_SECURE_NO_WARNINGS)
	# UI Automation, which is how a screen reader reads a window. A system library on
	# every Windows there is; PUBLIC because this is a static library and the application
	# is what links.
	target_link_libraries(rda_common INTERFACE uiautomationcore imm32)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(rda_common INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(MSVC)
	# The prebuilt GLFW is built against the release CRT. In a debug build that clashes
	# with MSVCRTD unless the release one is ignored; GLFW manages its own allocations,
	# so mixing is safe here.
	target_link_options(rendeer INTERFACE $<$<CONFIG:Debug>:/NODEFAULTLIB:MSVCRT>)
	# 4099: the prebuilt shaderc and glslang libraries reference PDBs the SDK omits.
	target_link_options(rendeer INTERFACE /ignore:4099)
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
	add_library(rendeer_c SHARED api/src/RendeerC.cpp)
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
	target_include_directories(rendeer_c PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/api/include")
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
