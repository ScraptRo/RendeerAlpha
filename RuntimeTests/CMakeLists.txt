# The test runner. Not a product, which is why it survives the trimming: it is the only
# executable proof that the wire format and the signal graph behave as documented.
#
# It compiles the sources it tests directly rather than linking the library, so the
# wire protocol stays testable even in a build that does not include it.
set(RDA_TEST_SOURCES
	SignalTests.cpp
	ExpressionTests.cpp
	../RendeerAlpha/src/Layout/Expression.cpp
	../RendeerAlpha/src/Layout/ExpressionParser.cpp
	../RendeerAlpha/src/Core/Signals.cpp
	../RendeerAlpha/src/Logger/Logger.cpp
)

# The wire protocol is carried over Windows named pipes, so its tests are too. They stay
# out of the build elsewhere rather than being ported to a transport that does not exist
# yet -- the format itself is platform-independent, and it is the transport that is not.
if(WIN32)
	list(APPEND RDA_TEST_SOURCES
		RuntimeTests.cpp
		../RendeerAlpha/src/Runtime/Host.cpp
		../RendeerAlpha/src/Runtime/Transport.cpp
	)
else()
	message(STATUS "Tests: the wire-protocol cases need named pipes and are skipped here")
endif()

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

add_test(NAME rendeer_tests COMMAND rendeer_tests)
