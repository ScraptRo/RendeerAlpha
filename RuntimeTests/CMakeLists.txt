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
	AccessibleDiffTests.cpp
	TableOrderTests.cpp
	SelectionTests.cpp
	ScriptHostTests.cpp
	StateCodeTests.cpp
	SourceMapTests.cpp
	CodegenTests.cpp
	TimerFormatTests.cpp
	ViewportEventTests.cpp
	WindowEdgeTests.cpp
	StampAndMailboxTests.cpp
	RetireQueueTests.cpp
	../RendeerAlpha/formats/src/Layout/Expression.cpp
	# Writing a picture, and reading it back with the decoder that is vendored for reading
	# other people's. The round trip is the test: an encoder checked against bytes somebody
	# decided were right only proves it still agrees with itself.
	../RendeerAlpha/foundation/src/Core/Png.cpp
	../RendeerAlpha/src/vendor/stb_image/stb_image.cpp
	# A mesh resolves to vertices against a rectangle, and refuses a file it cannot read.
	# Both are device-free, so both are covered here -- the compiler that writes those
	# files needs esbuild and QuickJS and is checked end to end by the rda_mesh test.
	../RendeerAlpha/ui/src/GraphicalObjects/MeshAsset.cpp
	# Watching a theme and its meshes. Nothing installs a compiler behind Layout/Rebuilder.h
	# here, so what is covered is the half that needs none: noticing a changed artifact, re-
	# reading it, and telling the theme to forget what it resolved against the old one.
	# A Theme is plain data and a blueprint is a POD array, so neither needs a device.
	# StyleWatch asks whether a file moved through Core/FileStamp, which is covered here too.
	../RendeerAlpha/foundation/src/Core/FileStamp.cpp
	../RendeerAlpha/runtime/src/Layout/StyleWatch.cpp
	../RendeerAlpha/formats/src/Layout/Rebuilder.cpp
	../RendeerAlpha/blueprint/src/Layout/ThemeLoader.cpp
	../RendeerAlpha/ui/src/GraphicalObjects/GuiTheme.cpp
	../RendeerAlpha/formats/src/Layout/Blueprint.cpp
	../RendeerAlpha/formats/src/Layout/ThemeSchema.cpp
	../RendeerAlpha/ui/src/GraphicalObjects/Motion.cpp
	# Motion::along follows a route, and a route is measured by the SVG path
	# flattener -- so the animation layer the tests exist to cover now reaches both.
	../RendeerAlpha/foundation/src/Core/Route.cpp
	../RendeerAlpha/foundation/src/Core/Svg.cpp
	../RendeerAlpha/foundation/src/Core/Colour.cpp
	../RendeerAlpha/ui/src/GraphicalObjects/TileGrid.cpp
	../RendeerAlpha/ui/src/GraphicalObjects/Syntax.cpp
	../RendeerAlpha/src/vendor/tinyxml2/tinyxml2.cpp
	../RendeerAlpha/toolchain/src/Layout/ExpressionParser.cpp
	../RendeerAlpha/state/src/Core/Signals.cpp
	../RendeerAlpha/state/src/Core/Commands.cpp
	../RendeerAlpha/foundation/src/Core/Json.cpp
	../RendeerAlpha/state/src/Core/Tables.cpp
	# A table seen sorted and filtered, which is what a <grid> and a <list> show through.
	../RendeerAlpha/state/src/Layout/TableOrder.cpp
	# Several rows chosen at once, by key, with Ctrl and Shift.
	../RendeerAlpha/ui/src/Layout/Selection.cpp
	# A layout's functions at run time: QuickJS, the natives, the deadline, the line map.
	# The QuickJS objects come from the engine's own target, below.
	../RendeerAlpha/runtime/src/Layout/Script/ScriptRuntime.cpp
	../RendeerAlpha/runtime/src/Layout/Script/ScriptValues.cpp
	../RendeerAlpha/runtime/src/Layout/Script/ScriptFrame.cpp
	../RendeerAlpha/runtime/src/Layout/Script/ScriptNatives.cpp
	../RendeerAlpha/runtime/src/Layout/Script/ScriptTimers.cpp
	../RendeerAlpha/runtime/src/Layout/Script/ScriptModules.cpp
	../RendeerAlpha/runtime/src/Layout/Script/ScriptState.cpp
	# The build-time half of the same question -- which line of which file -- read out of
	# esbuild's source map. The map is text, so no esbuild and no QuickJS: the tests hand it
	# one esbuild wrote.
	../RendeerAlpha/toolchain/src/Layout/SourceMap.cpp
	# What a state declaration is printed as, in four languages, and the writer they share.
	# Only the printing: the reader that builds the declaration from a state.ts needs esbuild
	# and QuickJS, so the tests hand the generators one built by hand.
	../RendeerAlpha/toolchain/src/Layout/Codegen/CodeWriter.cpp
	../RendeerAlpha/toolchain/src/Layout/Codegen/CppStateGen.cpp
	../RendeerAlpha/toolchain/src/Layout/Codegen/PythonStateGen.cpp
	../RendeerAlpha/toolchain/src/Layout/Codegen/NodeStateGen.cpp
	../RendeerAlpha/toolchain/src/Layout/Codegen/CSharpStateGen.cpp
	../RendeerAlpha/foundation/src/Core/Utf8.cpp
	# What a layout writes as a key chord, and which keys a focused widget keeps. GLFW-free
	# on purpose, so it can be here.
	../RendeerAlpha/ui/src/GraphicalObjects/Shortcut.cpp
	# What happened to a <viewport> -- resized, or the pointer over it -- summed for whoever reads. Its own lock and no
	# device, so it is covered here, waiting thread and all.
	../RendeerAlpha/sync/src/GraphicalObjects/ViewportEvents.cpp
	# A frameless window's edges: which one the pointer is on and what dragging it does.
	# Arithmetic and a picture, kept apart from GLFW so they can be checked here.
	../RendeerAlpha/platform/src/GraphicalObjects/WindowEdges.cpp
	# How a screen reader moves through a field: by character, word and line.
	../RendeerAlpha/formats/src/GraphicalObjects/Accessible.cpp
	../RendeerAlpha/formats/src/GraphicalObjects/AccessibleDiff.cpp
	../RendeerAlpha/foundation/src/Logger/Logger.cpp
)

add_executable(rendeer_tests ${RDA_TEST_SOURCES})
target_include_directories(rendeer_tests PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	${RDA_ENGINE_INCLUDE_DIRS}
	# The JavaScript the toolchain embeds -- ScriptPreludes.h, which TimerFormatTests runs,
	# includes the preludes as generated headers.
	"${RDA_GENERATED_DIR}"
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

# The blueprint layer's records, checked against the schema they implement. Linked against
# the engine rather than compiled from its sources: a record reaches every widget type, and
# none of them needs a device until it is painted -- so this runs anywhere the engine links.
add_executable(rendeer_engine_tests Main.cpp RegistryTests.cpp)
target_include_directories(rendeer_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rendeer_engine_tests PRIVATE rendeer)
add_test(NAME rendeer_engine_tests COMMAND rendeer_engine_tests)

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
	# The plainest write in a value binding. The grammar refuses it and hands it on, and what
	# is past the grammar has to refuse it too, so this is the whole path and not half of it.
	add_test(NAME rda_value_binding_write
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/value_write.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/value-write.rdab")
	set_tests_properties(rda_runtime_code rda_no_runtime_code rda_shadowed_local
	                     rda_value_binding_write PROPERTIES TIMEOUT 120)
	# Refused for the reason, not for any reason: a missing esbuild fails too.
	set_tests_properties(rda_no_runtime_code PROPERTIES
		PASS_REGULAR_EXPRESSION "asked for no run-time code")
	set_tests_properties(rda_shadowed_local PROPERTIES
		PASS_REGULAR_EXPRESSION "is declared by the code around this binding")
	set_tests_properties(rda_value_binding_write PROPERTIES
		PASS_REGULAR_EXPRESSION "a value binding cannot write state")
	# A component that throws while the layout is built, through a helper it imports: the
	# error names the helper's line and then the component's, as they are in the two files.
	# Read off esbuild's bundle, both frames said throws.tsx, at lines 4 and 10 -- the first
	# in the wrong file, the second three lines short.
	add_test(NAME rda_layout_error_line
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/throws.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/throws.rdab")
	set_tests_properties(rda_layout_error_line PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "at titleOf \\([^)]*throws-helper\\.ts:4:[0-9]+\\)\n    at Title \\([^)]*throws\\.tsx:13:[0-9]+\\)")
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
		PASS_REGULAR_EXPRESSION "3 computed, 1 action, 1 watch, [0-9]+ bytes of code")
	set_tests_properties(rda_state_no_runtime_code PROPERTIES
		PASS_REGULAR_EXPRESSION "asked for no run-time code")
	# A `watch` that is not a function. It would otherwise read as a field with no watch, and
	# the reaction its author wrote would quietly never run.
	add_test(NAME rda_state_watch_refused
		COMMAND rda python "${CMAKE_CURRENT_SOURCE_DIR}/script/state-watch-bad.ts"
		                   "${CMAKE_CURRENT_BINARY_DIR}/state-watch-bad.py")
	set_tests_properties(rda_state_watch_refused PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "has a `watch` that is not a function")
	# A declaration that throws while it is read: the line of state.ts, 10, which is line 28
	# of the IIFE esbuild makes of it. It used to say neither -- the message alone.
	add_test(NAME rda_state_error_line
		COMMAND rda python "${CMAKE_CURRENT_SOURCE_DIR}/script/state-throws.ts"
		                   "${CMAKE_CURRENT_BINARY_DIR}/state-throws.py")
	set_tests_properties(rda_state_error_line PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "cannot read property 'max' of undefined\n    at [^\n]*state-throws\\.ts:10:[0-9]+\\)")
	# A computed value that throws when it is run once at build: caught, and said as which
	# value it was -- and now where, the compute's own frame under the message and none of
	# the evaluator's.
	add_test(NAME rda_state_compute_error_line
		COMMAND rda python "${CMAKE_CURRENT_SOURCE_DIR}/script/state-compute-throws.ts"
		                   "${CMAKE_CURRENT_BINARY_DIR}/state-compute-throws.py")
	set_tests_properties(rda_state_compute_error_line PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "state\\.first is computed, and running it once[^\n]*\n    at compute \\([^)]*state-compute-throws\\.ts:12:[0-9]+\\)\n*$")

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
	# A mesh the geometry helpers refuse: their frames, then the mesh's own line under them.
	# It used to be the message alone, here and for the theme below.
	add_test(NAME rda_mesh_error_line
		COMMAND rda mesh "${CMAKE_CURRENT_SOURCE_DIR}/mesh/throws.ts"
		                 "${CMAKE_CURRENT_BINARY_DIR}/throws.rdam")
	set_tests_properties(rda_mesh_error_line PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "an edge is a number or \\[anchor, pixels\\][^\n]*\n(    at [^\n]*<mesh>[^\n]*\n)+    at [^\n]*mesh/throws\\.ts:11:[0-9]+\\)")
	# And a theme, the same way: its line -- which, had it been printed, the colour helpers
	# written in front of the theme would have pushed down by their whole length.
	add_test(NAME rda_theme_error_line
		COMMAND rda theme "${CMAKE_CURRENT_SOURCE_DIR}/script/theme-throws.ts"
		                  "${CMAKE_CURRENT_BINARY_DIR}/theme-throws.rdth")
	set_tests_properties(rda_theme_error_line PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "cannot read property 'accent' of undefined\n    at [^\n]*theme-throws\\.ts:10:[0-9]+\\)")
elseif(NOT TARGET rda)
	message(STATUS "Mesh test: RDA_BUILD_TOOLS is off, skipping rda_mesh")
else()
	message(STATUS "Mesh test: esbuild was not found, skipping rda_mesh")
endif()

# `rda shot` and `rda prototype` are run by rda-preview, which rda hands the whole command line
# to and whose output and exit code it passes back. The project here is not there, so what
# comes back is the refusal printed before any window or device is asked for: the test needs
# neither, and passes only if the line reached rda-preview and its answer reached this test.
if(TARGET rda AND TARGET rda-preview)
	add_test(NAME rda_preview_forwarded
		COMMAND rda shot "${CMAKE_CURRENT_SOURCE_DIR}/no-such-project")
	set_tests_properties(rda_preview_forwarded PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "rda: no res/ directory in [^\n]*no-such-project")
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
