function(set_rsync_excluded_config _var_stem _config)
	if (BE_RSYNC_UE_EXCLUDE_${_config})
		set(${_var_stem}${_config} "--exclude=${_config}" PARENT_SCOPE)
	else()
		set(${_var_stem}${_config} "" PARENT_SCOPE)
	endif()
endfunction()

check_ue_build_ver("${UNREAL_ENGINE_ROOT}" ueBuildVerFound "")
if (ueBuildVerFound STREQUAL BE_CURRENT_UE_VERSION_BASED_ON)
	message(FATAL_ERROR "Reached rsync_be_unreal when using an official build?! Have you built from source?")
elseif (ueBuildVerFound STREQUAL BE_CURRENT_BE_UE_VERSION AND NOT BE_RSYNC_FORCE_NO_VERSION_CHECK)
	# Nothing to do
else()
	set(beUeBuildOnNAS "rsync://eon@10.229.32.45:/Exchange/BeUE/")
	# Note: Exclusions NOT working for the moment - see options.cmake
	if (BE_RSYNC_UE_EXCLUDE_Debug AND BE_RSYNC_UE_EXCLUDE_Development AND BE_RSYNC_UE_EXCLUDE_Shipping)
		message(FATAL_ERROR "Excluding all three build configurations from the UE build rsync command: probably not a good idea?")
	endif()
	set_rsync_excluded_config(beUeRsyncExclude Development)
	set_rsync_excluded_config(beUeRsyncExclude Shipping)
	set_rsync_excluded_config(beUeRsyncExclude Debug)
	# When publishing, we don't want to redistribute cwrsync, so it's inside Private.
	# TODO: we'll probably need to sync as a preliminary step in the pipeline
	if (NOT EXISTS "${RSYNC_COMMAND}")
		message(FATAL_ERROR "Cannot sync the Unreal Engine built from source when publishing")
	endif()
	file(MAKE_DIRECTORY "${UNREAL_ENGINE_ROOT}")
	set(BE_RSYNC_OPTIONAL_DELETE)
	if (BE_RSYNC_DELETE_LOCAL)
		set(BE_RSYNC_OPTIONAL_DELETE "--delete")
	endif()
	message("About to sync with the following command (need to 'cd' to the destination directory to avoid colon in the destination path, which cwrsync would confuse for a remote path):\ncd \"${UNREAL_ENGINE_ROOT}\" && \"${RSYNC_COMMAND}\" --progress -z -rltD --chmod=777 --port=42 --password-file=${BE_RSYNC_CREDENTIALS_FILE} ${BE_RSYNC_OPTIONAL_DELETE} --exclude=Engine/Build/InstalledBuild.txt ${beUeRsyncExcludeShipping} ${beUeRsyncExcludeDevelopment} ${beUeRsyncExcludeDebug} \"${beUeBuildOnNAS}\" .\nTo later sync another build configuration in the background without relaunching CMake, just use the same, removing the exclusion parameter(s) as needed.")
	message("!! This can take several hours, depending on connectivity (much less if a subset of the files is already on your disk) !!")
	# DO NOT put double quotes around ${BE_RSYNC_CREDENTIALS_FILE} below, it made cwrsync give off an error "rsync: [Receiver] could not open password file"!
	# Engine/Build/InstalledBuild.txt is excluded in order to be updated last.
	# "chmod=777" does not actually set 777 on Windows (TODO Mac) but works around a bug preventing writing Build/InstalledBuild.txt after the rsync :/
	# Formerly using "--archive" <=> "-rlptgoD" but got "chgrp" errors which just triggered the COMMAND_ERROR_IS_FATAL even though the sync was OK => removed "-pgo", keeping "-rltD".
	# Formerly using "--delete" but it would delete builds products: I could "--exclude" them, which would not delete them (there is "--delete-excluded" for that),
	# but if I exclude "Intermediate/*" the natvis files will not be sync'd :-/
	# Maybe I could --exclude all Intermediate (and Binaries/Win64) and make a 2nd call specifically for natvis?
	execute_process(
		COMMAND "${RSYNC_COMMAND}" --progress -z -rltD --chmod=777 --port=42 --password-file=${BE_RSYNC_CREDENTIALS_FILE}
				${BE_RSYNC_OPTIONAL_DELETE} --exclude=.vs --exclude=.git --exclude=Engine/Build/InstalledBuild.txt
				${beUeRsyncExcludeShipping} ${beUeRsyncExcludeDevelopment} ${beUeRsyncExcludeDebug}
				"${beUeBuildOnNAS}" .
		WORKING_DIRECTORY "${UNREAL_ENGINE_ROOT}"
		COMMAND_ERROR_IS_FATAL ANY
	)
	file(WRITE "${BE_UNREAL_ENGINE_DIR}/Build/InstalledBuild.txt" ${BE_CURRENT_BE_UE_VERSION})
	set(BE_RSYNC_DELETE_LOCAL OFF CACHE BOOL FORCE "") # reset flag in case it was set!
	check_ue_build_ver("${UNREAL_ENGINE_ROOT}" ueBuildVerFound FATAL_ERROR)
	# Note: if the folder has already been registered, the build hash it is associated with (BE_CURRENT_BE_UE_VERSION)
	# is NOT updated by calling this again :-(
	execute_process(			# TODO Mac
		COMMAND "Engine/Binaries/Win64/UnrealVersionSelector${CMAKE_HOST_EXECUTABLE_SUFFIX}" -register -unattended
		WORKING_DIRECTORY "${UNREAL_ENGINE_ROOT}"
		COMMAND_ERROR_IS_FATAL ANY
	)
endif()
