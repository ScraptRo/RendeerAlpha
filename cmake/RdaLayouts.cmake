# Compiling layouts as part of the build.
#
# A .tsx layout is a source file that produces an artefact, exactly like a shader. Nobody
# should have to remember to run the compiler by hand, and a demo that begins with
# "first, run rda layout" has already lost the argument.
#
# The blueprint is written next to its source rather than into the build directory, and
# that is deliberate. An application links the loader, and the compiler only where something
# calls it -- hot reload does, in a Debug build -- so a release has no way to make a blueprint.
# Keeping the .rdab beside the .tsx makes it a committed artefact, so a build with no compiler
# (RDA_COMPILE_LAYOUTS off, or no esbuild) uses it as it is, the way a generated parser is.
#
#   rda_add_layouts(sandbox res/layouts/hello.tsx)

# Compiling widget meshes.
#
# Same shape as a layout: the source is TypeScript, the output sits beside it, and the
# build runs the compiler. What is different is the ordering. A mesh declares parameters
# that the theme may then set, so the theme schema is the engine's table *merged with
# every mesh this project has* -- which means the meshes have to be read before a theme
# or a .d.ts is checked against it.
#
# `rda build` gets that for free by having the whole project in one process. CMake gives
# each part its own, so the list is recorded here and handed to the theme and type rules
# as `--mesh` arguments. **Call this before rda_add_theme and rda_add_types**, or those
# two will be checked against the engine's table alone and a theme that sets a mesh's
# parameter will be refused as if it had misspelt a field.
#
#   rda_add_meshes(sandbox res/mesh/tab.ts)

function(rda_add_meshes target)
	if(NOT ARGN)
		return()
	endif()

	set(sources "")
	foreach(source IN LISTS ARGN)
		get_filename_component(absolute "${source}" ABSOLUTE)
		list(APPEND sources "${absolute}")
	endforeach()

	# Recorded whether or not anything can be compiled here, because the theme rule needs
	# the *declarations* either way -- and reading those needs only the source.
	set(RDA_MESH_FILES_${target} "${sources}" CACHE INTERNAL "")

	if(NOT RDA_COMPILE_LAYOUTS OR NOT TARGET rda OR NOT RDA_ESBUILD_EXECUTABLE)
		message(STATUS
			"Meshes for ${target}: not recompiled; using the .rdam files in the source tree")
		return()
	endif()

	set(outputs "")
	foreach(absolute IN LISTS sources)
		get_filename_component(directory "${absolute}" DIRECTORY)
		get_filename_component(stem "${absolute}" NAME_WE)
		set(compiled "${directory}/${stem}.rdam")

		add_custom_command(
			OUTPUT  "${compiled}"
			COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> mesh "${absolute}" "${compiled}"
			DEPENDS "${absolute}" rda
			COMMENT "Compiling mesh ${stem}.ts"
			VERBATIM
		)
		list(APPEND outputs "${compiled}")
	endforeach()

	add_custom_target(${target}_meshes DEPENDS ${outputs})
	add_dependencies(${target} ${target}_meshes)
endfunction()

function(rda_add_layouts target)
	if(NOT ARGN)
		return()
	endif()

	if(NOT RDA_COMPILE_LAYOUTS)
		message(STATUS
			"Layouts for ${target}: not recompiled (RDA_COMPILE_LAYOUTS is off); "
			"using the blueprints in the source tree")
		return()
	endif()

	if(NOT TARGET rda)
		# A build with RDA_BUILD_TOOLS off has no compiler to run. Its .rdab files come
		# from the source tree, compiled by a build that had one, or by CI.
		message(STATUS
			"Layouts for ${target}: not compiled here (the rda tool was not built); "
			"using the blueprints already in the source tree")
		return()
	endif()

	if(NOT RDA_ESBUILD_EXECUTABLE)
		message(STATUS
			"Layouts for ${target}: not recompiled (no esbuild); "
			"using the blueprints already in the source tree")
		return()
	endif()

	# RDA_RUNTIME_CODE off: a layout that would need a function at run time is a build
	# error rather than a blueprint carrying bytecode.
	set(runtime_flag "")
	if(DEFINED RDA_RUNTIME_CODE AND NOT RDA_RUNTIME_CODE)
		set(runtime_flag "--no-runtime-code")
	endif()

	set(outputs "")
	foreach(source IN LISTS ARGN)
		get_filename_component(absolute "${source}" ABSOLUTE)
		get_filename_component(directory "${absolute}" DIRECTORY)
		get_filename_component(stem "${absolute}" NAME_WE)
		set(blueprint "${directory}/${stem}.rdab")

		add_custom_command(
			OUTPUT  "${blueprint}"
			COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> layout ${runtime_flag} "${absolute}" "${blueprint}"
			DEPENDS "${absolute}" rda
			COMMENT "Compiling layout ${stem}.tsx"
			VERBATIM
		)
		list(APPEND outputs "${blueprint}")
	endforeach()

	# A target rather than plain outputs, so the blueprints are built before the
	# executable's post-build step copies res/ beside it.
	add_custom_target(${target}_layouts DEPENDS ${outputs})
	add_dependencies(${target} ${target}_layouts)
endfunction()
