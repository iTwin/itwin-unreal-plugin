# Wrapper around "file (READ_SYMLINK)" that handles NT path prefixes.
function (readSymlink link result)
	file(READ_SYMLINK "${link}" result_local)
	# Convert "\??\C:\foo\bar" into "C:\foo\bar".
	string (REPLACE "\\??\\" "" result_local "${result_local}")
	set (${result} ${result_local} PARENT_SCOPE)
endfunction ()

# Wrapper around "file (CREATE_LINK)" that does nothing if the link already exists
# and points to the corect target.
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
			if (EXISTS "${target}" AND NOT IS_DIRECTORY "${target}")
				execute_process (
					COMMAND "${Python3_EXECUTABLE}" -c "import os,sys; sys.stdout.write(str(os.lstat('${link}').st_mtime >= os.lstat('${target}').st_mtime))"
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

# Returns the name of the vcpkg package coresponding to the given dependency,
# or the empty string if this is not a vcpkg package.
# Recognizes regular package name (eg. "catch2") as well as CMake targets
# created by the package (eg. "Catch2::Catch2WithMain").
function (getVcpkgPackageName dependency result)
	set (result_local "")
	# If the given dependency is a target created by a package, there is no proper way to
	# reliably retrieve the package name.
	# So what we do here is take the lowercase string to the left of "::".
	# This transforms "Catch2::Catch2WithMain" into "catch2".
	string (REGEX REPLACE "::.*" "" package ${dependency})
	string (TOLOWER ${package} package)
	if (${package} IN_LIST BE_VCPKG_PACKAGES)
		set (result_local ${package})
	endif ()
	set (${result} ${result_local} PARENT_SCOPE)
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

# Creates a custom target which will package the plugin
function (be_create_plugin_packager_target pluginName pluginDir)
	set ( packagerTargetName "${pluginName}_PluginPackager" )
	# Since the same plugin can be used in several projects (thanks to the "external source" feature),
	# we make sure to create only a single "packager" target for each plugin.
	if (TARGET ${packagerTargetName})
		return ()
	endif ()
	set ( pluginPackageSrcDir "${CMAKE_BINARY_DIR}/Packaging_Input/${pluginName}" )
	set ( pluginPackageDstDir "${CMAKE_BINARY_DIR}/Packaging_Output/${pluginName}" )
	if (APPLE)
		set ( RunUATBasename "RunUAT.sh" )
		set ( TargetPlatforms "Mac" ) ## TODO_JDE
	elseif (CMAKE_HOST_UNIX)
		set ( RunUATBasename "RunUAT.sh" )
		set ( TargetPlatforms "Linux" ) ## TODO_JDE
	else ()
		set ( RunUATBasename "RunUAT.bat" )
		set ( TargetPlatforms "Win64" )
	endif ()
	add_custom_target ( ${packagerTargetName} ALL )
	# First duplicate the plugin folder completely: this is done to eliminate any
	# symbolic link, which RunUAT does not like at all...
	# Then run RunUAT for the current platform.
	add_custom_command ( TARGET ${packagerTargetName}
		WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Input"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Output"
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}"
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageDstDir}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${pluginDir}" "${pluginPackageSrcDir}"
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}/Binaries"
		COMMAND ./${RunUATBasename} BuildPlugin -Plugin=${pluginPackageSrcDir}/${pluginName}.uplugin -Package="${pluginPackageDstDir}" -CreateSubFolder -TargetPlatforms=${TargetPlatforms}
	)
	set_target_properties (${packagerTargetName} PROPERTIES FOLDER "UnrealProjects/Packaging")
endfunction ()

# Registers an Unreal project.
# This function will setup the build folder for this Unreal project,
# create necessary symlinks and adding the CMake targets.
# Examples:
# - be_add_unreal_project (Foo/Bar NO_TEST)
#   Declares an unreal project with no plugin, no dependency to external lib and no test.
#   There must be an existing file Foo/Bar/Bar.uproject (path is relative to the CMakeLists.txt calling this function).
# - be_add_unreal_project (Foo/Bar
#       MAIN_DEPENDS
#           MyLib1 # The app uses external lib MyLib1,
#           MyLib2 # and also on MyLib2
#       PLUGINS
#           PLUGIN MyPlugin1
#               PLUGIN_DEPENDS
#                   MyLib1 # Plugin MyPlugin1 depends on lib MyLib1
#           PLUGIN MyPlugin2
#               PLUGIN_SOURCE "Public/UnrealProjects/ITwinTestApp" # Plugin references an external source dir located in "Public/UnrealProjects/ITwinTestApp/Plugins/MyPlugin2"
#               PLUGIN_DEPENDS
#                   MyLib1
#                   MyLib3
#   )
#   Declares an unreal project containing plugins and tests.
#   The main app and the plugins have dependencies to external libs.
#   Tests must be named "Bentley.XXX".
function (be_add_unreal_project projectDir)
	function (setupDirs pluginName projectDependenciesRef addedFilesRef extraTestsRef)
		set (projectDependencies_local ${${projectDependenciesRef}})
		set (addedFiles_local ${${addedFilesRef}})
		set (extraTests_local ${${extraTestsRef}})
		cmake_parse_arguments (
			funcArgs
			""
			"SOURCE"
			"DEPENDS"
			${ARGN}
		)
		if ("${pluginName}" STREQUAL "")
			set (srcDirRel "")
			set (uniqueName ${projectName})
		else ()
			set (srcDirRel "/Plugins/${pluginName}")
			set (uniqueName "${projectName}_${pluginName}")
		endif ()
		# For each dependency, we have to create, inside the Unreal app/plugin folder,
		# symlinks to the header and lib files of the dependency.
		# Those symlinks will be located in Source/ThirdParty folder.
		# There is a big problem with symlinks (at least on Windows):
		# The "modified date" of the symlink itself never changes (it is always the creation date of the symlink itself).
		# And, when UBT looks for file changes to decide whether to rebuild something,
		# it looks at the modified date of the symlink, not the target.
		# As a result, unreal projects are not correctly rebuilt when an external header or lib has changed.
		# To fix this issue, we create a python script that is always run before building an unreal project.
		# This script will (re-)create the symlinks if the target has changed after the link was created.
		# Variable "setupExternFilesCommands" will contain the python script mentioned above.
		set (setupExternFilesCommands "")
		# The script above is eventually written to disk.
		# Problem: it may contain cmake generator expressions, which must be evaluated to get the real file name.
		# So, we create a list of "defines" that are passed in the command-line (and thus evaluated),
		# and used in the script to make the replacements.
		set (setupExternFilesDefines "Config:=$<CONFIG>")
		set (setupExternFilesDependencies "")
		set (realSrcAbsDir "${projectAbsDir}${srcDirRel}")
		if (DEFINED funcArgs_SOURCE)
			set (realSrcAbsDir "${CMAKE_SOURCE_DIR}/${funcArgs_SOURCE}${srcDirRel}")
			# Plugin uses an external source dir, so we must create symlinks to sources, content, shaders, etc.
			# But must make sure to only create symlinks to real files/folders, not to items that are symlinks themselves!
			# Also, these links must be created at cmake time (not build time), so that the VS project generated by UBT
			# (at cmake time) are correct.
			# First, create symlinks to all real (not symlinks) sub-dirs of "Source", except "ThirdParty" (which only contains symlinks).
			file (GLOB sourceItems RELATIVE "${CMAKE_SOURCE_DIR}/${funcArgs_SOURCE}${srcDirRel}/Source" "${CMAKE_SOURCE_DIR}/${funcArgs_SOURCE}${srcDirRel}/Source/*")
			foreach (sourceItem ${sourceItems})
				if ("${sourceItem}" STREQUAL "ThirdParty" OR IS_SYMLINK "${CMAKE_SOURCE_DIR}/${funcArgs_SOURCE}${srcDirRel}/Source/${sourceItem}")
					continue ()
				endif ()
				createSymlink ("${CMAKE_SOURCE_DIR}/${funcArgs_SOURCE}${srcDirRel}/Source/${sourceItem}" "${projectAbsDir}${srcDirRel}/Source/${sourceItem}" addedFiles_local)
			endforeach ()
			# Now create symlinks to other files/folders (content, shaders etc).
			# This is difficult to implement a reliable generic process here, so for now we only handle ITwinForUnreal.
			# To be generic, one possible solution would be to walk the external dir and for each item (file, folder),
			# ask git for whether the item is ignored. Only non-ignored items should be symlinked.
			if ("${pluginName}" STREQUAL ITwinForUnreal)
				foreach (item
					"Config/BaseITwinForUnreal.ini"
					"Content/ITwin"
					"Resources"
					"Shaders/ITwin"
					"${pluginName}.uplugin"
				)
					createSymlink ("${CMAKE_SOURCE_DIR}/${funcArgs_SOURCE}${srcDirRel}/${item}" "${projectAbsDir}${srcDirRel}/${item}" addedFiles_local)
				endforeach ()
			else ()
				message (SEND_ERROR "External source for plugin ${pluginName} is not supported (TODO).")
			endif ()
		endif ()
		get_property (cesiumDependenciesBinDir DIRECTORY "${CMAKE_SOURCE_DIR}/Public/CesiumDependencies" PROPERTY BINARY_DIR)
		if (DEFINED funcArgs_DEPENDS)
			file (MAKE_DIRECTORY "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Include")
			# Verify each direct dependency is either an existing target or a vcpkg package.
			foreach (dependency ${funcArgs_DEPENDS})
				if (TARGET ${dependency})
					list (APPEND projectDependencies_local ${dependency})
					continue ()
				endif ()
				getVcpkgPackageName (${dependency} packageName)
				if (packageName)
					continue ()
				endif ()
				message (SEND_ERROR "Unreal project/plugin dependency ${dependency} is neither an existing target nor a vcpk package.")
			endforeach ()
			# Recursively retrieve all dependencies.
			set (allDependencyTargets "")
			set (allDependencyPackages "")
			set (dependenciesToProcess ${funcArgs_DEPENDS})
			while (dependenciesToProcess)
				set (nextDependenciesToProcess "")
				foreach (dependency ${dependenciesToProcess})
					# Skip dependency if it is a target already encountered.
					if (${dependency} IN_LIST allDependencyTargets)
						continue ()
					endif ()
					if (TARGET ${dependency})
						# If this dependency is from Cesium, we can skip it, as this is already handled later in this script.
						get_property (dependencySourceDir TARGET ${dependency} PROPERTY SOURCE_DIR)
						cmake_path (IS_PREFIX cesiumDependenciesBinDir ${dependencySourceDir} isCesiumDependency)
						if (isCesiumDependency)
							continue ()
						endif ()
						# Retrieve the transitive dependencies, which will be processed in the next loop.
						list (APPEND allDependencyTargets ${dependency})
						get_property (dependencyDependencies TARGET ${dependency} PROPERTY LINK_LIBRARIES)
						list (APPEND nextDependenciesToProcess ${dependencyDependencies})
						continue ()
					endif ()
					# The dependency is not a target, check if this is a vcpkg package.
					getVcpkgPackageName (${dependency} packageName)
					if (packageName)
						# Skip package if it has already been encountered.
						if (${packageName} IN_LIST allDependencyPackages)
							continue ()
						endif ()
						list (APPEND allDependencyPackages ${packageName})
						# Retrieve the transitive dependencies, which will be processed in the next loop.
						# Note: some packages (eg. "curl") also exist as regulat CMake targets coming from Cesium.
						# So what we do is rename such package to "curl::_dummy" so that in the next loop
						# it is correctly recognized as a package (see getVcpkgPackageName()).
						foreach (dep ${BE_VCPKG_DEPENDENCIES_${packageName}})
							list (APPEND nextDependenciesToProcess "${dep}::_dummy")
						endforeach ()
					endif ()
				endforeach ()
				set (dependenciesToProcess ${nextDependenciesToProcess})
			endwhile ()
			foreach (dependency ${allDependencyTargets})
				# Create symlinks to the header files of the dependency, except for those in Extern/ (boost...) for which a link to the folder is enough
				set (isDependencyExtern FALSE)
				get_property (depSourceDir TARGET ${dependency} PROPERTY SOURCE_DIR)
				get_property (depBinaryDir TARGET ${dependency} PROPERTY BINARY_DIR)
				# Check if the dependency source dir is in the CMake "source" or "binary" directory.
				getRelativePathChecked ("${CMAKE_SOURCE_DIR}" "${depSourceDir}" depSourceDirRel)
				if (depSourceDirRel)
					# We want to retrieve the dependency source dir relative to Public or Private dir.
					# Now depSourceDirRel contains something like "Private/aa/bb".
					# We want to strip the leading "xxx/" part.
					# A "regex replace" command will do the job, but there is a catch:
					# Since "regex replace" replaces all occurences, we will end up with "bb" instead of "aa/bb".
					# So we add a dummy prefix character (':', but could be any character forbidden in paths)
					# to both the path and the regex, so that only a single replacement is done.
					string (REGEX REPLACE "^:[^/]*/" "" depSourceDirRel ":${depSourceDirRel}")
				else ()
					getRelativePathChecked ("${CMAKE_BINARY_DIR}" "${depSourceDir}" depSourceDirRel)
				endif ()
				if (NOT depSourceDirRel)
					message (SEND_ERROR "Cannot compute relative path for dependency \"${dependency}\".")
				endif ()
				if (depSourceDirRel MATCHES "^Extern/")
					string(REPLACE "Extern/" "" depInclDirRel ${depSourceDirRel})
					set (linkPath "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Include/${depInclDirRel}")
					createSymlink ("${depSourceDir}" "${linkPath}" addedFiles_local)
				else()
					# Here we are going to handle different types of targets:
					# - Our "own" regular targets, eg. BeUtils.
					# - External targets added by FetchContent(), eg. reflect-cpp.
					# In all cases, the goal is to retrieve the list of headers and lib files of the target,
					# and decide where to put the symlinks inside the "Source/ThirdParty" folder.
					# Examples:
					# - For BeUtils, when retrieving the list of source files, CMake will likey return relative paths
					#   as this is how they are "declared" in add_library(), eg. "Gltf/GltfBuilder.h".
					#   This file will be symlinked as "Source/ThirdParty/Include/BeUtils/Gltf/GltfBuilder.h".
					# - For reflect-cpp, when retrieving the list of source files, CMake will likey return absolute paths,
					#   eg. "<binaryDir>/_deps/reflect-cpp-src/include/rfl/json.hpp".
					#   This file will be symlinked as "Source/ThirdParty/Include/rfl/json.hpp".
					# For external libraries, we need to retrieve the list of "interface include directories",
					# eg. "<binaryDir>/_deps/reflect-cpp-src/include", so that we can compute the relative path of each header.
					set (depInterfaceIncludeDirs "")
					get_property (depInterfaceIncludeDirsRaw TARGET ${dependency} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
					foreach (dir ${depInterfaceIncludeDirsRaw})
						# "Raw" directories may contain generator expressions eg. "$<BUILD_INTERFACE:C:/xx/yy>",
						# so we transform this into "C:/xx/yy".
						string (REGEX REPLACE "\\$<BUILD_INTERFACE:(.*)>" "\\1" dir "${dir}")
						if ("${dir}" IN_LIST depInterfaceIncludeDirs OR NOT EXISTS "${dir}")
							continue ()
						endif ()
						list (APPEND depInterfaceIncludeDirs "${dir}")
					endforeach ()
					# Retrieve the list of all sources for this target.
					# We also need to query the "header sets" are some libs (eg. reflect-cpp) add their headers here.
					get_property (depSources TARGET ${dependency} PROPERTY SOURCES)
					get_property (depHeaderSets TARGET ${dependency} PROPERTY HEADER_SETS)
					foreach (headerSet ${depHeaderSets})
						get_property (headerSetFiles TARGET ${dependency} PROPERTY HEADER_SET_${headerSet})
						list (APPEND depSources ${headerSetFiles})
					endforeach ()
					foreach (depSource ${depSources})
						# Ignore non-header sources.
						get_filename_component (ext "${depSource}" EXT)
						string (TOLOWER "${ext}" extLower)
						list (FIND includeExts "${extLower}" idx)
						if (${idx} EQUAL -1)
							continue ()
						endif ()
						if (IS_ABSOLUTE "${depSource}")
							# Try to get the relative path from one of the "interface include directories"
							foreach (parentDirCandidate ${depInterfaceIncludeDirs})
								getRelativePathChecked ("${parentDirCandidate}" "${depSource}" depSourceRel)
								if (depSourceRel)
									break ()
								endif ()
							endforeach ()
							# If the source file is not inside one of the interface include dirs,
							# check if it is inside the dependency's binary dir.
							# For example, a file generated with configure_file().
							if (NOT depSourceRel)
								getRelativePathChecked ("${depBinaryDir}" "${depSource}" depSourceRel)
								if (depSourceRel)
									set (depSourceRel "${depSourceDirRel}/${depSource}")
								endif ()
							endif ()
							if (NOT depSourceRel)
								message (SEND_ERROR "Cannot compute relative path for header \"${depSource}\".")
							endif ()
							set (targetPath "${depSource}")
						else ()
							set (depSourceRel "${depSourceDirRel}/${depSource}")
							set (targetPath "${depSourceDir}/${depSource}")
						endif ()
						string (APPEND setupExternFilesCommands "CreateSymlink('${targetPath}', '${projectAbsDir}${srcDirRel}/Source/ThirdParty/Include/${depSourceRel}')\n")
					endforeach ()
				endif()
				get_property (depType TARGET ${dependency} PROPERTY TYPE)
				if (NOT depType MATCHES "^(INTERFACE_LIBRARY|OBJECT_LIBRARY)$")
					# Create symlinks to the lib files of the dependency.
					list (APPEND setupExternFilesDefines "LinkerFile:${dependency}=$<TARGET_LINKER_FILE:${dependency}>")
					string (APPEND setupExternFilesCommands "CreateSymlink('LinkerFile:${dependency}', '${projectAbsDir}${srcDirRel}/Source/ThirdParty/Lib/Config:/LinkerFileName:${dependency}')\n")
					if (depType STREQUAL SHARED_LIBRARY)
						list (APPEND setupExternFilesDefines "File:${dependency}=$<TARGET_FILE:${dependency}>")
						if(WIN32)
							string (APPEND setupExternFilesCommands "CreateSymlink('File:${dependency}', '${projectAbsDir}${srcDirRel}/Binaries/Win64/FileName:${dependency}')\n")
						elseif (APPLE)
							string (APPEND setupExternFilesCommands "CreateSymlink('File:${dependency}', '${projectAbsDir}${srcDirRel}/Binaries/Mac/FileName:${dependency}')\n")
						else()
							string (APPEND setupExternFilesCommands "CreateSymlink('File:${dependency}', '${projectAbsDir}${srcDirRel}/Binaries/Linux/FileName:${dependency}')\n")
						endif()
						
					endif ()
				endif ()
				list (APPEND setupExternFilesDependencies ${dependency})
			endforeach ()
			foreach (dependency ${allDependencyPackages})
				foreach (item ${BE_VCPKG_INCLUDES_${dependency}})
					string (APPEND setupExternFilesCommands "CreateSymlink('${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/${item}', '${projectAbsDir}${srcDirRel}/Source/ThirdParty/Include/${item}')\n")
				endforeach ()
				foreach (item ${BE_VCPKG_LIBS_${dependency}})
					string (APPEND setupExternFilesCommands "CreateSymlink('${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/${item}', '${projectAbsDir}${srcDirRel}/Source/ThirdParty/Lib/Config:/${item}')\n")
				endforeach ()
			endforeach ()
		endif ()
		# Extra stuff for the ITwinForUnreal plugin.
		# TODO: extract this part from be_add_unreal_project so that it remains as generic as possible.
		if ("${pluginName}" STREQUAL ITwinForUnreal)
			# The goal here is to merge the cesium-unreal plugin into our own plugin.
			# Some dirs are simply symlinked.
			foreach (item Config Content Shaders)
				createSymlinksForDirectoryContent ("${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/${item}" "${projectAbsDir}${srcDirRel}/${item}" addedFiles_local)
			endforeach ()
			# Source code of the (modified) CesiumRuntime are symlinked as well:
			foreach (item Source/ITwinCesiumRuntime)
				createSymlink ("${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/${item}" "${projectAbsDir}${srcDirRel}/${item}" addedFiles_local)
			endforeach ()
			# Add a command that will (at build time) create symlinks pointing to the header & lib files "installed"
			# during the build of the external dependencies of cesium-unreal.
			string (APPEND setupExternFilesCommands "SetupCesium('${cesiumDependenciesBinDir}/extern/../Source/ThirdParty', '${projectAbsDir}${srcDirRel}/Source/ThirdParty')\n")
			list (APPEND setupExternFilesDependencies InstallCesiumDependencies)
			list (APPEND extraTests_local "+Cesium.Unit")
		endif ()
		if (setupExternFilesCommands)
			# Actually create the target that will create the symlinks to the lib files of the dependencies.
			file (WRITE "${CMAKE_BINARY_DIR}/UnrealProjects/commands_SetupExternFiles_${uniqueName}.py" "${setupExternFilesCommands}")
			toJsonStrList (definesJson "${setupExternFilesDefines}")
			set (jsonFilePath "${CMAKE_BINARY_DIR}/UnrealProjects/Setup_${uniqueName}.json")
			set (JsonContent "{\
\"commands\":\"${CMAKE_BINARY_DIR}/UnrealProjects/commands_SetupExternFiles_${uniqueName}.py\"\
,\"added\":\"${CMAKE_BINARY_DIR}/UnrealProjects/addedFiles_SetupExternFiles_${uniqueName}.txt\"\
,\"defines\":${definesJson}\
}")

			add_custom_target (SetupExternFiles_${uniqueName}
				COMMAND ${CMAKE_COMMAND} -E make_directory "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Lib/$<CONFIG>"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${projectAbsDir}${srcDirRel}/Binaries/${TargetPlatforms}"
					COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Public/SetupUnrealExternFiles.py" '${JsonContent}'
				VERBATIM
			)

			set_target_properties (SetupExternFiles_${uniqueName} PROPERTIES FOLDER "UnrealProjects/Detail")
			if (setupExternFilesDependencies)
				add_dependencies (SetupExternFiles_${uniqueName} ${setupExternFilesDependencies})
			endif ()
			list (APPEND projectDependencies_local SetupExternFiles_${uniqueName})
		endif ()
		# Make sure CMake is re-run whenever source files are added/removed in the app/plugin dir.
		# This ensures the VS Solution Explorer will be updated.
		# We want to re-run CMake whenever the content of the "Source" dir changes,
		# but we want to ignore symlinks (which are added/removed at cmake/build time).
		# To do so, we monitor:
		# - the content of the "Source" dir *not recursively*, this allows to detect when a module is added or removed,
		# - the content of each sub-file/dir that is not a symlink and not "ThirdParty" (which only contains symlinks).
		# We also monitor .uproject and .uplugin files.
		if ("${pluginName}" STREQUAL "")
			set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${realSrcAbsDir}/${projectName}.uproject")
		else ()
			set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${realSrcAbsDir}/${pluginName}.uplugin")
		endif ()
		file (GLOB _unused_Items CONFIGURE_DEPENDS "${realSrcAbsDir}/Source/*")
		file (GLOB sourceItems RELATIVE "${realSrcAbsDir}/Source" "${realSrcAbsDir}/Source/*")
		foreach (sourceItem ${sourceItems})
			if ("${sourceItem}" STREQUAL "ThirdParty" OR IS_SYMLINK "${realSrcAbsDir}/Source/${sourceItem}")
				continue ()
			endif ()
			file (GLOB_RECURSE _unused_Items CONFIGURE_DEPENDS "${realSrcAbsDir}/Source/${sourceItem}/*")
		endforeach ()
		set (${projectDependenciesRef} ${projectDependencies_local} PARENT_SCOPE)
		set (${addedFilesRef} ${addedFiles_local} PARENT_SCOPE)
		set (${extraTestsRef} ${extraTests_local} PARENT_SCOPE)
	endfunction ()
	cmake_parse_arguments (
		funcArgs
		"NO_TEST"
		""
		"MAIN_DEPENDS;PLUGINS"
		${ARGN}
	)
	set (includeExts .h .hpp .inl)
	get_filename_component (projectAbsDir "${projectDir}" ABSOLUTE)
	get_filename_component (projectName "${projectDir}" NAME)
	set (projectBinDir "${CMAKE_BINARY_DIR}/UnrealProjects/${projectName}")
	# We will record all external dependencies in the variable below.
	set (projectDependencies "")
	# We will record additional tests in the variable below.
	set (extraTests "")
	# We have to ensure that all obsolete files/symlinks created during the previous run cmake
	# are cleaned up, otherwise UBT may report errors (or produce incorrect build output).
	# In the first version of this function, we would simply clean everything at the beginning of the function.
	# This would have the unwanted effect of trigger a full rebuild from UBT (because it relies on file timestamps).
	# To fix this problem, what we do now is try to touch only the files that really needs to be modified,
	# and only remove obsolete files at the end of this function.
	# So we record every file that we add (whether really added or untouched because already up-to-date)
	# in the variable below, which will be saved to disk so that the next cmake run can remove obsolete files on disk.
	set (addedFiles "")
	# Setup the build dir for the main app.
	setupDirs("" projectDependencies addedFiles extraTests DEPENDS ${funcArgs_MAIN_DEPENDS})
	# Now parse the parameters for the plugins.
	# The PLUGINS arguments returned by cmake_parse_arguments() look like this:
	# PLUGIN;MyPlugin1;PLUGIN_DEPENDS;MyLib1;PLUGIN;MyPlugin2;PLUGIN_DEPENDS;MyLib1;MyLib3
	# So we need to break them down to blocks (one block for each plugin).
	if (DEFINED funcArgs_PLUGINS)
		set (plugins ${funcArgs_PLUGINS})
	endif ()
	# Transform into this:
	# PLUGIN|MyPlugin1|PLUGIN_DEPENDS|MyLib1|PLUGIN|MyPlugin2|PLUGIN_DEPENDS|MyLib1|MyLib3
	string (REPLACE ";" "|" plugins "${plugins}")
	# Transform into this (list with 3 elements, the first one being empty and thus ignored by foreach()):
	# ;PLUGIN|MyPlugin1|PLUGIN_DEPENDS|MyLib1|;PLUGIN|MyPlugin2|PLUGIN_DEPENDS|MyLib1|MyLib3
	string (REPLACE "PLUGIN|" ";PLUGIN|" plugins "${plugins}")
	# Iterate over each plugin block, parse it and setup the corresponding build dir.
	foreach (plugin ${plugins})
		string (REPLACE "|" ";" plugin "${plugin}")
		cmake_parse_arguments (
			pluginArgs
			""
			"PLUGIN;PLUGIN_SOURCE"
			"PLUGIN_DEPENDS"
			${plugin}
		)
		set (sourceArgs "")
		if (DEFINED pluginArgs_PLUGIN_SOURCE)
			set (sourceArgs SOURCE "${pluginArgs_PLUGIN_SOURCE}")
		endif ()
		setupDirs("${pluginArgs_PLUGIN}" projectDependencies addedFiles extraTests DEPENDS ${pluginArgs_PLUGIN_DEPENDS} ${sourceArgs})
		be_create_plugin_packager_target("${pluginArgs_PLUGIN}" "${projectAbsDir}/Plugins/${pluginArgs_PLUGIN}")
	endforeach ()
	# Cleanup obsolete files now, before calling UBT to generate the project files.
	# Otherwise, dangling symlinks will cause UBT to report errors.
	set (addedFilesFilePath "${CMAKE_BINARY_DIR}/UnrealProjects/addedFiles_${projectName}.txt")
	set (addedFiles_old "")
	if (EXISTS "${addedFilesFilePath}")
		file (READ "${addedFilesFilePath}" addedFiles_old)
	endif ()
	foreach (addedFile_old ${addedFiles_old})
		if ("${addedFile_old}" IN_LIST addedFiles OR
			# EXISTS returns false for broken symbolic link.
			# So to test if the file (real file or symlink) exists we also have to test IS_SYMLINK,
			# as CMake has no equivalent of python function os.path.lexists().
			NOT (EXISTS "${addedFile_old}" OR IS_SYMLINK "${addedFile_old}"))
			continue ()
		endif ()
		if (IS_SYMLINK "${addedFile_old}")
			# If symlink exists and is valid, was previously added by this script,
			# and is no more added by this script, then should we delete it?
			# Maybe the link has been added by another script?
			# We cannot know, so it is safer to keep the link.
			readSymlink ("${addedFile_old}" target)
			if (EXISTS "${target}")
				continue ()
			endif ()
		else ()
			# The item exists as a regular file/folder (not a symlink).
			# Probably the old symlink has been replaced by a regular file/folder by this script.
			# This may happen for unreal "Config" folder.
			# So, do not report an error, and just do nothing.
			message ("When trying to delete \"${addedFile_old}\", expected to find a symlink, but found a regular file/folder. Skipping.")
			continue ()
		endif ()
		file (REMOVE "${addedFile_old}")
	endforeach ()
	# Save the updated list of current added files.
	file (WRITE "${addedFilesFilePath}" "${addedFiles}")
	
	#set var paths
	if( WIN32)
		set (UnrealLaunchPath "${BE_UNREAL_ENGINE_DIR}/Binaries/Win64/UnrealEditor$<$<CONFIG:UnrealDebug>:-Win64-DebugGame>.exe")
	elseif (APPLE)
		set (UnrealLaunchPath "${BE_UNREAL_ENGINE_DIR}/Binaries/Mac/UnrealEditor$<$<CONFIG:UnrealDebug>:-Mac-DebugGame>")
	endif()

	
	# Now that all build dirs have been setup, we can ask UBT to generate the project files (.sln, .vcxproj...).
	# We will use some of these generated files to create some cmake targets.
	if( WIN32)
		execute_process (
			COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Build.bat" -ProjectFiles -Project "${projectAbsDir}/${projectName}.uproject" 
			COMMAND_ERROR_IS_FATAL ANY
		)
		# The files generated by UBT use VS macro $(SolutionDir), which will not work in our case because our final .sln file
		# (which is generated by CMake) not the .sln file generated by UBT and is not in the same folder.
		# So we create our own project files by duplicating & fixing the files generated by UBT.
		file (MAKE_DIRECTORY "${projectAbsDir}/Intermediate/ProjectFiles_be/")
		foreach (extension .vcxproj .vcxproj.user .vcxproj.filters)
			file (READ "${projectAbsDir}/Intermediate/ProjectFiles/${projectName}${extension}" vcxprojContent)
			string (REPLACE "$(SolutionDir)" "${projectAbsDir}/" vcxprojContent "${vcxprojContent}")
			writeFile ("${projectAbsDir}/Intermediate/ProjectFiles_be/${projectName}${extension}" "${vcxprojContent}")
		endforeach ()
		# Some project files are referenced by others (using relative path) but we do not need to fix them
		# so let's just make symlinks.
		file (GLOB propsFiles RELATIVE "${projectAbsDir}/Intermediate/ProjectFiles" "${projectAbsDir}/Intermediate/ProjectFiles/*.props")
		foreach (propsFile ${propsFiles})
			createSymlink ("${projectAbsDir}/Intermediate/ProjectFiles/${propsFile}" "${projectAbsDir}/Intermediate/ProjectFiles_be/${propsFile}" addedFiles)
		endforeach ()
		# If for whatever reason, project files generated by UBT are modified, we have to update our own "fixed" files.
		# Thus, make sure CMake is re-run if these file changes.
		# We could think this would create an infinite loop because CMake launches UBT which writes the project files, which triggers a CMake relaunch.
		# Fortunately this does not happen.
		set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${projectAbsDir}/Intermediate/ProjectFiles/${projectName}.vcxproj")
		# Import the .vcxproj as a new CMake target.
			include_external_msproject (${projectName} "${projectAbsDir}/Intermediate/ProjectFiles_be/${projectName}.vcxproj")
		# Since Unreal uses its own specific set of configs, we have to map them to CMake configs.
		set_target_properties(${projectName} PROPERTIES
			MAP_IMPORTED_CONFIG_DEBUG DebugGame_Editor
			MAP_IMPORTED_CONFIG_UNREALDEBUG DebugGame_Editor
			MAP_IMPORTED_CONFIG_RELEASE Development_Editor
		)
		# Add a target that will build the Shipping version of the app.
		add_custom_target (${projectName}_Shipping ALL
			# This target is only compatible with CMake Release config, since it uses Release extern binaries.
			# Hence the dummy command that will output an explicit error message.
			COMMAND "$<$<NOT:$<CONFIG:Release>>:TARGET_NOT COMPATIBLE_WITH_THIS_CONFIG>"
			COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Build.bat" ${projectName} Win64 Shipping -Project="${projectAbsDir}/${projectName}.uproject" -WaitMutex -FromMsBuild
			)
		set_property (TARGET ${projectName}_Shipping PROPERTY VS_DEBUGGER_COMMAND "${projectAbsDir}/Binaries/Win64/${projectName}-Win64-Shipping.exe")
	elseif (APPLE)
	# Mac has a risk of freeze of UnrealBuildTools, try multiple time with tiemout
		message( "generating unreal project for ${projectAbsDir}/${projectName}.uproject")
		execute_process (
			COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Mac/Build.sh" -ProjectFiles -Project "${projectAbsDir}/${projectName}.uproject" -WaitMutex
			TIMEOUT 300
			RESULT_VARIABLE res
		)
		if (NOT ${res} STREQUAL "0")
			if (${res} MATCHES ".*timeout.*")
				message( "try generating unreal project for ${projectAbsDir}/${projectName}.uproject after timeout : ${res}")
				execute_process (
				COMMAND "killall dotnet" 
			)
			else()
				message( "try generating unreal project for ${projectAbsDir}/${projectName}.uproject after error : ${res}")
			endif()
			execute_process (
				COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Mac/Build.sh" -ProjectFiles -Project "${projectAbsDir}/${projectName}.uproject" 
				RESULT_VARIABLE res
				TIMEOUT 300
			)
			if (NOT ${res} STREQUAL "0")
				if (${res} MATCHES ".*timeout.*")
					message( "try generating unreal project for ${projectAbsDir}/${projectName}.uproject after timeout : ${res}")
					execute_process (
						COMMAND "killall dotnet" 
					)
				else()
					message( "try generating unreal project for ${projectAbsDir}/${projectName}.uproject after error : ${res}")
				endif()
				execute_process (
					COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Mac/Build.sh" -ProjectFiles -Project "${projectAbsDir}/${projectName}.uproject"
					COMMAND_ERROR_IS_FATAL ANY
					TIMEOUT 300
				)
			endif()
		endif()
		
		# Create a symlink to engine for debugging purpose
		message("createSymlink " "${BE_UNREAL_ENGINE_DIR}" to  "${projectAbsDir}/../Engine")	
		createSymlink ("${BE_UNREAL_ENGINE_DIR}" "${projectAbsDir}/../Engine" addedFiles)
		
		# Create targets linking to xcode proj generated earlier
		if (CMAKE_OSX_ARCHITECTURES)
			add_custom_target( ${projectName} 
				COMMAND "xcodebuild" "build" "-scheme" "${projectName}" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName} (Mac).xcodeproj" "-arch" "${CMAKE_OSX_ARCHITECTURES}"
				VERBATIM
			)
		else()
			add_custom_target( ${projectName} 
				COMMAND "xcodebuild" "build" "-scheme" "${projectName}" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName} (Mac).xcodeproj"
				VERBATIM
			)
		endif()
	
		if (CMAKE_OSX_ARCHITECTURES)
			add_custom_target( ${projectName}Editor 
				COMMAND "xcodebuild" "build" "-scheme" "${projectName}Editor" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName}Editor (Mac).xcodeproj" "-arch" "${CMAKE_OSX_ARCHITECTURES}"
				VERBATIM
			)
		else()
			add_custom_target( ${projectName}Editor 
				COMMAND "xcodebuild" "build" "-scheme" "${projectName}Editor" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName}Editor (Mac).xcodeproj"
				VERBATIM
			)
		endif()
		
		if (NOT funcArgs_NO_TEST)
			# Add a target that will build the Shipping version of the app.
			add_custom_target (${projectName}_Shipping ALL
				# This target is only compatible with CMake Release config, since it uses Release extern binaries.
				# Hence the dummy command that will output an explicit error message.
				COMMAND "$<$<NOT:$<CONFIG:Release>>:TARGET_NOT COMPATIBLE_WITH_THIS_CONFIG>"
				COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Mac/Build.sh" ${projectName} Mac Shipping -Project="${projectAbsDir}/${projectName}.uproject" -WaitMutex
				)
		endif()

				
		add_custom_target( Run${projectName}Editor
			COMMAND "${BE_UNREAL_ENGINE_DIR}/Binaries/Mac/UnrealEditor" "${projectAbsDir}/${projectName}.uproject"
			WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/.."
		)
		add_dependencies (Run${projectName}Editor ${projectName}Editor)
		set_target_properties (Run${projectName}Editor  PROPERTIES FOLDER "UnrealProjects")
		
	endif()
	
	# Add dependencies to all external libs used by the project.
	if (projectDependencies)
		add_dependencies (${projectName} ${projectDependencies})
		if (NOT funcArgs_NO_TEST)
			add_dependencies (${projectName}_Shipping ${projectDependencies})
		endif()
		if(APPLE)
			add_dependencies (${projectName}Editor  ${projectDependencies})
		endif()
	endif ()
	
	set_target_properties (${projectName} PROPERTIES FOLDER "UnrealProjects")
	if (NOT funcArgs_NO_TEST)
		set_target_properties (${projectName}_Shipping PROPERTIES FOLDER "UnrealProjects/Shipping")
	endif()
	if(APPLE)
		set_target_properties (${projectName}Editor PROPERTIES FOLDER "UnrealProjects")
	endif ()
	
	# Update the BuildUnrealProjects target so that it will build this project too.
	if (NOT TARGET BuildUnrealProjects)
		add_custom_target (BuildUnrealProjects ALL)
	endif ()
	add_dependencies (BuildUnrealProjects ${projectName})
	set_target_properties (BuildUnrealProjects PROPERTIES FOLDER "UnrealProjects/Detail")
	if(APPLE)
		add_dependencies (BuildUnrealProjects ${projectName}Editor)
	endif ()
	
	if (NOT funcArgs_NO_TEST)
		# Add a target to run the tests.
		add_custom_target (RunTests_${projectName} ALL # "ALL" so that target is enabled and MSBuild can build it.
			# UnrealEditor.exe loads the Development binaries (this config is mapped to CMake Release config).
			# UnrealEditor-Win64-DebugGame.exe loads the DebugGame binaries (this config is mapped to CMake Debug config).
			# Other CMake configs (if any) are not supported, so we add a dummy command that will try to run an unexisting command with an explicit name.
			COMMAND "$<$<NOT:$<CONFIG:UnrealDebug,Release>>:TARGET_NOT COMPATIBLE_WITH_THIS_CONFIG>"
			COMMAND "${UnrealLaunchPath}" "${projectAbsDir}/${projectName}.uproject" "-ExecCmds=\"Automation RunTests Bentley${extraTests};Quit\"" -unattended -nopause -editor -stdout
		)
		set_target_properties (RunTests_${projectName} PROPERTIES FOLDER "UnrealProjects/Tests")
		add_dependencies (RunTests_${projectName} ${projectName})
		if (NOT TARGET RunUnrealTests)
			add_custom_target (RunUnrealTests ALL)
		endif ()
		if(APPLE)
			add_dependencies (RunTests_${projectName} ${projectName}Editor)  #unreal tests needs the editor version compiled otherwise the Cesium Runtime Plugin may not be installeed by UnrealbuiltTool
		endif ()
		add_dependencies (RunUnrealTests RunTests_${projectName})
		set_target_properties (RunUnrealTests PROPERTIES FOLDER "UnrealProjects/Detail")
	endif ()

	if(WIN32)
		# If not done yet, add the UE5 project generated by UBT, which references all Unreal sources
		# and is needed for VAX to work correctly.
		# We also add Unreal natvis files for improved debugging experience.
		get_property (beIsUE5ProjectImported GLOBAL PROPERTY beIsUE5ProjectImported)
		set_property (GLOBAL PROPERTY beIsUE5ProjectImported ON)
		if (NOT beIsUE5ProjectImported)
			include_external_msproject (UE5 "${projectAbsDir}/Intermediate/ProjectFiles/UE5.vcxproj" PLATFORM Win64)
			set_target_properties(UE5 PROPERTIES
				MAP_IMPORTED_CONFIG_DEBUG BuiltWithUnrealBuildTool
				MAP_IMPORTED_CONFIG_UNREALDEBUG BuiltWithUnrealBuildTool
				MAP_IMPORTED_CONFIG_RELEASE BuiltWithUnrealBuildTool
			)
			set_target_properties (UE5 PROPERTIES FOLDER "UnrealProjects/Detail")
			add_custom_target (UE5Natvis
				SOURCES
					"${BE_UNREAL_ENGINE_DIR}/Extras/VisualStudioDebugging/Unreal.natvis" 
				"${CMAKE_SOURCE_DIR}/Public/VSNatVis/cesium_gltf.natvis"
				)
			set_target_properties (UE5Natvis PROPERTIES FOLDER "UnrealProjects/Detail")
		endif ()
	endif()

endfunction ()
