# To be called before using git.
# This retrieves the path to git executable, and raises an error on failure.
# It must be called manually so that products/builders that do not use git
# will not raise error if git cannot be found.
macro (be_find_git)
	if (NOT GITCOMMAND)
		find_program ( GITCOMMAND git 
			HINTS "C:\\Program Files\\Git\\bin" "C:\\Dev\\Git\\bin" "C:\\Program Files (x86)\\Git\\bin")
		mark_as_advanced ( FORCE GITCOMMAND )
	endif()
	if ( "${GITCOMMAND}" STREQUAL "GITCOMMAND-NOTFOUND" OR
		"${GITCOMMAND}" STREQUAL "" )
		message ( FATAL_ERROR "Can't find git command. Please set the GITCOMMAND variable" )
	endif()
endmacro ()