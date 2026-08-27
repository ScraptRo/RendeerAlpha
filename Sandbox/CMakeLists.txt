# The example. Also the tutorial: it is meant to be the first file someone opens.
add_executable(sandbox Sandbox.cpp)
target_link_libraries(sandbox PRIVATE rendeer)

# The interface, compiled from TypeScript into a blueprint before the assets are
# copied. Nobody runs the layout compiler by hand.
rda_add_layouts(sandbox res/layouts/hello.tsx)

add_custom_command(TARGET sandbox POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/res" "${RDA_OUTPUT_DIR}/res"
	COMMENT "Copying Sandbox res/ beside the binaries"
	VERBATIM
)
