# The test runner. Not a product, which is why it survives the trimming: it is the only
# executable proof that the wire format and the signal graph behave as documented.
#
# It compiles the sources it tests directly rather than linking the library, so the
# wire protocol stays testable even in a build that does not include it.
add_executable(rendeer_tests
	RuntimeTests.cpp
	SignalTests.cpp
	../RendeerAlpha/src/Runtime/Host.cpp
	../RendeerAlpha/src/Runtime/Transport.cpp
	../RendeerAlpha/src/Core/Signals.cpp
	../RendeerAlpha/src/Logger/Logger.cpp
)
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
