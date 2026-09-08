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
	../RendeerAlpha/src/Layout/Expression.cpp
	../RendeerAlpha/src/Layout/ThemeSchema.cpp
	../RendeerAlpha/src/GraphicalObjects/Motion.cpp
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
