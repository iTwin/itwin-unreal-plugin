
function (be_add_feature_option feature_name feature_desc ns_name default_on)
	# The cmake option we add should be uppercase, as all others
	STRING ( TOUPPER "${feature_name}" cmake_feature_name )
	advanced_option ( ${cmake_feature_name} "${feature_desc}" ${default_on} )

	set ( BE_FEATURE_NAMESPACE_NAME ${ns_name} )
	string ( REPLACE "_" "" BE_FEATURE_NAME ${feature_name} )
	set ( BE_FEATURE_DESC ${feature_desc} ) # this will appear in the header
	STRING ( TOUPPER "${feature_name}" BE_FEATURE_NAME_UPPER )

	# Mimic the behavior of #cmakedefine01
	# (I did not find an easy way to use #cmakedefine01 here...)
	if ( ${cmake_feature_name} )
		set ( BE_FEATURE_AS_0_OR_1 1 )
	else ()
		set ( BE_FEATURE_AS_0_OR_1 0 )
	endif ()

	if ( "${feature_name}" STREQUAL "${BE_FEATURE_NAME_UPPER}" )
		# The feature name is already 100% upper (TPF_PCE for instance)
		# => keep the same basename for the generated header
		set ( feature_header_name "${feature_name}" )
	else ()
		string ( REPLACE "_" "" feature_header_name ${feature_name} )
	endif ()
	set (dstFolder "${CMAKE_SOURCE_DIR}/Public/BeHeaders/BuildConfig" )
	configure_file( ${CMAKE_SOURCE_DIR}/Public/Cmake/be_feature_option.h.template ${dstFolder}/${feature_header_name}.h )

	# Append BuildConfig folder to the list of extra folders for symlink in ThirdParty directory
	# (only doing it once is sufficient)
	get_property (extraFolders GLOBAL PROPERTY beExtraFoldersToSymlink)
	if (NOT ${dstFolder} IN_LIST extraFolders)
		list (APPEND extraFolders ${dstFolder})
		set_property (GLOBAL PROPERTY beExtraFoldersToSymlink ${extraFolders})
	endif ()
endfunction ()
