# Converts a cmake list "aa;bb;cc" to a json string list ["aa","bb","cc"].
function (toJsonStrList outVar inList)
	if (NOT inList)
		set (${outVar} "[]" PARENT_SCOPE)
		return ()
	endif ()
	# Double quotes must be escaped.
	string (REPLACE "\"" "\\\"" tmpOutVar "${inList}")
	list (JOIN tmpOutVar "\",\"" tmpOutVar)
	set (${outVar} "[\"${tmpOutVar}\"]" PARENT_SCOPE)
endfunction ()

# This function converts a cmake boolean variable (ON/OFF...) to a json boolean.
function (toJsonBool outVar inVar)
	if (${inVar})
		set (${outVar} "true" PARENT_SCOPE)
	else ()
		set (${outVar}  "false" PARENT_SCOPE)
	endif ()
endfunction ()
