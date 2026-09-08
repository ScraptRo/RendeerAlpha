# Finding what the engine links against.
#
# Three sources, tried in this order, first answer wins:
#
#   1. A package-manager install, via find_package. What Linux uses -- libvulkan-dev,
#      libglfw3-dev, libglm-dev, libshaderc-dev -- and what a Windows machine with vcpkg
#      uses.
#   2. The Vulkan SDK, via $VULKAN_SDK. On Windows that is the one install this engine
#      asks for, and it carries Vulkan, shaderc and glm as a matched set. That matters:
#      headers and libraries out of one installer cannot drift apart from each other.
#   3. GLFW only: built from source. It is the one thing the SDK does not ship, it has no
#      dependencies of its own, and it builds in seconds -- so fetching it is less to ask
#      of somebody than another install step, and it works with whatever toolchain is
#      building this rather than only the one a committed .lib was built with.
#
# There used to be a source above all of these: a vendored copy of the SDK under Dep/.
# It was 1.2 GB, of which 1.19 GB was MSVC import libraries -- one file of 425 MB, which
# is more than GitHub accepts in a single file at all. They were useless on any other
# platform or toolchain, and they mirrored something LunarG ships an installer for.
#
# Dep/ is now ignored rather than tracked. It is still *consulted* if it happens to be
# there, so a working tree that has one keeps building exactly as it did; it is simply no
# longer the thing a fresh clone depends on. Point RDA_VENDOR_ROOT somewhere else to use
# a mirror kept outside the tree.

set(RDA_VENDOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Dep/Vulkan/include" CACHE PATH
	"Optional local mirror of the Vulkan SDK. Untracked, and only used if it exists.")

# Pinned, because "whatever is on the branch today" is not a dependency, it is a lottery.
set(RDA_GLFW_VERSION "3.4" CACHE STRING "GLFW tag to build when none is installed")
set(RDA_VULKAN_VENDOR "${RDA_VENDOR_ROOT}/VULKAN")
set(RDA_GLFW_VENDOR "${RDA_VENDOR_ROOT}/GLFW")

# The installed SDK, if there is one. Everything below looks here before giving up, and
# says so in its status line, because "which copy did it pick" is the first question
# anybody asks when a build works on one machine and not another.
set(RDA_SDK_ROOT "")
if(DEFINED ENV{VULKAN_SDK})
	file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" RDA_SDK_ROOT)
endif()

# ---- Vulkan -------------------------------------------------------------------------
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
	message(STATUS "Vulkan: ${Vulkan_LIBRARY}")
	set(RDA_VULKAN_INCLUDE "${Vulkan_INCLUDE_DIRS}")
	set(RDA_VULKAN_LIBS "${Vulkan_LIBRARIES}")
elseif(WIN32 AND EXISTS "${RDA_VULKAN_VENDOR}/Lib/vulkan-1.lib")
	message(STATUS "Vulkan: local mirror (${RDA_VULKAN_VENDOR})")
	set(RDA_VULKAN_INCLUDE "${RDA_VULKAN_VENDOR}")
	set(RDA_VULKAN_LIBS "${RDA_VULKAN_VENDOR}/Lib/vulkan-1.lib")
else()
	message(FATAL_ERROR
		"No Vulkan found.\n"
		"  Windows: install the Vulkan SDK from https://vulkan.lunarg.com/sdk/home\n"
		"           (it also supplies shaderc and glm, below)\n"
		"  Linux:   install libvulkan-dev, or run scripts/linux-bringup.sh")
endif()

# ---- shaderc ------------------------------------------------------------------------
# Shaders are compiled at run time and cached, so this is not optional. The combined
# library bundles glslang and SPIRV-Tools, which is why only one name appears here.
#
# Debug and release are found separately because MSVC needs them to be: the two are
# compiled against different C runtimes, and linking the wrong one is a wall of duplicate
# symbols rather than anything that names the real problem. Everywhere else both searches
# land on the same file, which is correct.
#
# Which name comes first depends on the platform, and getting it wrong is silent. The
# SDK's shaderc_combined really is combined -- glslang and SPIRV-Tools inside it -- and
# on Windows it is the one to want. Debian and Ubuntu ship a libshaderc_combined.a of the
# same name that is not combined at all: glslang and SPIRV-Tools are separate packages,
# and linking it statically leaves every one of their symbols undefined. A shared library
# swallows that -- the holes are left for dlopen -- so the engine's own .so linked, and
# the first executable to link the engine failed with a page of `glslang::TShader`.
# Their libshaderc.so is the complete one, eight megabytes with everything inside, so
# on Linux the plain name goes first and the misleading one last.
if(WIN32)
	set(_rda_shaderc_release shaderc_combined shaderc_shared shaderc)
	set(_rda_shaderc_debug   shaderc_combinedd shaderc_sharedd shadercd shaderc_combined shaderc_shared shaderc)
else()
	set(_rda_shaderc_release shaderc shaderc_shared shaderc_combined)
	set(_rda_shaderc_debug   shaderc shaderc_shared shaderc_combined)
endif()
find_library(RDA_SHADERC_RELEASE
	NAMES ${_rda_shaderc_release}
	HINTS "${RDA_SDK_ROOT}/Lib" "${RDA_VULKAN_VENDOR}/Lib"
	DOC "shaderc, for release configurations")
find_library(RDA_SHADERC_DEBUG
	NAMES ${_rda_shaderc_debug}
	HINTS "${RDA_SDK_ROOT}/Lib" "${RDA_VULKAN_VENDOR}/Lib"
	DOC "shaderc, for debug configurations")
if(NOT RDA_SHADERC_RELEASE)
	message(FATAL_ERROR
		"No shaderc found.\n"
		"  Windows: install the Vulkan SDK, which ships it\n"
		"  Linux:   install libshaderc-dev, or run scripts/linux-bringup.sh")
endif()
if(NOT RDA_SHADERC_DEBUG)
	set(RDA_SHADERC_DEBUG "${RDA_SHADERC_RELEASE}")
endif()
message(STATUS "shaderc: ${RDA_SHADERC_RELEASE}")

# ---- glm ----------------------------------------------------------------------------
# Header only. The Vulkan SDK ships a copy under Include/, which is why a Windows machine
# needs nothing installed for it.
find_package(glm QUIET)
if(glm_FOUND)
	message(STATUS "glm: system")
	set(RDA_GLM_INCLUDE "")
elseif(EXISTS "${RDA_SDK_ROOT}/Include/glm")
	message(STATUS "glm: Vulkan SDK")
	set(RDA_GLM_INCLUDE "${RDA_SDK_ROOT}/Include")
elseif(EXISTS "${RDA_VULKAN_VENDOR}/glm")
	message(STATUS "glm: local mirror")
	set(RDA_GLM_INCLUDE "${RDA_VULKAN_VENDOR}")
else()
	message(FATAL_ERROR
		"No glm found.\n"
		"  Windows: install the Vulkan SDK, which ships it under Include/glm\n"
		"  Linux:   install libglm-dev, or run scripts/linux-bringup.sh")
endif()

# ---- GLFW ---------------------------------------------------------------------------
# Windowing and input. The one dependency worth revisiting later: SDL3 would bring proper
# IME and text input, which a custom interface layer needs before it can claim to handle
# text in every language it can now draw.
find_package(glfw3 QUIET)
if(glfw3_FOUND)
	message(STATUS "GLFW: system")
	set(RDA_GLFW_INCLUDE "")
	set(RDA_GLFW_LIBS glfw)
elseif(WIN32 AND EXISTS "${RDA_GLFW_VENDOR}/lib-vc2022/glfw3.lib")
	message(STATUS "GLFW: local mirror (${RDA_GLFW_VENDOR})")
	# One level above the GLFW directory, so sources use the standard <GLFW/glfw3.h>
	# spelling and a system install works unchanged.
	set(RDA_GLFW_INCLUDE "${RDA_VENDOR_ROOT}")
	# The static library only. The import library beside it caused duplicate-symbol
	# warnings when both were linked, and nothing here wants a GLFW DLL.
	set(RDA_GLFW_LIBS "${RDA_GLFW_VENDOR}/lib-vc2022/glfw3.lib")
else()
	# Built from source, pinned. This needs the network the first time a build tree is
	# configured and nothing after that -- it is cached in the build directory like any
	# other FetchContent dependency.
	message(STATUS "GLFW: building from source (${RDA_GLFW_VERSION})")
	include(FetchContent)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(glfw
		GIT_REPOSITORY https://github.com/glfw/glfw.git
		GIT_TAG "${RDA_GLFW_VERSION}"
		GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(glfw)
	set(RDA_GLFW_INCLUDE "")
	set(RDA_GLFW_LIBS glfw)
endif()
