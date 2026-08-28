# The example. Also the tutorial: it is meant to be the first file someone opens.
add_executable(sandbox Sandbox.cpp)
target_link_libraries(sandbox PRIVATE rendeer)

# Where the layouts actually live.
#
# Assets are copied beside the executable, so a running program's res/ is a *copy* --
# editing the original would change nothing it can see. A development build is told where
# the originals are so hot reload watches the file a person is editing; a release build is
# not told, and watches only the blueprint it shipped with.
target_compile_definitions(sandbox PRIVATE
	$<$<CONFIG:Debug>:RDA_SANDBOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">)

# The interface, compiled from TypeScript into a blueprint before the assets are
# copied. Nobody runs the layout compiler by hand.
# The state both sides agree on, and the interface written against it.
rda_add_state(sandbox res/state.xml)
rda_add_theme(sandbox res/themes/sandbox.ts)
rda_add_types(sandbox res/layouts/rda.d.ts res/themes/sandbox.ts res/state.xml)
rda_add_layouts(sandbox res/layouts/hello.tsx)

add_custom_command(TARGET sandbox POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/res" "${RDA_OUTPUT_DIR}/res"
	COMMENT "Copying Sandbox res/ beside the binaries"
	VERBATIM
)
