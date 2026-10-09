# When a function contains arguments that are lists, we cannot use ARGV or ARGN,
# see https://gitlab.kitware.com/cmake/cmake/-/issues/22251
# The solution is to use ARGV0, ARGV1 etc which are correct.
# This macro return the range of ARGV# variables corresponding to extra arguments (ie. ARGN).
# Example:
# Suppose that we call this:
#
# addLib (myLyb static a "b;c" e)
#
# with addLib defined like that:
#
# function (addLib name type) # 2 named arguments
#     get_argn_range ()
#     # use argnFirstIndex and argnLastIndex ...
# endfunction ()
#
# then inside addLib, after the call to get_argn_range(), we have:
# - argnFirstIndex = 2
# - argnLastIndex = 4
# meaning that extra arguments are ARGV2, ARGV3 and ARGV4, with ARGV3 = "b;c" as expected.
#
# Also, to access arguments of the calling function, we use this trick:
# https://stackoverflow.com/questions/50365544/how-to-access-enclosing-functions-arguments-from-within-a-macro
macro (get_argn_range)
	list (LENGTH ${}ARGV argvLength)
	list (LENGTH ${}ARGN argnLength)
	math (EXPR argnFirstIndex "${argvLength}-${argnLength}")
	math (EXPR argnLastIndex "${${}ARGC}-1")
endmacro ()

# Internal helper function used by be_add_custom_command() & be_add_custom_target()
function (be_add_custom_step stepType)
	# If a command is currently parsed, appends its echoed version.
	macro (append_command)
		if (${isCmd})
			# Note: the first COMMAND is already appended.
			list (APPEND newArgs echo ${curCmd} COMMAND ${curCmd})
			set (isCmd FALSE)
		endif ()
	endmacro ()
	# keywords list up-to-date as of cmake 3.20.2... :-/
	if (${stepType} STREQUAL command)
		set (keywords OUTPUT COMMAND MAIN_DEPENDENCY DEPENDS BYPRODUCTS IMPLICIT_DEPENDS WORKING_DIRECTORY COMMENT DEPFILE JOB_POOL VERBATIM APPEND USES_TERMINAL COMMAND_EXPAND_LISTS TARGET PRE_BUILD PRE_LINK POST_BUILD)
	elseif (${stepType} STREQUAL target)
		set (keywords ALL COMMAND DEPENDS BYPRODUCTS WORKING_DIRECTORY COMMENT JOB_POOL VERBATIM USES_TERMINAL COMMAND_EXPAND_LISTS SOURCES)
	else ()
		message (FATAL_ERROR "be_add_custom_stuff: unknown stepType ${stepType}")
	endif ()
	set (newArgs "")
	set (isCmd FALSE)
	get_argn_range ()
	foreach (argIdx RANGE ${argnFirstIndex} ${argnLastIndex})
		be_escape_semicolons (arg "${ARGV${argIdx}}")
		# Check if the argument is a keyword.
		list(FIND keywords "${arg}" sepIdx)
		if (NOT ${sepIdx} EQUAL -1)
			# We reached a keyword, add the currently parsed command if any.
			append_command ()
		endif ()
		# If we are currently parsing a command, add it to the cmd buffer.
		# Otherwise, just append the arg to the new list.
		if (${isCmd})
			list (APPEND curCmd "${arg}")
		else ()
			list (APPEND newArgs "${arg}")
		endif ()
		# Check if we enter a command.
		if ("zz${arg}" STREQUAL "zzCOMMAND") # Use dummy prefix to prevent an error when arg contains TARGET (if (TARGET ...) has a special meaning).
			set (isCmd TRUE)
			set (curCmd "")
		endif ()
	endforeach ()
	# Add the currently parsed command if any.
	append_command ()
	# Forward new args to built-in command.
	if (${stepType} STREQUAL command)
		add_custom_command (${newArgs})
	elseif (${stepType} STREQUAL target)
		add_custom_target (${newArgs})
	endif ()
endfunction ()

macro (escape_args)
	set (escapedArgs "")
	get_argn_range ()
	foreach (argIdx RANGE ${argnFirstIndex} ${argnLastIndex})
		# See doc of get_argn_range() for the trick to access arguments of the calling function.
		be_escape_semicolons (arg "${${}ARGV${argIdx}}")
		list (APPEND escapedArgs "${arg}")
	endforeach ()
endmacro ()

# Takes the same arguments as built-in command add_custom_command(), and echoes each command before executing it.
# For example, it would transform this:
#
# be_add_custom_command (TARGET mySuperLib
#   POST_BUILD
#   COMMAND python backup.py
#   COMMAND format c:
# )
#
# into something like this:
#
# add_custom_command (TARGET mySuperLib
#   POST_BUILD
#   COMMAND echo python backup.py
#   COMMAND python backup.py
#   COMMAND echo format c:
#   COMMAND format c:
# )
function (be_add_custom_command)
	escape_args()
	be_add_custom_step("command" ${escapedArgs})
endfunction ()


# Takes the same arguments as built-in command add_custom_target(), and echoes each command before executing it.
# For example, it would transform this:
#
# be_add_custom_target (MySuperTarget
#   COMMAND python backup.py
#   COMMAND format c:
#   DEPENDS backup.py
# )
#
# into something like this:
#
# add_custom_command (MySuperTarget
#   COMMAND echo python backup.py
#   COMMAND python backup.py
#   COMMAND echo format c:
#   COMMAND format c:
#   DEPENDS backup.py
# )
function (be_add_custom_target)
	escape_args()
	be_add_custom_step("target" ${escapedArgs})
endfunction ()
