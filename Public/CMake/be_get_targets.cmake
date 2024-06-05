# Retrieves the list of all cmake targets added in the given directory or any of its sub-directories.
# See cmake property BUILDSYSTEM_TARGETS.
# Optional flag funcArgs_SKIP_EXCLUDE_FROM_ALL will ignore targets which meet one of these conditions:
# - target has property EXCLUDE_FROM_ALL set to true
# - or ancestor directory of the target has property EXCLUDE_FROM_ALL set to true
function (be_get_targets result dir)
	cmake_parse_arguments (
		funcArgs
		"SKIP_EXCLUDE_FROM_ALL"
		""
		""
		${ARGN}
	)
	get_property (isExcludedFromAll DIRECTORY "${dir}" PROPERTY EXCLUDE_FROM_ALL)
	if (funcArgs_SKIP_EXCLUDE_FROM_ALL AND "${isExcludedFromAll}")
		# Skip directory.
		return ()
	endif ()
	# Recursive call on sub-directories.
	get_property (subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
	foreach (subdir IN LISTS subdirs)
		set (extraArgs "")
		if (funcArgs_SKIP_EXCLUDE_FROM_ALL)
			list (APPEND  extraArgs SKIP_EXCLUDE_FROM_ALL)
		endif ()
		be_get_targets(${result} "${subdir}" ${ARGN})
	endforeach()
	# Get targets added by the given directory exactly.
	get_property (targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
	if (funcArgs_SKIP_EXCLUDE_FROM_ALL)
		# Filter targets.
		set (targetsCopy ${targets})
		set (targets "")
		foreach (target ${targetsCopy})
			get_property (isExcludeFromAll TARGET ${target} PROPERTY EXCLUDE_FROM_ALL)
			if (NOT "${isExcludeFromAll}")
				list (APPEND targets ${target})
			endif ()
		endforeach ()
	endif ()
	set(${result} ${${result}} ${targets} PARENT_SCOPE)
endfunction()
