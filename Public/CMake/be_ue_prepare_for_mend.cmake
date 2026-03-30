if (NOT EXISTS "${RSYNC_COMMAND}")
	message(FATAL_ERROR "Should have access to cwrsync/rsync at this point :-/")
endif()
message("Preparing filtered UE ThirdParty folder for Mend scan (using rsync) in ${UNREAL_ENGINE_ROOT}/../_UEDepsFilteredCopyForMend:")
file(MAKE_DIRECTORY "${UNREAL_ENGINE_ROOT}/../_UEDepsFilteredCopyForMend")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/TPSAudit")
# Note: this only works with BE_IS_USING_BENTLEY_UNREAL!
execute_process(COMMAND RunUAT.bat ListThirdPartySoftware
		"-target=CrashReportClient Shipping Win64" "-target=iTwinEngage Shipping Win64"
		"-project=${CMAKE_SOURCE_DIR}/Private/UnrealProjects/Carrot/iTwinEngage.uproject"
	WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles"
	OUTPUT_FILE "${CMAKE_BINARY_DIR}/TPSAudit/TPSAudit.log"
	COMMAND_ERROR_IS_FATAL ANY
)
# The regex excludes verbose RunUAT output lines as well as tps "redirects" which are redundant
file(STRINGS "${CMAKE_BINARY_DIR}/TPSAudit/TPSAudit.log" _tpsAudit REGEX ".*\.tps$")
set(thirdPartyPathToScan)
foreach(_tps ${_tpsAudit})
	if (_tps MATCHES Vulkan_SDK_Linux OR _tps MATCHES GoogleTest
		OR _tps MATCHES Engine\\\\Content\\\\ OR _tps MATCHES Engine\\\\Shaders\\\\
	)
		continue()
	endif()
	# The <Location> tag contains a space-separated(!) list of files or folders where the thirdparty code is
	file(READ "${_tps}" _tpsData)
	string(REGEX REPLACE ".*<Location>" "" _tpsTmp "${_tpsData}")
	if (NOT _tpsTmp OR _tpsTmp STREQUAL _tpsData)
		message(FATAL_ERROR "<Location> tag malformed or not found in ${_tps}")
	else()
		string(REGEX REPLACE "</Location>.*" "" _tpsData "${_tpsTmp}")
		if (NOT _tpsData OR _tpsTmp STREQUAL _tpsData)
			message(FATAL_ERROR "</Location> tag malformed or not found in ${_tps}")
		else()
			string(REGEX REPLACE "\n" ";" _tpsLocations "${_tpsData}")
			if (NOT _tpsLocations)
				message(FATAL_ERROR "<Location> tag is empty in ${_tps}")
			endif()
			set(_hasLocation 0)
			foreach(_tpsLoc ${_tpsLocations})
				string(STRIP "${_tpsLoc}" _tpsLoc)
				string(REPLACE "\\" "/" _tpsLoc "${_tpsLoc}")
				if (NOT _tpsLoc)
					continue()
				endif()
				if (NOT _tpsLoc STREQUAL NONE_AS_SUCH AND NOT _tpsLoc MATCHES "Engine/Source" AND NOT _tpsLoc MATCHES "Engine/Plugins")
					message(FATAL_ERROR "Thirdparty location '${_tpsLoc}' should be in Engine/Source or Engine/Plugins (from ${_tps})")
				endif()
				# Remove all sorts of fancy prefixes to the location folder (UE4/Main, UE5/Main, Fortnite/Main...).
				# Not just ".*Engine/" because of Engine/Source/Runtime/Engine/Private/Animation/AnimPhysicsSolver.cpp
				string(REGEX REPLACE ".*Engine/Source/" "Source/" _tpsLoc "${_tpsLoc}")
				string(REGEX REPLACE ".*Engine/Plugins/" "Plugins/" _tpsLoc "${_tpsLoc}")
				if (_tpsLoc STREQUAL NONE_AS_SUCH)
					set(_hasLocation 1)
				elseif (EXISTS "${BE_UNREAL_ENGINE_DIR}/${_tpsLoc}")
					set(_hasLocation 1)
					if (NOT _tpsLoc IN_LIST thirdPartyPathToScan)
						list(APPEND thirdPartyPathToScan "${_tpsLoc}")
					endif()
				else()
					message(FATAL_ERROR "Invalid path to '${BE_UNREAL_ENGINE_DIR}/${_tpsLoc}' from ${_tps}")
				endif()
			endforeach()
			if (NOT _hasLocation)
				message(FATAL_ERROR "No valid location found in ${_tps}")
			endif()
		endif()
	endif()
endforeach()
set(_subdirsToScan "")
foreach(_thirdParty ${thirdPartyPathToScan})
	string(REGEX REPLACE "^/" "" _thirdParty "${_thirdParty}") # should have been removed above already
	string(REGEX REPLACE "/$" "" _thirdParty "${_thirdParty}")
	set(_subdir "${_thirdParty}")
	# Shrink future subdir name...
	string(REPLACE "Source/" "S/" _subdir "${_subdir}")
	string(REPLACE "Plugins/" "S/" _subdir "${_subdir}")
	string(REPLACE "/Public" "/Pub" _subdir "${_subdir}")
	string(REPLACE "/Private" "/Prv" _subdir "${_subdir}")
	string(REPLACE "/Engine" "/Eng" _subdir "${_subdir}")
	string(REPLACE "/Runtime" "/Rt" _subdir "${_subdir}")
	string(REPLACE "/ThirdParty" "/3p" _subdir "${_subdir}")
	string(REPLACE "/GeometryProcessing" "/Gp" _subdir "${_subdir}")
	if (IS_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/${_thirdParty}")
		string(REPLACE "/" "_" _subdir "${_subdir}")
		set(_thirdParty "${_thirdParty}/") # relevant for rsync (avoids one level of dir tree)
	else()
		cmake_path(GET _subdir PARENT_PATH _subdir)
		string(REPLACE "/" "_" _subdir "${_subdir}")
	endif()
	list(APPEND _subdirsToScan "${_subdir}")
	list(APPEND _subdirsToScan "${_thirdParty}")
endforeach()
# Delete existing directories that we no longer need to scan
file(GLOB _existingSubdirs LIST_DIRECTORIES true
	RELATIVE "${BE_UNREAL_ENGINE_DIR}/../../_UEDepsFilteredCopyForMend"
	"${BE_UNREAL_ENGINE_DIR}/../../_UEDepsFilteredCopyForMend/*"
)
foreach(_existing ${_existingSubdirs})
	if (NOT _existing IN_LIST _subdirsToScan)
		message("Deleting ${BE_UNREAL_ENGINE_DIR}/../../_UEDepsFilteredCopyForMend/${_existing} not (anymore) in TPS list...")
		file(REMOVE_RECURSE "${BE_UNREAL_ENGINE_DIR}/../../_UEDepsFilteredCopyForMend/${_existing}")
	endif()
endforeach()
# Now copy the 3rdparties we need, using rsync
set(_subdir "")
foreach(_thirdParty ${_subdirsToScan})
	if (NOT _subdir)
		set(_subdir "${_thirdParty}")
		continue()
	endif()
	execute_process(
		COMMAND "${RSYNC_COMMAND}" --progress -z -rltD --chmod=777 --delete --delete-excluded
			--exclude=Debug --exclude=debug --exclude=Release --exclude=release --exclude=Deploy --exclude=Perforce --exclude=bin --exclude=Bin --exclude=doc --exclude=docs --exclude=Documentation --exclude=examples --exclude=Examples --exclude=EOSSDK --exclude=Steamworks --exclude=ios --exclude=IOS --exclude=TVOS --exclude=tvos --exclude='*.a' --exclude='*.lib' --exclude='*.dylib' --exclude='*.so' --exclude='*.dll' --exclude='*.exe' --exclude='*.zip' --exclude='*.tgz' --exclude='*.tar.gz' --exclude='*.hdr' --exclude='*.obj' --exclude='*.pdf' --exclude='*.jpg' --exclude='*.png' --exclude='*.chm' --exclude='*.glb' --exclude='test*' --exclude='Test*'
			# cwrsync issue: need to use relative paths for local-to-local directory copy :/
			"${_thirdParty}" "../../_UEDepsFilteredCopyForMend/${_subdir}/"
		COMMAND_ECHO STDOUT
		WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}"
		COMMAND_ERROR_IS_FATAL ANY
	)
	set(_subdir "")
endforeach()
set(dummyAddedFile)
createSymlink("${UNREAL_ENGINE_ROOT}/../_UEDepsFilteredCopyForMend" "${CMAKE_SOURCE_DIR}/Private/_UEDepsLinkForMend" dummyAddedFile)