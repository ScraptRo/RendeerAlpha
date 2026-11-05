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
add_executable(rendeer_engine_tests Main.cpp RegistryTests.cpp LifecycleTests.cpp FontTests.cpp
	PlacementTests.cpp)
target_include_directories(rendeer_engine_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(rendeer_engine_tests PRIVATE
	RDA_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../RendeerAlpha/res/fonts/CascadiaMono.ttf")
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
	# The same layout written twice, in JSX and as an object. What they compile to has to be
	# the same bytes: that is the whole of what the object form promises, and it checks every
	# kind of node and binding in the file at once.
	add_test(NAME rda_layout_as_jsx
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/same_as_jsx.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/same.rdab")
	add_test(NAME rda_layout_as_object
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/same_as_object.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/same-object.rdab")
	add_test(NAME rda_layout_forms_agree
		COMMAND "${CMAKE_COMMAND}" -E compare_files "${CMAKE_CURRENT_BINARY_DIR}/same.rdab"
		                                            "${CMAKE_CURRENT_BINARY_DIR}/same-object.rdab")
	set_tests_properties(rda_layout_as_jsx rda_layout_as_object PROPERTIES TIMEOUT 120)
	set_tests_properties(rda_layout_forms_agree PROPERTIES
		DEPENDS "rda_layout_as_jsx;rda_layout_as_object")
	# Text between tags: named for what it is, rather than guessed at as a fragment.
	add_test(NAME rda_text_between_tags
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/text_between_tags.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/text-between-tags.rdab")
	set_tests_properties(rda_text_between_tags PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "\"English\" is text between the tags of")
	# onOpen anywhere but on the root. Compiled, it would be dropped with a line in the log,
	# and what the author wrote would quietly never run.
	add_test(NAME rda_on_open_off_root
		COMMAND rda layout "${CMAKE_CURRENT_SOURCE_DIR}/script/on_open_off_root.tsx"
		                   "${CMAKE_CURRENT_BINARY_DIR}/on-open-off-root.rdab")
	set_tests_properties(rda_on_open_off_root PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "belongs on the layout's root element")
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

# `rda init`: a project made from nothing, in each of the five shapes, and then built by the
# same `rda build` a person would run next -- the point of the command is that what it writes
# compiles with no edits, so a template that drifts from the tool is caught here and not by
# the next newcomer. Made into the build tree with --force, so a second run of the suite
# overwrites rather than being refused; the refusal has a test of its own, which runs after
# a project exists. The folder names are not all valid package names as they stand, on
# purpose: `Py_App` has to come out as `py-app`.
#
# A kind is its folder, the flag that makes it, the flag `rda build` needs to write the same
# state module, and how many files init says it wrote.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	set(init_dir "${CMAKE_CURRENT_BINARY_DIR}/init")
	set(init_kinds    interface   Py_App     node-app    CSharpApp   cpp.app)
	set(init_flags    ""          --python   --node      --csharp    --cpp)
	set(init_builds   ""          --python   --node      --csharp    "")
	set(init_files    5           6          7           7           7)
	list(LENGTH init_kinds init_count)
	math(EXPR init_last "${init_count} - 1")
	foreach(i RANGE ${init_last})
		list(GET init_kinds  ${i} kind)
		list(GET init_flags  ${i} flag)
		list(GET init_builds ${i} build_flag)
		list(GET init_files  ${i} count)
		string(TOLOWER "${kind}" id)
		string(REGEX REPLACE "[^a-z0-9]" "_" id "${id}")
		add_test(NAME rda_init_${id}
			COMMAND rda init "${init_dir}/${kind}" ${flag} --force)
		set_tests_properties(rda_init_${id} PROPERTIES TIMEOUT 120
			PASS_REGULAR_EXPRESSION "project [^\n]*${kind}, ${count} files")
		# Already built by init itself, so what this proves is that a separate `rda build`
		# agrees: nothing left to write, and nothing that fails.
		add_test(NAME rda_init_build_${id}
			COMMAND rda build "${init_dir}/${kind}" ${build_flag})
		set_tests_properties(rda_init_build_${id} PROPERTIES TIMEOUT 120
			DEPENDS rda_init_${id}
			PASS_REGULAR_EXPRESSION "rda: 0 written, [0-9]+ up to date")
	endforeach()

	# Refused, and says which files and what to do. The project is there because the test
	# above made it.
	add_test(NAME rda_init_refuses_overwrite
		COMMAND rda init "${init_dir}/Py_App" --python)
	set_tests_properties(rda_init_refuses_overwrite PROPERTIES TIMEOUT 120
		DEPENDS rda_init_py_app
		PASS_REGULAR_EXPRESSION "nothing was changed[^\n]*\n(  [^\n]+\n)+Use --force")
	# Two backends is a mistake and not a request for both.
	add_test(NAME rda_init_one_backend
		COMMAND rda init "${init_dir}/two" --python --node)
	set_tests_properties(rda_init_one_backend PROPERTIES TIMEOUT 120
		PASS_REGULAR_EXPRESSION "a project has one backend")
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

# What `rda build` makes of a folder with nothing said about it: the language of the backend is
# read off the files in the project root, and the state module of each one written. The five
# projects `rda init` made above are the five kinds, so they are what is asked; a folder with
# two backends in it and one with none are made here, at configure time, because init makes
# neither. Each test names the files it matched, which is the line a person reads.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	set(detect_dir "${CMAKE_CURRENT_BINARY_DIR}/detect")
	foreach(kind both none)
		file(MAKE_DIRECTORY "${detect_dir}/${kind}/res/layouts")
		configure_file("${CMAKE_SOURCE_DIR}/Cli/templates/common/state.ts"
		               "${detect_dir}/${kind}/res/state.ts" COPYONLY)
	endforeach()
	file(WRITE "${detect_dir}/both/app.py" "print('hello')\n")
	file(WRITE "${detect_dir}/both/package.json" "{}\n")

	add_test(NAME rda_detect_python COMMAND rda build "${init_dir}/Py_App")
	add_test(NAME rda_detect_node   COMMAND rda build "${init_dir}/node-app")
	add_test(NAME rda_detect_csharp COMMAND rda build "${init_dir}/CSharpApp")
	add_test(NAME rda_detect_cpp    COMMAND rda build "${init_dir}/cpp.app")
	add_test(NAME rda_detect_none   COMMAND rda build "${init_dir}/interface")
	add_test(NAME rda_detect_several COMMAND rda build "${detect_dir}/both")
	# --force, so that the modules are written on every run of the suite and not only the first.
	add_test(NAME rda_detect_several_writes COMMAND rda build "${detect_dir}/both" --force)
	# A flag is the whole of the answer: nothing is looked for, so nothing is announced.
	add_test(NAME rda_detect_flag_wins COMMAND rda build "${detect_dir}/both" --node)
	set_tests_properties(rda_detect_python PROPERTIES DEPENDS rda_init_py_app
		PASS_REGULAR_EXPRESSION "rda: detected Python \\(app\\.py\\)")
	set_tests_properties(rda_detect_node PROPERTIES DEPENDS rda_init_node_app
		PASS_REGULAR_EXPRESSION "rda: detected Node \\(package\\.json\\)")
	set_tests_properties(rda_detect_csharp PROPERTIES DEPENDS rda_init_csharpapp
		PASS_REGULAR_EXPRESSION "rda: detected C# \\([^)]*\\.csproj\\)")
	set_tests_properties(rda_detect_cpp PROPERTIES DEPENDS rda_init_cpp_app
		PASS_REGULAR_EXPRESSION "rda: detected C\\+\\+ \\(CMakeLists\\.txt\\)\n[^\n]*generated by CMake")
	set_tests_properties(rda_detect_none PROPERTIES DEPENDS rda_init_interface
		PASS_REGULAR_EXPRESSION "rda: no backend found in [^\n]*so no state module is written -- pass --python, --node or --csharp")
	set_tests_properties(rda_detect_several PROPERTIES
		PASS_REGULAR_EXPRESSION "rda: detected Python \\(app\\.py\\), Node \\(package\\.json\\)")
	set_tests_properties(rda_detect_several_writes PROPERTIES DEPENDS rda_detect_several
		PASS_REGULAR_EXPRESSION "state\\.ts -> [^\n]*both/state\\.py[^\n]*\n[^\n]*state\\.ts -> [^\n]*both/state\\.mjs")
	set_tests_properties(rda_detect_flag_wins PROPERTIES DEPENDS rda_detect_several_writes
		FAIL_REGULAR_EXPRESSION "detected")
	set_tests_properties(rda_detect_python rda_detect_node rda_detect_csharp rda_detect_cpp
		rda_detect_none rda_detect_several rda_detect_several_writes rda_detect_flag_wins
		PROPERTIES TIMEOUT 120)
endif()

# Names the widget schema does not know. A typo used to compile and be ignored when the layout
# was loaded; now it is a warning with what was probably meant, and an error under --strict.
# misspelt.tsx has one of each kind, every_name.tsx has only names that are valid -- the
# structural ones the compiler reads itself, a component's own props, an object-form element --
# and must come out silent, because a warning on a valid name is the one way this does harm.
# A test that passes on a regular expression does not look at the exit code, so the ones that
# are about exit codes are separate and have none.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	set(names_src "${CMAKE_CURRENT_SOURCE_DIR}/script")
	set(names_out "${CMAKE_CURRENT_BINARY_DIR}/names")
	add_test(NAME rda_names_element COMMAND rda layout "${names_src}/misspelt.tsx" "${names_out}-1.rdab")
	add_test(NAME rda_names_property COMMAND rda layout "${names_src}/misspelt.tsx" "${names_out}-2.rdab")
	add_test(NAME rda_names_known_guess COMMAND rda layout "${names_src}/misspelt.tsx" "${names_out}-3.rdab")
	add_test(NAME rda_names_nothing_close COMMAND rda layout "${names_src}/misspelt.tsx" "${names_out}-4.rdab")
	add_test(NAME rda_names_still_builds COMMAND rda layout "${names_src}/misspelt.tsx" "${names_out}-5.rdab")
	add_test(NAME rda_names_strict_fails COMMAND rda layout --strict "${names_src}/misspelt.tsx" "${names_out}-6.rdab")
	add_test(NAME rda_names_strict_says COMMAND rda layout --strict "${names_src}/misspelt.tsx" "${names_out}-7.rdab")
	add_test(NAME rda_names_clean COMMAND rda layout "${names_src}/every_name.tsx" "${names_out}-8.rdab")
	add_test(NAME rda_names_clean_strict COMMAND rda layout --strict "${names_src}/every_name.tsx" "${names_out}-9.rdab")
	add_test(NAME rda_names_strict_build COMMAND rda build --strict "${init_dir}/interface")
	set_tests_properties(rda_names_element PROPERTIES
		PASS_REGULAR_EXPRESSION "warning: [^\n]*<buton> 'root/go' is not an element; did you mean <button>\\?")
	set_tests_properties(rda_names_property PROPERTIES
		PASS_REGULAR_EXPRESSION "warning: [^\n]*<button> 'root/again' has no property 'widht'; did you mean 'width'\\?")
	set_tests_properties(rda_names_known_guess PROPERTIES
		PASS_REGULAR_EXPRESSION "<checkbox> 'root/remember' has no property 'text'; did you mean 'label'\\?")
	set_tests_properties(rda_names_nothing_close PROPERTIES
		PASS_REGULAR_EXPRESSION "<label> 'root/title' has no property 'zebra'; it takes text, wrap, spans")
	# Warnings do not fail the build: no expression, so the exit code is what is checked.
	# --strict does, and says that it was --strict that did.
	set_tests_properties(rda_names_strict_fails PROPERTIES WILL_FAIL TRUE)
	set_tests_properties(rda_names_strict_says PROPERTIES
		PASS_REGULAR_EXPRESSION "has no property 'widht'[^\n]*\n[^\n]*\n[^\n]*\n\\(--strict: without it these are warnings")
	set_tests_properties(rda_names_clean rda_names_clean_strict PROPERTIES
		FAIL_REGULAR_EXPRESSION "warning:|is not an element|has no property")
	set_tests_properties(rda_names_strict_build PROPERTIES DEPENDS rda_init_interface
		FAIL_REGULAR_EXPRESSION "has no property|is not an element")
	set_tests_properties(rda_names_element rda_names_property rda_names_known_guess
		rda_names_nothing_close rda_names_still_builds rda_names_strict_fails rda_names_strict_says
		rda_names_clean rda_names_clean_strict rda_names_strict_build PROPERTIES TIMEOUT 120)
endif()

# `rda elements`: the widget schema read back, from the table the loader and the .d.ts are
# written from. No esbuild: it compiles nothing.
if(TARGET rda)
	add_test(NAME rda_elements_list COMMAND rda elements)
	add_test(NAME rda_elements_one COMMAND rda elements checkbox)
	add_test(NAME rda_elements_events COMMAND rda elements checkbox)
	add_test(NAME rda_elements_json COMMAND rda elements checkbox --json)
	add_test(NAME rda_elements_unknown COMMAND rda elements chekbox)
	set_tests_properties(rda_elements_list PROPERTIES
		PASS_REGULAR_EXPRESSION "elements\\. `rda elements <name>` lists the properties[^\n]*\n\n  container  groups children")
	set_tests_properties(rda_elements_one PROPERTIES
		PASS_REGULAR_EXPRESSION "<checkbox>\n  a labelled boolean toggle\n\nProperties\n  label  \\[string\\]  \\(can be bound\\)\n      the text beside the box")
	set_tests_properties(rda_elements_events PROPERTIES
		PASS_REGULAR_EXPRESSION "Events\n  onChange  \\[event with a boolean\\]")
	set_tests_properties(rda_elements_json PROPERTIES
		PASS_REGULAR_EXPRESSION "\"name\": \"label\", \"type\": \"string\", \"bindable\": true, \"common\": false")
	set_tests_properties(rda_elements_unknown PROPERTIES
		PASS_REGULAR_EXPRESSION "there is no element called `chekbox`. The elements are: container, panel")
endif()

# `rda doctor`: says what it found. What it finds depends on the machine, so what is checked is
# that each question is asked and answered in a line of its own -- not what the answer is.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	add_test(NAME rda_doctor COMMAND rda doctor)
	add_test(NAME rda_doctor_project COMMAND rda doctor "${init_dir}/interface")
	# A folder that is not a project: something it needs is missing, so it fails, and says what.
	add_test(NAME rda_doctor_missing COMMAND rda doctor "${CMAKE_CURRENT_BINARY_DIR}/no-such-project")
	add_test(NAME rda_doctor_missing_fails COMMAND rda doctor "${CMAKE_CURRENT_BINARY_DIR}/no-such-project")
	set_tests_properties(rda_doctor PROPERTIES
		PASS_REGULAR_EXPRESSION "rda doctor\n  ok  +rda +[^\n]*\\(engine [0-9.]+\\)\n  ok  +esbuild +[^\n]*\n  [^\n]*rda-preview")
	set_tests_properties(rda_doctor_project PROPERTIES DEPENDS rda_init_interface
		PASS_REGULAR_EXPRESSION "project [^\n]*interface\n  ok  +res/ [^\n]*\n  ok  +state\\.ts[^\n]*\n  ok  +themes +1 theme in res/themes/\n  ok  +layouts +1 file in res/layouts/\n  ok  +backend +none found")
	set_tests_properties(rda_doctor_missing PROPERTIES
		PASS_REGULAR_EXPRESSION "PROBLEM  project +no such folder\n +-> check the path")
	set_tests_properties(rda_doctor_missing_fails PROPERTIES WILL_FAIL TRUE)
	set_tests_properties(rda_doctor rda_doctor_project rda_doctor_missing rda_doctor_missing_fails
		PROPERTIES TIMEOUT 120)
endif()

# `rda dev`, without running anything that opens a window: what it would do, and -- with a
# backend that is a script printing a line and ending -- that it builds, starts it, waits for it
# and hands its exit code on. The script is Python, so the last two need one on the PATH.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	add_test(NAME rda_dev_dry_run COMMAND rda dev "${init_dir}/Py_App" --dry-run)
	add_test(NAME rda_dev_dry_run_prototype COMMAND rda dev "${init_dir}/interface" --dry-run)
	add_test(NAME rda_dev_cpp COMMAND rda dev "${init_dir}/cpp.app" --dry-run)
	set_tests_properties(rda_dev_dry_run PROPERTIES DEPENDS rda_init_py_app
		PASS_REGULAR_EXPRESSION "rda: would build [^\n]*Py_App\nrda: would run: python3? app\\.py ")
	set_tests_properties(rda_dev_dry_run_prototype PROPERTIES DEPENDS rda_init_interface
		PASS_REGULAR_EXPRESSION "rda: would run: rda prototype [^\n]*interface")
	set_tests_properties(rda_dev_cpp PROPERTIES DEPENDS rda_init_cpp_app
		PASS_REGULAR_EXPRESSION "a C\\+\\+ project is its own program, and `dev` cannot start it")
	set_tests_properties(rda_dev_dry_run rda_dev_dry_run_prototype rda_dev_cpp PROPERTIES TIMEOUT 120)

	find_program(RDA_TEST_PYTHON NAMES python python3)
	if(RDA_TEST_PYTHON)
		set(dev_dir "${CMAKE_CURRENT_BINARY_DIR}/dev")
		foreach(kind ok fails)
			file(MAKE_DIRECTORY "${dev_dir}/${kind}/res/layouts")
			configure_file("${CMAKE_SOURCE_DIR}/Cli/templates/common/state.ts"
			               "${dev_dir}/${kind}/res/state.ts" COPYONLY)
		endforeach()
		file(WRITE "${dev_dir}/ok/app.py" "print('fake backend started')\n")
		file(WRITE "${dev_dir}/fails/app.py" "import sys\nprint('fake backend failing')\nsys.exit(3)\n")
		add_test(NAME rda_dev_runs_backend COMMAND rda dev "${dev_dir}/ok")
		add_test(NAME rda_dev_passes_code_on COMMAND rda dev "${dev_dir}/fails")
		add_test(NAME rda_dev_code_is_nonzero COMMAND rda dev "${dev_dir}/fails")
		set_tests_properties(rda_dev_runs_backend PROPERTIES
			PASS_REGULAR_EXPRESSION "rda: running python3? app\\.py[^\n]*\n[^\n]*fake backend started\nrda: the backend ended with code 0")
		set_tests_properties(rda_dev_passes_code_on PROPERTIES
			PASS_REGULAR_EXPRESSION "fake backend failing\nrda: the backend ended with code 3")
		set_tests_properties(rda_dev_code_is_nonzero PROPERTIES WILL_FAIL TRUE)
		set_tests_properties(rda_dev_runs_backend rda_dev_passes_code_on rda_dev_code_is_nonzero
			PROPERTIES TIMEOUT 120)
	endif()
endif()

# `rda add screen`, on a project of its own that every run of the suite makes again: a screen
# in a project with no routes (which has to declare them, with the screen already there), one in
# a project that has them, and the two refusals. Each step is a test of its own that depends on
# the one before, because each is one command.
if(TARGET rda AND RDA_ESBUILD_EXECUTABLE)
	set(add_dir "${CMAKE_CURRENT_BINARY_DIR}/add")
	add_test(NAME rda_add_clean COMMAND "${CMAKE_COMMAND}" -E remove_directory "${add_dir}")
	add_test(NAME rda_add_init COMMAND rda init "${add_dir}/shop" --python --force)
	add_test(NAME rda_add_screen_declares_routes COMMAND rda add screen about "${add_dir}/shop")
	add_test(NAME rda_add_screen_with_routes COMMAND rda add screen settings "${add_dir}/shop")
	add_test(NAME rda_add_screen_refuses_layout COMMAND rda add screen about "${add_dir}/shop")
	add_test(NAME rda_add_remove_layout COMMAND "${CMAKE_COMMAND}" -E remove "${add_dir}/shop/res/layouts/settings.tsx")
	add_test(NAME rda_add_screen_refuses_route COMMAND rda add screen settings "${add_dir}/shop")
	set_tests_properties(rda_add_init PROPERTIES DEPENDS rda_add_clean
		PASS_REGULAR_EXPRESSION "project [^\n]*shop, 6 files")
	set_tests_properties(rda_add_screen_declares_routes PROPERTIES DEPENDS rda_add_init
		PASS_REGULAR_EXPRESSION "route `about`, and a `routes` declaration holding the screens already here\\)\n\nrda: this project had no routes[^\n]*\n  Python  from state import ROUTES\n[^\n]*open_routes\\(ROUTES[^\n]*\n(.|\n)*rda: [0-9]+ written")
	set_tests_properties(rda_add_screen_with_routes PROPERTIES DEPENDS rda_add_screen_declares_routes
		PASS_REGULAR_EXPRESSION "state\\.ts  \\(route `settings`\\)\n\nrda: detected Python(.|\n)*rda: [0-9]+ written")
	set_tests_properties(rda_add_screen_refuses_layout PROPERTIES DEPENDS rda_add_screen_with_routes
		PASS_REGULAR_EXPRESSION "about\\.tsx already exists; nothing was changed")
	set_tests_properties(rda_add_remove_layout PROPERTIES DEPENDS rda_add_screen_refuses_layout)
	set_tests_properties(rda_add_screen_refuses_route PROPERTIES DEPENDS rda_add_remove_layout
		PASS_REGULAR_EXPRESSION "state\\.ts already has a route called `settings` \\(it shows `settings`\\); nothing was changed")
	set_tests_properties(rda_add_clean rda_add_init rda_add_screen_declares_routes
		rda_add_screen_with_routes rda_add_screen_refuses_layout rda_add_remove_layout
		rda_add_screen_refuses_route PROPERTIES TIMEOUT 120)
endif()

# `rda shot --click <id>`: a widget named instead of a point. What can be tested without a GPU is
# the refusals, which happen before a window is asked for -- an id that is nothing, and an id that
# is the end of two paths -- and so they run anywhere rda-preview is. That the click lands on the
# widget needs a device, and was checked by hand: it is the one part this does not cover.
if(TARGET rda AND TARGET rda-preview AND RDA_ESBUILD_EXECUTABLE)
	set(click_dir "${CMAKE_CURRENT_BINARY_DIR}/click/dup")
	file(MAKE_DIRECTORY "${click_dir}/res/layouts" "${click_dir}/res/themes")
	configure_file("${CMAKE_SOURCE_DIR}/Cli/templates/common/theme.ts" "${click_dir}/res/themes/app.ts" COPYONLY)
	file(WRITE "${click_dir}/res/layouts/dup.tsx" [=[
export default function Dup() {
  return (
    <stack id="root" arrange="vertical">
      <stack id="left"><button id="x" text="Left" /></stack>
      <stack id="right"><button id="x" text="Right" /></stack>
    </stack>
  )
}
]=])
	add_test(NAME rda_shot_click_unknown COMMAND rda shot "${click_dir}" --click nope)
	add_test(NAME rda_shot_click_ambiguous COMMAND rda shot "${click_dir}" --click x)
	add_test(NAME rda_shot_hover_unknown COMMAND rda shot "${click_dir}" --hover nope)
	add_test(NAME rda_shot_click_refusal_fails COMMAND rda shot "${click_dir}" --click nope)
	set_tests_properties(rda_shot_click_unknown PROPERTIES
		PASS_REGULAR_EXPRESSION "rda: `nope` is not the id of any widget in [^\n]*dup\\.tsx\\. Its ids are: root, root/left, root/right, root/left/x, root/right/x")
	set_tests_properties(rda_shot_click_ambiguous PROPERTIES
		PASS_REGULAR_EXPRESSION "rda: `x` is the end of 2 widgets' ids in [^\n]*dup\\.tsx -- say more of the path: root/left/x, root/right/x")
	set_tests_properties(rda_shot_hover_unknown PROPERTIES
		PASS_REGULAR_EXPRESSION "rda: `nope` is not the id of any widget")
	set_tests_properties(rda_shot_click_refusal_fails PROPERTIES WILL_FAIL TRUE)
	set_tests_properties(rda_shot_click_unknown rda_shot_click_ambiguous rda_shot_hover_unknown
		rda_shot_click_refusal_fails PROPERTIES TIMEOUT 120)
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
