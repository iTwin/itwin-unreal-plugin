# Escapes semicolons (replaces ";" with "\;") in the given string.
# This can be called with 1 or 2 arguments.
# Example with 1 argument (used as both input and output):
#
# set (myString "aa;bb;cc")
# be_escape_semicolons (myString)
# # now myString contains "aa\;bb\;cc"
#
# Example with 2 arguments (separate output and input):
#
# be_escape_semicolons (myString "aa;bb;cc")
# # now myString contains "aa\;bb\;cc"
function (be_escape_semicolons outVarName)
	if (${ARGC} GREATER 1)
		set (inVarValue "${ARGV1}")
	else ()
		set (inVarValue "${${outVarName}}")
	endif ()
	string (REPLACE ";" "\;" replaced "${inVarValue}")
	set (${outVarName} "${replaced}" PARENT_SCOPE)
endfunction ()

# Called by be_add_executable() & be_add_library().
function (be_process_binary target)
	# Bentley-specific processing (signing...).
	if (COMMAND be_process_binary_private)
		be_process_binary_private (${ARGV})
	endif ()
endfunction ()

# Same signature as built-in add_executable().
# May do additional processing if applicable.
function (be_add_executable)
	add_executable (${ARGV})
	be_process_binary (${ARGV0})
endfunction ()

# Same signature as built-in add_library().
# May do additional processing if applicable.
function (be_add_library)
	add_library (${ARGV})
	be_process_binary (${ARGV0})
endfunction ()
