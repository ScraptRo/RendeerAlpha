# The bindings, tested against a running engine.
#
# This is the one test tier that needs a device, a window and a second language runtime,
# which is exactly why it did not exist for a while. It is worth having anyway: the C ABI
# is a product with three consumers, and nothing else in the build compiles any of them.
#
# All three run the same checklist in the same order, so a line that appears in two
# outputs and not the third is a gap rather than a difference of opinion. Each opens a
# window, does its work, and stops itself -- no timeout, no killing.
#
# Skipped rather than failed when a runtime is not installed: a machine without Node or
# .NET should still be able to run everything else.

find_package(Python3 COMPONENTS Interpreter QUIET)
# Bare names only. CMake appends .exe on Windows by itself, and naming it here has one
# effect elsewhere: WSL puts the Windows PATH after its own, so `dotnet.exe` finds the
# Windows SDK from inside Linux, which then runs MSBuild against Linux paths and fails
# on the first one. A Linux box with no dotnet must find none, and skip.
find_program(RDA_NODE_EXECUTABLE NAMES node)
find_program(RDA_DOTNET_EXECUTABLE NAMES dotnet)

set(fixture "${CMAKE_BINARY_DIR}/generated/bindingtests")
set(rundir "${RDA_OUTPUT_DIR}/bindingtests")
set(here "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- the generated halves of one declaration ----------------------------------------
add_custom_command(
	OUTPUT  "${fixture}/state.py"
	COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> python "${here}/state.ts" "${fixture}/state.py"
	DEPENDS "${here}/state.ts" rda
	COMMENT "Generating state.py for the binding tests"
	VERBATIM
)
add_custom_command(
	OUTPUT  "${fixture}/state.mjs"
	COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> node "${here}/state.ts" "${fixture}/state.mjs"
	DEPENDS "${here}/state.ts" rda
	COMMENT "Generating state.mjs for the binding tests"
	VERBATIM
)
add_custom_command(
	OUTPUT  "${fixture}/state.cs"
	COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> csharp "${here}/state.ts" "${fixture}/state.cs"
	DEPENDS "${here}/state.ts" rda
	COMMENT "Generating state.cs for the binding tests"
	VERBATIM
)

# ---- the layouts they load ------------------------------------------------------------
# Into the build tree rather than beside their source: these are a fixture, not something
# an application ships, so there is nothing to commit and nothing to keep in step by hand.
set(blueprints "")
foreach(layout first second)
	add_custom_command(
		OUTPUT  "${fixture}/layouts/${layout}.rdab"
		COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> layout
			"${here}/layouts/${layout}.tsx" "${fixture}/layouts/${layout}.rdab"
		DEPENDS "${here}/layouts/${layout}.tsx" rda
		COMMENT "Compiling binding-test layout ${layout}.tsx"
		VERBATIM
	)
	list(APPEND blueprints "${fixture}/layouts/${layout}.rdab")
endforeach()

# ---- one directory holding everything a run needs -------------------------------------
add_custom_target(rda_binding_fixture ALL
	DEPENDS "${fixture}/state.py" "${fixture}/state.mjs" "${fixture}/state.cs" ${blueprints}
	COMMAND ${CMAKE_COMMAND} -E make_directory "${rundir}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${fixture}/state.py" "${fixture}/state.mjs"
		"${here}/check.py" "${here}/check.mjs" "${rundir}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${fixture}/layouts" "${rundir}/res/layouts"
	# The font, because start-up fails without one and it resolves from the working
	# directory. The rest of the engine's res/ comes with it and costs nothing.
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/RendeerAlpha/res" "${rundir}/res"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:rendeer_c>" "${rundir}/$<TARGET_FILE_NAME:rendeer_c>"
	# Both packages, laid out the way each language looks for one.
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${here}/../python/rda" "${rundir}/rda"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${rundir}/node_modules/rda"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${here}/../node/package.json" "${here}/../node/index.js"
		"${here}/../node/engine.js" "${rundir}/node_modules/rda"
	COMMENT "Assembling the binding-test fixture"
	VERBATIM
)
add_dependencies(rda_binding_fixture rendeer_c)

# The layouts are copied before the engine's res/ overwrites the directory, so put them
# back afterwards. Two copies rather than one ordering trick, because the ordering trick
# is the kind of thing that works until somebody reorders the lines.
add_custom_command(TARGET rda_binding_fixture POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${fixture}/layouts" "${rundir}/res/layouts"
	VERBATIM)

# ---- the C# check, which unlike the other two has to be compiled ----------------------
#
# Built straight into the run directory so the assembly sits beside rendeer_c.dll, which
# is where .NET looks first. The binding's own sources are compiled in rather than
# referenced, so this exercises what is in the repository and not a stale artefact.
if(RDA_DOTNET_EXECUTABLE)
	add_custom_target(rda_csharp_check ALL
		DEPENDS "${fixture}/state.cs"
		COMMAND "${RDA_DOTNET_EXECUTABLE}" build "${here}/Check.csproj"
			-c Release -o "${rundir}" -v quiet --nologo
			"-p:RdaGeneratedState=${fixture}/state.cs"
		COMMENT "Building the C# binding check"
		VERBATIM
	)
	add_dependencies(rda_csharp_check rda_binding_fixture)
endif()

# ---- the tests ------------------------------------------------------------------------
if(Python3_Interpreter_FOUND)
	add_test(NAME rda_python_binding
	         COMMAND "${Python3_EXECUTABLE}" check.py
	         WORKING_DIRECTORY "${rundir}")
	set_tests_properties(rda_python_binding PROPERTIES TIMEOUT 180)
else()
	message(STATUS "Binding tests: no Python interpreter found, skipping rda_python_binding")
endif()

# koffi is Node's FFI and is not vendored -- Node has none of its own. It is resolved by
# walking up from the run directory, which reaches this checkout's own node_modules, so
# `npm install` here is what turns this test on. Checked rather than assumed: a clone that
# has not been npm-installed should skip this the way it skips recompiling layouts, not
# fail with a module error.
set(koffi "${CMAKE_SOURCE_DIR}/node_modules/koffi")

if(RDA_NODE_EXECUTABLE AND EXISTS "${koffi}")
	add_test(NAME rda_node_binding
	         COMMAND "${RDA_NODE_EXECUTABLE}" check.mjs
	         WORKING_DIRECTORY "${rundir}")
	set_tests_properties(rda_node_binding PROPERTIES TIMEOUT 180)
elseif(NOT RDA_NODE_EXECUTABLE)
	message(STATUS "Binding tests: no node found, skipping rda_node_binding")
else()
	message(STATUS
		"Binding tests: koffi is not installed, skipping rda_node_binding "
		"(run `npm install` in ${CMAKE_SOURCE_DIR} to enable it)")
endif()

if(RDA_DOTNET_EXECUTABLE)
	add_test(NAME rda_csharp_binding
	         COMMAND "${rundir}/rda_csharp_check${CMAKE_EXECUTABLE_SUFFIX}"
	         WORKING_DIRECTORY "${rundir}")
	set_tests_properties(rda_csharp_binding PROPERTIES TIMEOUT 180)
else()
	message(STATUS "Binding tests: no dotnet found, skipping rda_csharp_binding")
endif()
