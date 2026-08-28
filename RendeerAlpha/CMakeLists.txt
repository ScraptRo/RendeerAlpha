# The engine library.
#
# One list, grouped by what the files do. Everything here is always compiled; the only
# conditional part is out-of-process hosting, which changes how an application is
# deployed rather than what it can do.

set(RDA_SOURCES
	src/RendeerAlpha.cpp
	src/Core/LoopWork.cpp
	src/Core/Signals.cpp
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
	src/GraphicalObjects/Input.cpp
	src/GraphicalObjects/Texture.cpp

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
	src/GraphicalObjects/GuiWidgets.cpp
	src/GraphicalObjects/GuiTheme.cpp
	src/GraphicalObjects/GuiComponents.cpp
	src/GraphicalObjects/Widget.cpp
	src/GraphicalObjects/Syntax.cpp
	src/GraphicalObjects/Docking.cpp
	src/GraphicalObjects/DockTree.cpp

	src/Layout/Blueprint.cpp
	src/Layout/WidgetSchema.cpp
	src/Layout/LayoutLoader.cpp
	src/Layout/LayoutHost.cpp
	src/Layout/Expression.cpp
	src/Layout/Bindings.cpp
)

# ---- out-of-process hosting, off by default ---------------------------------------
# The wire protocol, the host that serves it and the client that speaks it. Once the
# only way to run an application; now reserved for the case that actually needs process
# isolation, which is a preview outliving the crash of what it previews.
#
# DrawApi belongs here rather than with the rest of the scripting layer: it is the
# drawing interface a *client* uses, and it has no meaning in a process that owns its
# own device.
set(RDA_SOURCES_REMOTE
	src/Runtime/Transport.cpp
	src/Runtime/Host.cpp
	src/Runtime/Client.cpp
	src/Runtime/Compositor.cpp
)
set(RDA_SOURCES_REMOTE_STUDIO
	src/Scripting/DrawApi.cpp
)

# The authoring layer. Compiled in, but linked into an application only if that
# application calls it -- which is why a program that merely loads blueprints does not
# carry the compiler or the JavaScript engine behind it.
list(APPEND RDA_SOURCES
	src/Layout/LayoutCompiler.cpp
	src/Layout/ModuleTransform.cpp
	src/Layout/ThemeCompiler.cpp
	src/Layout/TypeEmitter.cpp
	src/Layout/StateSchema.cpp
	src/Layout/ExpressionParser.cpp
	src/Scripting/Script.cpp
	src/Scripting/ScriptBindings.cpp
	src/Scripting/ScriptQueue.cpp
)

set(RDA_SOURCES_QUICKJS
	src/vendor/quickjs/quickjs.c
	src/vendor/quickjs/libregexp.c
	src/vendor/quickjs/libunicode.c
	src/vendor/quickjs/dtoa.c
)

if(RDA_ENABLE_REMOTE)
	list(APPEND RDA_SOURCES ${RDA_SOURCES_REMOTE} ${RDA_SOURCES_REMOTE_STUDIO})
	message(STATUS "Out-of-process hosting: enabled")
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

if(RDA_ENABLE_REMOTE)
	target_compile_definitions(rendeer PUBLIC RDA_REMOTE)
endif()

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

# Assets resolve relative to the working directory, not the executable, so they are
# copied beside whatever is about to run. The font is not optional: without it the GUI
# cannot measure anything and start-up fails.
add_custom_command(TARGET rendeer POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/res" "${RDA_OUTPUT_DIR}/res"
	COMMENT "Copying engine res/ beside the binaries"
	VERBATIM
)
