# Helper macros to safely define options (booleans as well as CACHE'd strings or filepath) with
# default values.
# 
# See http://eonmatrix/intranet/git/wiki/Cmake#How_to_correctly_set_options

# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
# Macros to define CACHE options taking care not to overwrite an existing default value, if any.
# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #

# Internal helper macro
macro ( be_internal_set_actual_initial_value _optionname _initialvalue _outputvar )
	set ( ${_outputvar} "${_initialvalue}" )
	# If set, use the default value instead of the initial value provided
	if ( NOT "${__${_optionname}_DEFAULT_VALUE__}" STREQUAL "" )
		set ( ${_outputvar} "${__${_optionname}_DEFAULT_VALUE__}" )
	endif ()
endmacro ( be_internal_set_actual_initial_value )

macro ( option_string _optionname _optiondescription _initialvalue )
	be_internal_set_actual_initial_value ( ${_optionname} "${_initialvalue}" myOptVal )
	set ( ${_optionname} "${myOptVal}" CACHE STRING "${_optiondescription}" )
endmacro ( option_string )

macro ( option_filepath _optionname _optiondescription _initialvalue )
	be_internal_set_actual_initial_value ( ${_optionname} "${_initialvalue}" myOptVal )
	set ( ${_optionname} "${myOptVal}" CACHE FILEPATH "${_optiondescription}" )
	# We expect the filepath stored in the variable to be in "cmake-style" (forward slashes).
	# The cmake-gui UI generally does a good job of automatically converting the user-supplied path
	# (which can contain back-slashes) into cmake-style.
	# But in some cases, it does not perform the conversion (eg. paste a text in the field and
	# immediately press enter).
	# This can cause problems because some of our build tools (eg. typescript compiler) do not
	# handle back-slashes correctly.
	# So, instead of "manually" making ad-hoc fixes at various places, we fix them all here.
	file (TO_CMAKE_PATH "${${_optionname}}" fixedValue)
	set (${_optionname} "${fixedValue}" CACHE FILEPATH "${_optiondescription}" FORCE)
endmacro ( option_filepath )

macro ( option_path _optionname _optiondescription _initialvalue )
	be_internal_set_actual_initial_value ( ${_optionname} "${_initialvalue}" myOptVal )
	set ( ${_optionname} "${myOptVal}" CACHE PATH "${_optiondescription}" )
	# We expect the filepath stored in the variable to be in "cmake-style" (forward slashes).
	# The cmake-gui UI generally does a good job of automatically converting the user-supplied path
	# (which can contain back-slashes) into cmake-style.
	# But in some cases, it does not perform the conversion (eg. paste a text in the field and
	# immediately press enter).
	# This can cause problems because some of our build tools (eg. typescript compiler) do not
	# handle back-slashes correctly.
	# So, instead of "manually" making ad-hoc fixes at various places, we fix them all here.
	file (TO_CMAKE_PATH "${${_optionname}}" fixedValue)
	set (${_optionname} "${fixedValue}" CACHE PATH "${_optiondescription}" FORCE)
endmacro ( option_path )

# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
# Helper macro to define a default value to a boolean/string/filepath options. If the option already
# has a different default, the call fails.
# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #

# Internal helper macro
macro ( be_internal_option_default_protect _optionname _defaultval _optiontypestringinfo )
	if ( "${_defaultval}" STREQUAL "" )
		message ( FATAL_ERROR "DO NOT use an empty default value (for option '${_optionname}') "
			"as it is undistinguishable from an undefined value and can be overwritten in "
			"subsequent calls to option_*** or option_***_default!" )
	endif ()
	if ( NOT "${__${_optionname}_DEFAULT_VALUE__}" STREQUAL "" )
		if ( NOT "${_defaultval}" STREQUAL "${__${_optionname}_DEFAULT_VALUE__}" )
			message ( FATAL_ERROR
			"Multiple default values for option '${_optionname}' (type '${_optiontypestringinfo}')" )
		endif ()
	endif ()
	set ( __${_optionname}_DEFAULT_VALUE__ ${_defaultval} )
endmacro ( be_internal_option_default_protect )

macro ( basic_option_default _optionname _defaultval )
	be_internal_option_default_protect ( ${_optionname} ${_defaultval} "boolean" )
	option ( ${_optionname} "" ${_defaultval} )
endmacro ( basic_option_default )

macro ( option_default _optionname _defaultval )
	basic_option_default ( ${_optionname} ${_defaultval} )
	mark_as_advanced ( FORCE ${_optionname} )
endmacro ( option_default )

macro ( option_string_default _optionname _defaultval )
	be_internal_option_default_protect ( ${_optionname} "${_defaultval}" "string" )
	option_string ( ${_optionname} "" "${_defaultval}" )
endmacro ( option_string_default )

macro ( option_filepath_default _optionname _defaultval )
	be_internal_option_default_protect ( ${_optionname} "${_defaultval}" "filepath" )
	option_filepath ( ${_optionname} "" "${_defaultval}" )
endmacro ( option_filepath_default )

macro ( option_path_default _optionname _defaultval )
	be_internal_option_default_protect ( ${_optionname} "${_defaultval}" "filepath" )
	option_path ( ${_optionname} "" "${_defaultval}" )
endmacro ( option_path_default )

# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
# Option definition macros defaulting to the "advanced" status
# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #

macro ( advanced_option _optionname _optiondescription _initialvalue )
	# A direct call to "option" is OK here, it will not overwrite the option value if already set by
	# a call to "option_default".
	option ( ${_optionname} "${_optiondescription}" ${_initialvalue} )
	mark_as_advanced ( FORCE ${_optionname} )
endmacro ( advanced_option )

macro ( advanced_option_string _optionname _optiondescription _initialvalue )
	option_string ( ${_optionname} "${_optiondescription}" "${_initialvalue}" )
	mark_as_advanced ( FORCE ${_optionname} )
endmacro ( advanced_option_string )

macro ( advanced_option_filepath _optionname _optiondescription _initialvalue )
	option_filepath ( ${_optionname} "${_optiondescription}" "${_initialvalue}" )
	mark_as_advanced ( FORCE ${_optionname} )
endmacro ( advanced_option_filepath )

macro ( advanced_option_path _optionname _optiondescription _initialvalue )
	option_path ( ${_optionname} "${_optiondescription}" "${_initialvalue}" )
	mark_as_advanced ( FORCE ${_optionname} )
endmacro ( advanced_option_path )

# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
# Force Option Value in CACHE
# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #

macro ( be_force_option_value_cache option_ value_)
	if ( DEFINED "${option_}" AND NOT "${${option_}}" STREQUAL ${value_} )
		message ( ${autocompiler_check} "Forcing ${option_} to ${value_}" )
		set ( ${option_} ${value_} CACHE BOOL "" FORCE )
	else ()
		option_default(${option_} ${value_})
	endif ()
endmacro ( be_force_option_value_cache )