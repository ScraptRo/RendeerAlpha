# Compiling layouts as part of the build.
#
# A .tsx layout is a source file that produces an artefact, exactly like a shader. Nobody
# should have to remember to run the compiler by hand, and a demo that begins with
# "first, run rda layout" has already lost the argument.
#
# The blueprint is written next to its source rather than into the build directory, and
# that is deliberate. Only a Studio build has the compiler; an App build has the loader
# and nothing that could produce a blueprint. Keeping the .rdab beside the .tsx makes it
# a committed artefact, so an App-tier build of an application is self-sufficient — the
# same reason a project would commit a generated parser.
#
#   rda_add_layouts(sandbox res/layouts/hello.tsx)

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

	set(outputs "")
	foreach(source IN LISTS ARGN)
		get_filename_component(absolute "${source}" ABSOLUTE)
		get_filename_component(directory "${absolute}" DIRECTORY)
		get_filename_component(stem "${absolute}" NAME_WE)
		set(blueprint "${directory}/${stem}.rdab")

		add_custom_command(
			OUTPUT  "${blueprint}"
			COMMAND ${RDA_TOOL_ENV} $<TARGET_FILE:rda> layout "${absolute}" "${blueprint}"
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
