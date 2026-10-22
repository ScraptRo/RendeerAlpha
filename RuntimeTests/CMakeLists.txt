# The test runner. Not a product, which is why it survives the trimming: it is the only
# executable proof that the signal graph, the expression VM and the animation layer
# behave as documented.
#
# It compiles the sources it tests directly rather than linking the library, so what is
# tested stays testable in a build that does not include it -- and so a source only has
# to be device-independent to be covered, not part of any particular configuration.
set(RDA_TEST_SOURCES
	Main.cpp
	SignalTests.cpp
	ExpressionTests.cpp
	FeatureTests.cpp
	LayoutTests.cpp
	TextTests.cpp
	TileTests.cpp
	MeshTests.cpp
	PngTests.cpp
	ShortcutTests.cpp
	AccessibleTextTests.cpp
	TableOrderTests.cpp
	SelectionTests.cpp
	ScriptHostTests.cpp
	StateCodeTests.cpp
	TimerFormatTests.cpp
	ViewportEventTests.cpp
	WindowEdgeTests.cpp
	../RendeerAlpha/src/Layout/Expression.cpp
	# Writing a picture, and reading it back with the decoder that is vendored for reading
	# other people's. The round trip is the test: an encoder checked against bytes somebody
	# decided were right only proves it still agrees with itself.
	../RendeerAlpha/src/Core/Png.cpp
	../RendeerAlpha/src/vendor/stb_image/stb_image.cpp
	# A mesh resolves to vertices against a rectangle, and refuses a file it cannot read.
	# Both are device-free, so both are covered here -- the compiler that writes those
	# files needs esbuild and QuickJS and is checked end to end by the rda_mesh test.
	../RendeerAlpha/src/GraphicalObjects/MeshAsset.cpp
	# Watching a theme and its meshes. Compiled without RDA_ENABLE_HOT_RELOAD, so what is
	# covered here is the half that needs no compiler: noticing a changed artifact, re-
	# reading it, and telling the theme to forget what it resolved against the old one.
	# A Theme is plain data and a blueprint is a POD array, so neither needs a device.
	../RendeerAlpha/src/Layout/StyleWatch.cpp
	../RendeerAlpha/src/GraphicalObjects/GuiTheme.cpp
	../RendeerAlpha/src/Layout/Blueprint.cpp
	../RendeerAlpha/src/Layout/ThemeSchema.cpp
	../RendeerAlpha/src/GraphicalObjects/Motion.cpp
	# Motion::along follows a route, and a route is measured by the SVG path
	# flattener -- so the animation layer the tests exist to cover now reaches both.
	../RendeerAlpha/src/Core/Route.cpp
	../RendeerAlpha/src/Core/Svg.cpp
	../RendeerAlpha/src/Core/Colour.cpp
	../RendeerAlpha/src/GraphicalObjects/TileGrid.cpp
	../RendeerAlpha/src/GraphicalObjects/Syntax.cpp
	../RendeerAlpha/src/vendor/tinyxml2/tinyxml2.cpp
	../RendeerAlpha/src/Layout/ExpressionParser.cpp
	../RendeerAlpha/src/Core/Signals.cpp
	../RendeerAlpha/src/Core/Commands.cpp
	../RendeerAlpha/src/Core/Json.cpp
	../RendeerAlpha/src/Core/Tables.cpp
	# A table seen sorted and filtered, which is what a <grid> and a <list> show through.
	../RendeerAlpha/src/Layout/TableOrder.cpp
	# Several rows chosen at once, by key, with Ctrl and Shift.
	../RendeerAlpha/src/Layout/Selection.cpp
	# A layout's functions at run time: QuickJS, the natives, the deadline, the line map.
	# The QuickJS objects come from the engine's own target, below.
	../RendeerAlpha/src/Layout/ScriptHost.cpp
	../RendeerAlpha/src/Core/Utf8.cpp
	# What a layout writes as a key chord, and which keys a focused widget keeps. GLFW-free
	# on purpose, so it can be here.
	../RendeerAlpha/src/GraphicalObjects/Shortcut.cpp
	# What happened to a <viewport> -- resized, or the pointer over it -- summed for whoever reads. Its own lock and no
	# device, so it is covered here, waiting thread and all.
	../RendeerAlpha/src/GraphicalObjects/ViewportEvents.cpp
	# A frameless window's edges: which one the pointer is on and what dragging it does.
	# Arithmetic and a picture, kept apart from GLFW so they can be checked here.
	../RendeerAlpha/src/GraphicalObjects/WindowEdges.cpp
	# How a screen reader moves through a field: by character, word and line.
	../RendeerAlpha/src/GraphicalObjects/Accessible.cpp
	../RendeerAlpha/src/Logger/Logger.cpp
)

add_executable(rendeer_tests ${RDA_TEST_SOURCES})
target_include_directories(rendeer_tests PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/RendeerAlpha/include"
	"${CMAKE_SOURCE_DIR}/RendeerAlpha/src"
	${RDA_VULKAN_INCLUDE}
	${RDA_GLM_INCLUDE}
)
if(WIN32)
	target_compile_definitions(rendeer_tests PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# The syntax languages the engine ships are data, and data can be wrong in ways a
# compiler cannot see. The tests read the real file rather than a copy of it, so a typo
# in it fails here rather than in an editor that quietly draws plain text.
target_compile_definitions(rendeer_tests PRIVATE
	RDA_ENGINE_RES_DIR="${CMAKE_SOURCE_DIR}/RendeerAlpha/res")

target_link_libraries(rendeer_tests PRIVATE rendeer_quickjs)

add_test(NAME rendeer_tests COMMAND rendeer_tests)

# The compiler's half of a layout's functions: esbuild's bundle, acorn, which functions a
# binding reaches, what ships as code and what as a value, and the bytecode -- one layout
# written to use all of it, compiled. And the same layout refused under
# --no-runtime-code, because that promise is only worth anything if it is kept.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	add_test(NAME rda_runtime_code
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/functions.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/functions.rdab")
	add_test(NAME rda_no_runtime_code
		COMMAND rda layout --no-runtime-code "${CMAKE_CURRENT_SOURCE_DIR}/script/functions.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/functions-refused.rdab")
	# A binding that reads a local of the code around it, when the top level has the same
	# name: it would compile and read the wrong one, so it has to be refused.
	add_test(NAME rda_shadowed_local
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/shadowed.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/shadowed.rdab")
	set_tests_properties(rda_runtime_code rda_no_runtime_code rda_shadowed_local PROPERTIES TIMEOUT 120)
	# Refused for the reason, not for any reason: a missing esbuild fails too.
	set_tests_properties(rda_no_runtime_code PROPERTIES
		PASS_REGULAR_EXPRESSION "asked for no run-time code")
	set_tests_properties(rda_shadowed_local PROPERTIES
		PASS_REGULAR_EXPRESSION "is declared by the code around this binding")
endif()

# state.ts's code: the same compiler, reading compute and run out of a declaration. Then the
# C++ the declaration generates, compiled into the tests and run -- the typed handler, the
# embedded bytecode, the computed values -- since nothing else in the build compiles a
# header with code in it. Without the tool or esbuild, the tests that need it are left out.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	add_test(NAME rda_state_code
		COMMAND rda python "${CMAKE_CURRENT_SOURCE_DIR}/script/state.ts"
		                   "${CMAKE_CURRENT_BINARY_DIR}/state.py")
	add_test(NAME rda_state_no_runtime_code
		COMMAND rda python --no-runtime-code "${CMAKE_CURRENT_SOURCE_DIR}/script/state.ts"
		                   "${CMAKE_CURRENT_BINARY_DIR}/state-refused.py")
	set_tests_properties(rda_state_code rda_state_no_runtime_code PROPERTIES TIMEOUT 120)
	set_tests_properties(rda_state_code PROPERTIES
		PASS_REGULAR_EXPRESSION "3 computed, 1 action, [0-9]+ bytes of code")
	set_tests_properties(rda_state_no_runtime_code PROPERTIES
		PASS_REGULAR_EXPRESSION "asked for no run-time code")

	set(state_header "${CMAKE_CURRENT_BINARY_DIR}/generated/RdaState.h")
	add_custom_command(
		OUTPUT  "${state_header}"
		COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> state "${CMAKE_CURRENT_SOURCE_DIR}/script/state.ts"
		        "${state_header}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/script/state.ts"
		        "${CMAKE_CURRENT_SOURCE_DIR}/script/state-helpers.ts" rda
		COMMENT "Generating the state header the tests run"
		VERBATIM
	)
	target_sources(rendeer_tests PRIVATE "${state_header}")
	target_include_directories(rendeer_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
	target_compile_definitions(rendeer_tests PRIVATE RDA_TEST_STATE_HEADER=1)
endif()

# The other end of a mesh: esbuild, the geometry prelude, QuickJS, the writer, and the
# reader -- `rda mesh` parses back what it has written, so one command covers all of it and
# a non-zero exit means one of them moved without the others. mesh/badge.ts is written to
# use every primitive a real mesh uses, which is why it is a strange-looking shape.
#
# Skipped with a reason rather than failing when the pieces are not there, the way the
# binding tests are: a configuration without the tool has nothing to run this with.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	add_test(NAME rda_mesh
		COMMAND rda mesh "${CMAKE_CURRENT_SOURCE_DIR}/mesh/badge.ts"
		                 "${CMAKE_CURRENT_BINARY_DIR}/badge.rdam")
	set_tests_properties(rda_mesh PROPERTIES TIMEOUT 120)
elseif(NOT TARGET rda)
	message(STATUS "Mesh test: RDA_BUILD_TOOLS is off, skipping rda_mesh")
else()
	message(STATUS "Mesh test: esbuild was not found, skipping rda_mesh")
endif()

# Every test above that runs the tool is given the esbuild the build itself uses, the way the
# custom commands are (RDA_TOOL_ENV). The tool finds one through RDA_ESBUILD, through the path
# a Debug build bakes in, or by looking upward for node_modules -- and a Release tool has no
# baked path, deliberately, since a shipped binary should not carry the build machine's. So
# without this a Release tree passed only where a node_modules happened to sit above it, and a
# worktree or a copied build tree failed every one with "esbuild not found". By name, so a test
# added later is covered without anyone remembering to.
if(RDA_ESBUILD_EXECUTABLE)
	get_property(rda_tool_tests DIRECTORY PROPERTY TESTS)
	list(FILTER rda_tool_tests INCLUDE REGEX "^rda_")
	if(rda_tool_tests)
		set_tests_properties(${rda_tool_tests} PROPERTIES
			ENVIRONMENT "RDA_ESBUILD=${RDA_ESBUILD_EXECUTABLE}")
	endif()
endif()
