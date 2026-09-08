# Finding esbuild, once, at configure time.
#
# The layout and theme compilers shell out to esbuild. It used to be located at run time
# by walking up from the working directory looking for node_modules, which worked only
# because every application lived inside this repository. An application outside it has
# no node_modules anywhere above its build directory, so the search found nothing and
# every layout rule failed.
#
# So the question is answered here instead, where both the application and the engine
# checkout are known, and the answer is handed to the tool. Configure-time also means a
# missing esbuild is a status line rather than a build failure four minutes in.

set(RDA_ESBUILD "" CACHE FILEPATH "esbuild executable used to compile layouts and themes")

if(WIN32)
	set(_rda_esbuild_name "esbuild.cmd")
else()
	set(_rda_esbuild_name "esbuild")
endif()

set(RDA_ESBUILD_EXECUTABLE "")

if(RDA_ESBUILD)
	set(RDA_ESBUILD_EXECUTABLE "${RDA_ESBUILD}")
elseif(DEFINED ENV{RDA_ESBUILD} AND EXISTS "$ENV{RDA_ESBUILD}")
	set(RDA_ESBUILD_EXECUTABLE "$ENV{RDA_ESBUILD}")
else()
	# The application's own install first: a project that ran `npm install` meant the
	# copy it chose. Then this checkout's, so an application built against the engine
	# needs no node install of its own. node_modules holds binaries for one platform,
	# which is why an override exists at all.
	foreach(place IN ITEMS
		"${CMAKE_SOURCE_DIR}/node_modules/.bin/${_rda_esbuild_name}"
		"${CMAKE_CURRENT_LIST_DIR}/../node_modules/.bin/${_rda_esbuild_name}")
		if(EXISTS "${place}")
			get_filename_component(RDA_ESBUILD_EXECUTABLE "${place}" ABSOLUTE)
			break()
		endif()
	endforeach()

	if(NOT RDA_ESBUILD_EXECUTABLE)
		find_program(RDA_ESBUILD_ON_PATH NAMES esbuild)
		if(RDA_ESBUILD_ON_PATH)
			set(RDA_ESBUILD_EXECUTABLE "${RDA_ESBUILD_ON_PATH}")
		endif()
	endif()
endif()

# Found is not the same as works. A node_modules copied from another machine has the
# file and not the execute bit; or has the bit and not the platform binary the shim
# launches; or has both and no node to launch it with. Each of those passed the EXISTS
# test above, was announced as found, and then failed the build with "Permission denied"
# an hour of compiling later -- the precise failure the comment at the top of this file
# says configure-time detection exists to prevent. So the candidate is run, once, here.
set(_rda_esbuild_version "")
if(RDA_ESBUILD_EXECUTABLE)
	if(WIN32)
		# A .cmd is not a program; only cmd.exe can start it.
		set(_rda_esbuild_probe cmd /c "${RDA_ESBUILD_EXECUTABLE}" --version)
	else()
		set(_rda_esbuild_probe "${RDA_ESBUILD_EXECUTABLE}" --version)
	endif()
	execute_process(COMMAND ${_rda_esbuild_probe}
		RESULT_VARIABLE _rda_esbuild_rc
		OUTPUT_VARIABLE _rda_esbuild_version
		ERROR_VARIABLE  _rda_esbuild_error
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_STRIP_TRAILING_WHITESPACE
		TIMEOUT 30)
	if(NOT _rda_esbuild_rc EQUAL 0)
		# The result is a number when the program ran and failed, and a sentence when it
		# could not be started at all; either is the reason worth printing.
		string(REPLACE "\n" " " _rda_esbuild_error "${_rda_esbuild_error}")
		message(STATUS
			"esbuild: ${RDA_ESBUILD_EXECUTABLE} exists but does not run "
			"(${_rda_esbuild_rc} ${_rda_esbuild_error}) -- a node_modules copied from "
			"another platform, or no node installed. Treated as not found.")
		set(RDA_ESBUILD_EXECUTABLE "")
	endif()
endif()

if(RDA_ESBUILD_EXECUTABLE)
	message(STATUS "esbuild: ${RDA_ESBUILD_EXECUTABLE} (${_rda_esbuild_version})")
	# Through the environment rather than a new argument: it is what the compiler already
	# reads, so one mechanism covers the build rules and a hand-run `rda` alike.
	set(RDA_TOOL_ENV ${CMAKE_COMMAND} -E env "RDA_ESBUILD=${RDA_ESBUILD_EXECUTABLE}")
	# Note for anyone adding a rule that uses this: once a command starts with something
	# else, CMake stops substituting the target name `rda` for its executable, because it
	# only does that for the first argument. Every wrapped rule therefore spells the tool
	# as $<TARGET_FILE:rda>, and one that does not will invoke a program called "rda"
	# from PATH and fail with no useful message.
else()
	message(STATUS
		"esbuild: not found -- layouts and themes will not be recompiled, and the "
		"blueprints already in the source tree are used as they are")
	set(RDA_TOOL_ENV "")
endif()

# Cached rather than left as a plain variable, because an application includes the engine
# as a subdirectory and then calls the rda_add_* rules from its own scope -- which a
# variable set down here would never reach. This was silent: the rules simply reported no
# esbuild and used the blueprints already in the tree.
set(RDA_ESBUILD_EXECUTABLE "${RDA_ESBUILD_EXECUTABLE}" CACHE INTERNAL "esbuild used to compile layouts")
set(RDA_TOOL_ENV "${RDA_TOOL_ENV}" CACHE INTERNAL "environment prefix for rda invocations")
