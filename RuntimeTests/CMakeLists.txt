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
	../RendeerAlpha/src/Core/Tables.cpp
	../RendeerAlpha/src/Core/Utf8.cpp
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

add_test(NAME rendeer_tests COMMAND rendeer_tests)

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
