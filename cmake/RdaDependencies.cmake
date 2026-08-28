# Finding what the engine links against.
#
# Two sources, and which one wins depends on the platform.
#
# On Windows the vendored copies under Dep/ win. That is how this repository has always
# built there, it needs no SDK installed, and an installed SDK of a different version is
# the classic way for a build to quietly stop matching the one people have been using.
#
# Everywhere else the system packages win, and the vendored copies are not consulted at
# all. They are Windows binaries -- import libraries, and headers configured for the Win32
# surface -- so picking them up on Linux does not fail at link time where it would be
# obvious. It fails while compiling vulkan.h, looking for windows.h.

set(RDA_VENDOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Dep/Vulkan/include")
set(RDA_VULKAN_VENDOR "${RDA_VENDOR_ROOT}/VULKAN")
set(RDA_GLFW_VENDOR "${RDA_VENDOR_ROOT}/GLFW")

# ---- Vulkan -----------------------------------------------------------------------
if(WIN32 AND EXISTS "${RDA_VULKAN_VENDOR}/Lib/vulkan-1.lib")
	message(STATUS "Vulkan: vendored (Dep/Vulkan)")
	set(RDA_VULKAN_INCLUDE "${RDA_VULKAN_VENDOR}")
	set(RDA_VULKAN_LIBS "${RDA_VULKAN_VENDOR}/Lib/vulkan-1.lib")
else()
	find_package(Vulkan QUIET)
endif()
if(NOT RDA_VULKAN_LIBS AND Vulkan_FOUND)
	message(STATUS "Vulkan: system (${Vulkan_LIBRARY})")
	set(RDA_VULKAN_INCLUDE "${Vulkan_INCLUDE_DIRS}")
	set(RDA_VULKAN_LIBS "${Vulkan_LIBRARIES}")
endif()
if(NOT RDA_VULKAN_LIBS)
	message(FATAL_ERROR
		"No Vulkan found. Install the Vulkan SDK (or your distribution's vulkan-headers "
		"and vulkan-loader packages), or restore the vendored copy under Dep/Vulkan.")
endif()

# ---- GLFW -------------------------------------------------------------------------
# Windowing and input. The one dependency worth revisiting later: SDL3 would bring
# proper IME and text input, which a custom interface layer needs before it can claim
# to handle text.
find_package(glfw3 QUIET)
if(glfw3_FOUND)
	message(STATUS "GLFW: system")
	set(RDA_GLFW_INCLUDE "")
	set(RDA_GLFW_LIBS glfw)
elseif(WIN32 AND EXISTS "${RDA_GLFW_VENDOR}/lib-vc2022/glfw3.lib")
	message(STATUS "GLFW: vendored (Dep/Vulkan/include/GLFW)")
	# One level above the GLFW directory, so sources can use the standard
	# <GLFW/glfw3.h> spelling and a system install works unchanged.
	set(RDA_GLFW_INCLUDE "${RDA_VENDOR_ROOT}")
	# The static library only. The import library beside it caused duplicate-symbol
	# warnings when both were linked, and nothing here wants a GLFW DLL.
	set(RDA_GLFW_LIBS "${RDA_GLFW_VENDOR}/lib-vc2022/glfw3.lib")
else()
	message(FATAL_ERROR "No GLFW found. Install libglfw3-dev, or restore Dep/Vulkan/include/GLFW.")
endif()

# ---- shaderc ----------------------------------------------------------------------
# Shaders are compiled at runtime and cached, so this is not optional. The combined
# library bundles glslang and SPIRV-Tools, which is why only one name appears here.
if(WIN32 AND EXISTS "${RDA_VULKAN_VENDOR}/Lib/shaderc_combined.lib")
	set(RDA_SHADERC_RELEASE "${RDA_VULKAN_VENDOR}/Lib/shaderc_combined.lib")
	set(RDA_SHADERC_DEBUG   "${RDA_VULKAN_VENDOR}/Lib/shaderc_combinedd.lib")
	if(NOT EXISTS "${RDA_SHADERC_DEBUG}")
		set(RDA_SHADERC_DEBUG "${RDA_SHADERC_RELEASE}")
	endif()
	message(STATUS "shaderc: vendored")
else()
	find_library(RDA_SHADERC_ANY NAMES shaderc_combined shaderc_shared shaderc)
	if(NOT RDA_SHADERC_ANY)
		message(FATAL_ERROR
			"No shaderc found. Install shaderc (libshaderc-dev), or restore the vendored "
			"copy under Dep/Vulkan/include/VULKAN/Lib.")
	endif()
	message(STATUS "shaderc: system (${RDA_SHADERC_ANY})")
	set(RDA_SHADERC_RELEASE "${RDA_SHADERC_ANY}")
	set(RDA_SHADERC_DEBUG   "${RDA_SHADERC_ANY}")
endif()

# ---- glm --------------------------------------------------------------------------
# Header only, and vendored alongside Vulkan. A system glm works just as well.
find_package(glm QUIET)
if(glm_FOUND)
	message(STATUS "glm: system")
	set(RDA_GLM_INCLUDE "")
elseif(WIN32 AND EXISTS "${RDA_VULKAN_VENDOR}/glm")
	message(STATUS "glm: vendored")
	set(RDA_GLM_INCLUDE "${RDA_VULKAN_VENDOR}")
else()
	message(FATAL_ERROR "No glm found. Install libglm-dev, or restore Dep/Vulkan/include/VULKAN/glm.")
endif()
