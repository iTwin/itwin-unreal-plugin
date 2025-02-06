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
