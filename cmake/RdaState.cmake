# Generating the C++ side of a state declaration.
#
# The declaration is the source of truth for both sides of the boundary: this produces
# the header an application includes, and the same file is handed to `rda types` so the
# TypeScript a layout is checked against describes the same signals. A name that exists
# on one side exists on the other, or neither builds.
#
#   rda_add_state(sandbox res/state.ts)

function(rda_add_state target declaration)
	get_filename_component(absolute "${declaration}" ABSOLUTE)
	set(generated "${CMAKE_BINARY_DIR}/generated/${target}")
	set(header "${generated}/RdaState.h")

	if(NOT TARGET rda)
		message(STATUS "State for ${target}: the toolchain is not built, using whatever header is already there")
		target_include_directories(${target} PRIVATE "${generated}")
		return()
	endif()

	# Wrapped like the others now that a declaration is TypeScript: generating the header
	# means transforming it, which means esbuild. And spelled $<TARGET_FILE:rda> because
	# once a command starts with something else CMake stops substituting the target name.
	add_custom_command(
		OUTPUT  "${header}"
		COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> state "${absolute}" "${header}"
		DEPENDS "${absolute}" rda
		COMMENT "Generating state for ${target}"
		VERBATIM
	)
	add_custom_target(${target}_state DEPENDS "${header}")
	add_dependencies(${target} ${target}_state)
	target_include_directories(${target} PRIVATE "${generated}")

	# Handed to the type emitter as well, so both sides come from this one file.
	set(RDA_STATE_FILE_${target} "${absolute}" CACHE INTERNAL "")
endfunction()

# Generating the TypeScript a layout is checked against.
#
# It depends on three things -- the widget schema compiled into the toolchain, the theme
# that defines the variants, and the state declaration -- so it is regenerated when any of
# them moves. A committed .d.ts that has drifted from the engine is worse than none: it
# reports errors that are not real and misses ones that are.
#
#   rda_add_types(sandbox res/layouts/rda.d.ts res/themes/sandbox.ts res/state.ts)

function(rda_add_types target output theme declaration)
	# Reading the theme means compiling it, so this needs esbuild for the same reason the
	# theme rule does. Without one, the .d.ts already in the tree is left alone.
	if(NOT TARGET rda OR NOT RDA_ESBUILD_EXECUTABLE)
		message(STATUS "Types for ${target}: not regenerated; using the .d.ts in the source tree")
		return()
	endif()
	get_filename_component(out_abs "${output}" ABSOLUTE)
	get_filename_component(theme_abs "${theme}" ABSOLUTE)
	get_filename_component(state_abs "${declaration}" ABSOLUTE)

	add_custom_command(
		OUTPUT  "${out_abs}"
		COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> types "${out_abs}" "${theme_abs}" "${state_abs}"
		DEPENDS "${theme_abs}" "${state_abs}" rda
		COMMENT "Generating layout types for ${target}"
		VERBATIM
	)
	add_custom_target(${target}_types DEPENDS "${out_abs}")
	add_dependencies(${target} ${target}_types)
endfunction()

# Compiling a theme.
#
# Same shape as layouts: the source is TypeScript, the output is a blueprint, and the
# build does it so nobody runs the compiler by hand. The compiled theme sits beside its
# source for the same reason a blueprint does -- an application that cannot compile one
# still needs a theme to load.
#
#   rda_add_theme(sandbox res/themes/sandbox.ts)

function(rda_add_theme target source)
	if(NOT TARGET rda OR NOT RDA_COMPILE_LAYOUTS OR NOT RDA_ESBUILD_EXECUTABLE)
		message(STATUS "Theme for ${target}: not recompiled; using the compiled theme in the source tree")
		return()
	endif()

	get_filename_component(absolute "${source}" ABSOLUTE)
	get_filename_component(directory "${absolute}" DIRECTORY)
	get_filename_component(stem "${absolute}" NAME_WE)
	set(compiled "${directory}/${stem}.rdth")

	add_custom_command(
		OUTPUT  "${compiled}"
		COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> theme "${absolute}" "${compiled}"
		DEPENDS "${absolute}" rda
		COMMENT "Compiling theme ${stem}.ts"
		VERBATIM
	)
	add_custom_target(${target}_theme DEPENDS "${compiled}")
	add_dependencies(${target} ${target}_theme)
endfunction()
