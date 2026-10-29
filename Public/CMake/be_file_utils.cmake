# Wrapper around "file (READ_SYMLINK)" that handles NT path prefixes.
function (readSymlink link result)
	file(READ_SYMLINK "${link}" result_local)
	# Convert "\??\C:\foo\bar" into "C:\foo\bar".
	string (REPLACE "\\??\\" "" result_local "${result_local}")
	set (${result} ${result_local} PARENT_SCOPE)
endfunction ()

if (WIN32)
	# copyTimestampsToSymlink may be called many times, avoid testing presence of the exe many times uselessly
	set (copy_timestamps_to_symlink_src "${CMAKE_SOURCE_DIR}/Public/CMake/copy_timestamps_to_symlink")
	set (copy_timestamps_to_symlink_dir "${copy_timestamps_to_symlink_src}/build")
	set (copy_timestamps_to_symlink_exe "${copy_timestamps_to_symlink_dir}/Release/copy_timestamps_to_symlink.exe")
	if (NOT EXISTS "${copy_timestamps_to_symlink_dir}")
		file(MAKE_DIRECTORY "${copy_timestamps_to_symlink_dir}")
	elseif (EXISTS "${copy_timestamps_to_symlink_exe}" AND
			("${copy_timestamps_to_symlink_src}/main.cpp" IS_NEWER_THAN "${copy_timestamps_to_symlink_exe}"
			OR "${copy_timestamps_to_symlink_src}/CMakeLists.txt" IS_NEWER_THAN "${copy_timestamps_to_symlink_exe}")
		)
		file(REMOVE "${copy_timestamps_to_symlink_exe}")
	endif()
	if (NOT EXISTS "${copy_timestamps_to_symlink_exe}")
		execute_process(
			COMMAND ${CMAKE_COMMAND} -S "${copy_timestamps_to_symlink_src}" -B .
			COMMAND_ECHO STDOUT
			COMMAND_ERROR_IS_FATAL ANY
			WORKING_DIRECTORY "${copy_timestamps_to_symlink_dir}"
		)
		execute_process(
			COMMAND ${CMAKE_COMMAND} --build . --config Release
			COMMAND_ECHO STDOUT
			COMMAND_ERROR_IS_FATAL ANY
			WORKING_DIRECTORY "${copy_timestamps_to_symlink_dir}"
		)
	endif()
	if (NOT EXISTS "${copy_timestamps_to_symlink_exe}")
		message(FATAL_ERROR "Building ${copy_timestamps_to_symlink_exe} failed?!")
	endif()
endif()

function(copyTimestampsToSymlink referenceFile targetSymlink)
	execute_process(
		COMMAND "${copy_timestamps_to_symlink_exe}" "${referenceFile}" "${targetSymlink}"
		COMMAND_ECHO STDOUT
		COMMAND_ERROR_IS_FATAL ANY
	)
endfunction()

# Wrapper around "file (CREATE_LINK)" that does nothing if the link already exists
# and points to the correct target.
function (createSymlink target link addedFilesRef)
	message( "createSymlink ${target} ${link}")
	set (addedFiles_local ${${addedFilesRef}})
	list (APPEND addedFiles_local "${link}")
	set (${addedFilesRef} ${addedFiles_local} PARENT_SCOPE)
	if (IS_SYMLINK "${link}")
		readSymlink ("${link}" oldTarget)
		if ("${oldTarget}" PATH_EQUAL "${target}")
			set (isUpToDate TRUE)
			# For a file, we also need to check that the target was not modified after the link is created.
			# If the target has been modified after the link was created, we must re-create the link
			# so that tools that check the modification date (eg. UBT) will correctly see that the file
			# (ie. the link) has changed and thus properly rebuild what depends on it.
			# CMake provided at least 2 methods to get or compare modification date:
			# 1: if(<file1> IS_NEWER_THAN <file2>)
			# 2: file(TIMESTAMP ...)
			# Problem: we want to make sure that symlinks are not "resolved" when querying the modification date,
			# but the doc does not mention if symlinks are resolved or not.
			# It appears that, with CMake 3.28 on Windows, method #1 does not follow symlinks,
			# but method #2 does. It may change with other version or platform.
			# So to ensure reliable behavior, we use a python script.
			# Note that we now test strict equality of the timestamps: see the call to 'utime' at the end of this function
			if (EXISTS "${target}" AND NOT IS_DIRECTORY "${target}")
				execute_process (
					COMMAND "${Python3_EXECUTABLE}" -c "import os,sys; sys.stdout.write(str(os.lstat('${link}').st_mtime == os.lstat('${target}').st_mtime))"
					OUTPUT_VARIABLE isUpToDate
					COMMAND_ERROR_IS_FATAL ANY
				)
			endif ()
			if (isUpToDate)
				return ()
			endif ()
		endif ()
	elseif (EXISTS "${link}")
		message (SEND_ERROR "Cannot create symlink \"${link}\", because there is already a real file/directory at this path. Overwriting it could result in data loss.")
	endif ()
	get_filename_component (linkParent "${link}" DIRECTORY)
	# Fixup case when the parent folder of the link already exists as a symlink itself.
	# This may happen eg. if the previous version of the script would symlink the parent folder directly,
	# For example for unreal "Config" folder which used to be a symlink,
	# but is now a regular folder (into which we may add symlinks).
	# In this case, we need to remove the parent before creating the directory.
	if (IS_SYMLINK "${linkParent}")
		file (REMOVE "${linkParent}")
	endif ()
	file (MAKE_DIRECTORY "${linkParent}")
	file (CREATE_LINK "${target}" "${link}" SYMBOLIC)
	# Copy the target's timestamp onto the link otherwise UBT will error out (got this when upgrading to UE 5.7).
	# Symptom: packaging the app loops on "Failed to copy" errors because of differing timestamps then unhelpfully
	# proceed to "deleting, waiting 10s and retrying" the copy with obviously the same result (10 times per file...).
	if (NOT IS_DIRECTORY "${target}")
		if (APPLE)
			execute_process (
				COMMAND "${Python3_EXECUTABLE}" -c "import os; os.utime('${link}', (os.path.getmtime('${target}'), os.path.getctime('${target}')), follow_symlinks=False)"
				COMMAND_ERROR_IS_FATAL ANY
			)
		else()
			# Alas changing a symlink's timestamp does not work, neither with 'utime' nor with 'touch' in a bash: it modifies the link's target.
			# Let's use a custom utility built for this purpose...
			copyTimestampsToSymlink("${target}" "${link}")
		endif()
	endif()
endfunction ()

# Wrapper around "file (WRITE)" that does nothing if the file is already up to date.
function (writeFile filePath content)
	if (EXISTS "${filePath}")
		file (READ "${filePath}" oldContent)
		if ("${oldContent}" STREQUAL "${content}")
			return ()
		endif ()
	endif ()
	file (WRITE "${filePath}" "${content}")
endfunction ()

# Creates symlinks for every item contained in target dir.
# This is non-recursive.
# So if we have this structure:
# <target>
# |- foo
# |  |- bar
# |- qux
# Then the function will create this:
# <link>
# |- foo -> points to <target>/foo
# |- qux -> points to <target>/qux
function (createSymlinksForDirectoryContent target link addedFilesRef)
	set (addedFiles_local ${${addedFilesRef}})
	file (GLOB items RELATIVE "${target}" "${target}/*")
	foreach (item ${items})
		createSymlink ("${target}/${item}" "${link}/${item}" addedFiles_local)
	endforeach ()
	set (${addedFilesRef} ${addedFiles_local} PARENT_SCOPE)
endfunction ()

# Same as built-in "file (RELATIVE_PATH)" but verifies the relative path is actually
# "inside" the given directory (eg. it does not start with "..").
# Returns the empty string if the relative path is not inside the directory.
function (getRelativePathChecked directory item result)
	set (result_local "")
	file (RELATIVE_PATH relPath ${directory} ${item})
	if (NOT "${relPath}" MATCHES "^\\.\\..*")
		set (result_local ${relPath})
	endif ()
	set (${result} ${result_local} PARENT_SCOPE)
endfunction ()
