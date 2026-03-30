if (BE_REBUILD_ALL_VCPKG_FOR_MEND)
	cmake_path(GET BE_REBUILD_ALL_VCPKG_FOR_MEND PARENT_PATH _outParent)
	if (NOT EXISTS "${CMAKE_SOURCE_DIR}/${_outParent}")
		message(FATAL_ERROR "Parent path for the intended vcpkg buildtrees filtered copy does not exist, this is fishy! Tested '${CMAKE_SOURCE_DIR}/${_outParent}', obtained from the CMake parameter passed: '${BE_REBUILD_ALL_VCPKG_FOR_MEND}'")
	endif()
	if (NOT EXISTS "${RSYNC_COMMAND}")
		message(FATAL_ERROR "Should have access to cwrsync/rsync at this point :-/")
	endif()
	file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/${BE_REBUILD_ALL_VCPKG_FOR_MEND}")
	# cwrsync needs relative paths...
	cmake_path(RELATIVE_PATH "${CMAKE_SOURCE_DIR}/${BE_REBUILD_ALL_VCPKG_FOR_MEND}" BASE_DIRECTORY "${VCPKG_ROOT}/buildtrees" OUTPUT_VARIABLE _relPath)
	message("Will do a filtered copy of vcpkg buildtrees to \"${VCPKG_ROOT}/buildtrees/${_relPath}\" ")
	execute_process(
		COMMAND "${RSYNC_COMMAND}" --progress -z -rltD --chmod=777 --delete --delete-excluded
			# Include only 'src' folders, but remove a lot of useless and bulky stuff from them
			--exclude=Debug --exclude=debug --exclude=Release --exclude=release --exclude=Deploy --exclude=Perforce --exclude=bin --exclude=Bin --exclude=examples --exclude=Examples --exclude=ios --exclude=IOS --exclude=TVOS --exclude=tvos --exclude='*.srcjar' --exclude='*.a' --exclude='*.log' --exclude='*.lib' --exclude='*.dylib' --exclude='*.so' --exclude='*.pdb' --exclude='*.dll' --exclude='*.exe' --exclude='*.hdr' --exclude='*.obj' --exclude='*.jpg' --exclude='*.png' --exclude='*.chm' --exclude='*.glb' --exclude='*.bz' --exclude='*.pdf' --exclude='*.bin' --exclude='*.wasm' --exclude='*.html' --exclude='*.basis' --exclude='*.ico' --exclude=configure --exclude='ChangeLog*' --exclude="vcpkg-*" --exclude='CHANGELOG*' --exclude='README*' --exclude='CONTRIB*' --exclude='LICENSE*' --exclude='SECURITY*' --exclude='.*' --exclude='test*/' --exclude='Test*/' --exclude='doc*/' --exclude='Doc*/'
			--include="/" --include="/*/" --include="/*/src/***" --exclude="*"
			"./" "${_relPath}/"
		COMMAND_ECHO STDOUT
		WORKING_DIRECTORY "${VCPKG_ROOT}/buildtrees"
		COMMAND_ERROR_IS_FATAL ANY
	)
	# Delete subdirs for these ports that are actually header-only (which means their files relevant
	# to Mend are already scanned from the vcpkg_installed subdirs), so that there is no need to manually update
	# them on the ADO agents when they are upgraded...
	# Note: I should modify the above rsync command but the whole include/exclude listing and ordering is already acrobatic so maybe later...
	# (this is rarely used anyway)
	set(headerOnlyLibs ctre cpp-httplib earcut-hpp expected-lite libmorton magic-enum node-addon-api node-api-headers picosha2 stb stduuid)
	foreach(_lib ${headerOnlyLibs})
		file(REMOVE_RECURSE "${VCPKG_ROOT}/buildtrees/${_relPath}/${headerOnlyLibs}")
	endforeach()
endif()
set(dummyAddedFile)
createSymlink("${CMAKE_SOURCE_DIR}/${BE_VCPKG_FILTERED_BUILDTREES_RELPATH}" "${CMAKE_SOURCE_DIR}/Private/_Vcpkg3rdPartySrcForMend" dummyAddedFile)
