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
	if(NOT ${package} STREQUAL "")
		string (TOLOWER ${package} package)
		if (${package} IN_LIST BE_VCPKG_PACKAGES)
			set (result_local ${package})
		endif ()
	endif()
	set (${result} ${result_local} PARENT_SCOPE)
endfunction ()

# Retrieve the project name from the .uproject file in the specified dir.
function (getProjectName result projectDir)
	get_filename_component (projectAbsDir "${projectDir}" ABSOLUTE)
	file (GLOB projects RELATIVE "${projectAbsDir}" "${projectAbsDir}/*.uproject")
	list (LENGTH projects projectCount)
	if (NOT ${projectCount} EQUAL 1)
		message (SEND_ERROR "Found ${projectCount} *.uproject files in ${projectDir}: [${projects}]. Expected exactly one.")
	endif ()
	list (GET projects 0 project_)
	get_filename_component (project_ "${project_}" NAME_WE)
	set (${result} ${project_} PARENT_SCOPE)
endfunction ()

# Creates a custom target which will package the plugin
function (be_create_plugin_packager_target pluginName projectDir)
	get_filename_component (projectAbsDir "${projectDir}" ABSOLUTE)
	getProjectName(projectName "${projectDir}")
	if(FOR_VERACODE)
		message( "not create_plugin_packager_ ${pluginName} ${projectDir}")
		return ()
	endif ()
	set ( packagerTargetName "${pluginName}_PluginPackager" )
	message( "create_plugin_packager_ ${pluginName} ${projectDir}")
	# Since the same plugin can be used in several projects (thanks to the "external source" feature),
	# we make sure to create only a single "packager" target for each plugin.
	if (TARGET ${packagerTargetName})
		return ()
	endif ()
	set ( pluginPackageSrcDir "${CMAKE_BINARY_DIR}/Packaging_Input/${pluginName}" )
	set ( pluginPackageDstDir "${CMAKE_BINARY_DIR}/Packaging_Output/${pluginName}" )
	if (APPLE)
		set ( RunUATBasename "RunUAT.sh" )
		set ( TargetPlatform "Mac" ) ## TODO_JDE
	elseif (CMAKE_HOST_UNIX)
		set ( RunUATBasename "RunUAT.sh" )
		set ( TargetPlatform "Linux" ) ## TODO_JDE
	else ()
		set ( RunUATBasename "RunUAT.bat" )
		set ( TargetPlatform "Win64" )
	endif ()
	add_custom_target ( ${packagerTargetName} ALL )
	# First duplicate the plugin folder completely: this is done to eliminate any
	# symbolic link, which RunUAT does not like at all...
	# Then run RunUAT for the current platform.
	# Note: We need to cleanup the Binaries directory (remove all previously built stuff coming from the "source" project).
	# But we still need to keep the extern binaries (eg Singleton.dll for plugin ITwinForUnreal).
	# So we retrieve them from the ExternBinaries_XXX dir which contains a backup copy of these binaries.
	if (APPLE)
		if(CMAKE_OSX_ARCHITECTURES)
			add_custom_command ( TARGET ${packagerTargetName}
				POST_BUILD
				WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Input"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Output"
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}"
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageDstDir}"
				COMMAND cp -RL "${projectAbsDir}/Plugins/${pluginName}" "${pluginPackageSrcDir}"
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}/Binaries"
				COMMAND cp -RL "${CMAKE_BINARY_DIR}/UnrealProjects/ExternBinaries_${projectName}_${pluginName}" "${pluginPackageSrcDir}/Binaries"
				COMMAND ./${RunUATBasename} BuildPlugin -Plugin=${pluginPackageSrcDir}/${pluginName}.uplugin -Package="${pluginPackageDstDir}" -CreateSubFolder -TargetPlatforms=${TargetPlatform} -Architecture_Mac=${CMAKE_OSX_ARCHITECTURES}
			)
		else()
			add_custom_command ( TARGET ${packagerTargetName}
				POST_BUILD
				WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Input"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Output"
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}"
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageDstDir}"
				COMMAND cp -RL "${projectAbsDir}/Plugins/${pluginName}" "${pluginPackageSrcDir}"
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}/Binaries"
				COMMAND cp -RL "${CMAKE_BINARY_DIR}/UnrealProjects/ExternBinaries_${projectName}_${pluginName}" "${pluginPackageSrcDir}/Binaries"
				COMMAND ./${RunUATBasename} BuildPlugin -Plugin=${pluginPackageSrcDir}/${pluginName}.uplugin -Package="${pluginPackageDstDir}" -CreateSubFolder -TargetPlatforms=${TargetPlatform}
			)
		endif()
	else()
		add_custom_command ( TARGET ${packagerTargetName}
			POST_BUILD
			WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Input"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Packaging_Output"
			COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}"
			COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageDstDir}"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${projectAbsDir}/Plugins/${pluginName}" "${pluginPackageSrcDir}"
			# Set sources as read-only, to disable the "adaptive mode" of the unity build, thus actually merging source files.
			# See doc on bUseAdaptiveUnityBuild here: https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine?application_version=5.5
			# Ideally we would enable bUseAdaptiveUnityBuild as we do in the xxx.target.cs file for our apps,
			# but there is no xxx.target.cs (or BuildConfiguration.xml) for plugins.
			COMMAND attrib /S +R "${pluginPackageSrcDir}/*"
			COMMAND ${CMAKE_COMMAND} -E rm -rf "${pluginPackageSrcDir}/Binaries"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/UnrealProjects/ExternBinaries_${projectName}_${pluginName}" "${pluginPackageSrcDir}/Binaries"
			COMMAND ./${RunUATBasename} BuildPlugin -Plugin=${pluginPackageSrcDir}/${pluginName}.uplugin -Package="${pluginPackageDstDir}" -CreateSubFolder -TargetPlatforms=${TargetPlatform}
		)
	endif()
	set_target_properties (${packagerTargetName} PROPERTIES FOLDER "UnrealProjects/Packaging")
	# Make sure ThirdParty folder is populated (and thus all extern libs are built) before packaging.
	if (TARGET SetupExternFiles_${projectName}_${pluginName})
		add_dependencies (${packagerTargetName} SetupExternFiles_${projectName}_${pluginName})
	endif ()
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
			# Create a folder that will contain backup copies of the extern binaries.
			# See comment in be_create_plugin_packager_target().
			file (MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/UnrealProjects/ExternBinaries_${uniqueName}")
		endif ()
		if (APPLE)
			set ( TargetPlatform "Mac" )
		elseif (CMAKE_HOST_UNIX)
			set ( TargetPlatform "Linux" )
		else ()
			set ( TargetPlatform "Win64" )
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
			# This is difficult to implement a reliable generic process here, so for now we handle ITwinForUnreal specifically.
			# To be generic, one possible solution would be to walk the external dir and for each item (file, folder),
			# ask git for whether the item is ignored. Only non-ignored items should be symlinked.
			set (otherItems 
				"${pluginName}.uplugin"
			)
			if ("${pluginName}" STREQUAL ITwinForUnreal)
				list (APPEND otherItems
					"Config/BaseITwinForUnreal.ini"
					"Content/ITwin"
					"Resources"
					"Shaders/ITwin"
				)
			endif ()
			foreach (item ${otherItems})
				createSymlink ("${CMAKE_SOURCE_DIR}/${funcArgs_SOURCE}${srcDirRel}/${item}" "${projectAbsDir}${srcDirRel}/${item}" addedFiles_local)
			endforeach ()
		endif ()
		get_property (cesiumDependenciesBinDir DIRECTORY "${CMAKE_SOURCE_DIR}/Public/CesiumDependencies" PROPERTY BINARY_DIR)
		if (DEFINED funcArgs_DEPENDS)
			file (MAKE_DIRECTORY "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Include")
			# Create those two too to avoid "System.IO.DirectoryNotFoundException" errors with C# stack trace from UBT,
			# although you will get multiple "xxx.lib was not resolvable to a file when used in Module ..." instead :-/
			# (until building once per configuration has created the links)
			file (MAKE_DIRECTORY "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Lib/UnrealDebug")
			file (MAKE_DIRECTORY "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Lib/Release")
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
						list (APPEND allDependencyTargets ${dependency})
						# Retrieve the transitive dependencies, which will be processed in the next loop.
						# If the current dependency is a shared lib however, then we skip the recursive dependencies.
						# This makes sense because the static libs used by the shared libs are "private" to the shared lib.
						# For "public" headers however, we might need to consider transitive dependencies (TODO).
						# For now this case has not happened yet.
						get_property (dependencyType TARGET ${dependency} PROPERTY TYPE)
						if ("${dependencyType}" STREQUAL SHARED_LIBRARY)
							continue ()
						endif ()
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
				get_property (depSourceDir TARGET ${dependency} PROPERTY SOURCE_DIR)
				# Had to work around that for imported targets (Async++ in my case) for which depSourceDir
				# and depBinaryDir were exactly CMAKE_SOURCE_DIR and CMAKE_BINARY_DIR: since it comes from cesium-native,
				# links are already done - not sure it will work for vcpkg dependencies not coming from cs...?
				if (depSourceDir STREQUAL "${CMAKE_SOURCE_DIR}")
					continue()
				endif()
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
					# - External targets added by FetchContent()
					# In all cases, the goal is to retrieve the list of headers and lib files of the target,
					# and decide where to put the symlinks inside the "Source/ThirdParty" folder.
					# Examples:
					# - For BeUtils, when retrieving the list of source files, CMake will likely return relative paths
					#   as this is how they are "declared" in add_library(), eg. "Gltf/GltfBuilder.h".
					#   This file will be symlinked as "Source/ThirdParty/Include/BeUtils/Gltf/GltfBuilder.h".
					# - For FetchContent() deps, when retrieving the list of source files, CMake will likely return absolute paths,
					#   eg. "<binaryDir>/_deps/foo-src/include/foo.h".
					#   This file will be symlinked as "Source/ThirdParty/Include/foo/foo.h".
					# For external libraries, we need to retrieve the list of "interface include directories",
					# eg. "<binaryDir>/_deps/foo-src/include", so that we can compute the relative path of each header.
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
					# We also need to query the "header sets" are some libs (initially reflect-cpp, although not anymore) add their headers here.
					get_property (depSources TARGET ${dependency} PROPERTY SOURCES)
					get_property (depHeaderSets TARGET ${dependency} PROPERTY HEADER_SETS)
					foreach (headerSet ${depHeaderSets})
						get_property (headerSetFiles TARGET ${dependency} PROPERTY HEADER_SET_${headerSet})
						list (APPEND depSources ${headerSetFiles})
					endforeach ()
					foreach (depSource ${depSources})
						# Ignore non-header sources.
						get_filename_component (ext "${depSource}" LAST_EXT)
						string (TOLOWER "${ext}" extLower)
						list (FIND includeExts "${extLower}" idx)
						if (${idx} EQUAL -1)
							continue ()
						endif ()
						if (IS_ABSOLUTE "${depSource}")
							set (depSourceRel "")
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
									set (depSourceRel "${depSourceDirRel}/${depSourceRel}")
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
						string (APPEND setupExternFilesCommands "CreateSymlink('File:${dependency}', '${projectAbsDir}${srcDirRel}/Binaries/${TargetPlatform}/FileName:${dependency}')\n")
						# Also make a backup copy that will be used in be_create_plugin_packager_target(), see comment in this function.
						string (APPEND setupExternFilesCommands "CreateSymlink('File:${dependency}', '${CMAKE_BINARY_DIR}/UnrealProjects/ExternBinaries_${uniqueName}/${TargetPlatform}/FileName:${dependency}')\n")
					endif ()
				endif ()
				list (APPEND setupExternFilesDependencies ${dependency})
			endforeach ()
			# Create extra directory symlinks (currently just one: BeBuildConfig/, used for options)
			get_property (extraFolders GLOBAL PROPERTY beExtraFoldersToSymlink)
			foreach (extraFolder ${extraFolders})
				# Check if the extra dir is in the CMake "source" or "binary" directory.
				getRelativePathChecked ("${CMAKE_SOURCE_DIR}" "${extraFolder}" extraFolderRel)
				if (extraFolderRel)
					# Same remark as above: we want to retrieve the source dir relative to Public or Private dir.
					string (REGEX REPLACE "^:[^/]*/" "" extraFolderRel ":${extraFolderRel}")
				else ()
					getRelativePathChecked ("${CMAKE_BINARY_DIR}" "${extraFolder}" extraFolderRel)
				endif ()
				if (NOT extraFolderRel)
					message (SEND_ERROR "Cannot compute relative path for extra directory \"${extraFolder}\".")
				endif ()
				set (linkPath "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Include/${extraFolderRel}")
				createSymlink ("${extraFolder}" "${linkPath}" addedFiles_local)
			endforeach()
			foreach (dependency ${allDependencyPackages})
				foreach (item ${BE_VCPKG_INCLUDES_${dependency}})
					string (APPEND setupExternFilesCommands "CreateSymlink('${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/${item}', '${projectAbsDir}${srcDirRel}/Source/ThirdParty/Include/${item}')\n")
				endforeach ()
				foreach (item ${BE_VCPKG_LIBS_${dependency}})
					string (APPEND setupExternFilesCommands "CreateSymlink('${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/${item}', '${projectAbsDir}${srcDirRel}/Source/ThirdParty/Lib/Config:/${item}')\n")
				endforeach ()
			endforeach ()
		endif () # DEFINED funcArgs_DEPENDS

		# Extra stuff for the ITwinForUnreal plugin.
		# TODO: extract this part from be_add_unreal_project so that it remains as generic as possible.
		if ("${pluginName}" STREQUAL ITwinForUnreal)
			# The goal here is to merge the cesium-unreal plugin into our own plugin.
			# Some dirs are simply symlinked.
			foreach (item Config Content Shaders)
				createSymlinksForDirectoryContent ("${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/${item}" "${projectAbsDir}${srcDirRel}/${item}" addedFiles_local)
			endforeach ()
			# Source code of the (modified) CesiumRuntime are symlinked as well:
			foreach (item Source/CesiumRuntime)
				createSymlink ("${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/${item}" "${projectAbsDir}${srcDirRel}/${item}" addedFiles_local)
			endforeach ()
			# Add a command that will (at build time) create symlinks pointing to the header & lib files "installed"
			# during the build of the external dependencies of cesium-unreal.
			string (APPEND setupExternFilesCommands "SetupCesium('${cesiumDependenciesBinDir}/extern/../Source/ThirdParty', '${projectAbsDir}${srcDirRel}/Source/ThirdParty', '${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}')\n")
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
,\"added\":\"${CMAKE_BINARY_DIR}/UnrealProjects/addedFiles_SetupExternFiles_${uniqueName}_$<CONFIG>.txt\"\
,\"defines\":${definesJson}\
}")

			add_custom_target (SetupExternFiles_${uniqueName}
				COMMAND ${CMAKE_COMMAND} -E make_directory "${projectAbsDir}${srcDirRel}/Source/ThirdParty/Lib/$<CONFIG>"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${projectAbsDir}${srcDirRel}/Binaries/${TargetPlatform}"
				COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Public/SetupUnrealExternFiles.py" '${JsonContent}'
				VERBATIM
			)
			set_target_properties (SetupExternFiles_${uniqueName} PROPERTIES FOLDER "UnrealProjects/Detail")
			if (setupExternFilesDependencies)
				add_dependencies (SetupExternFiles_${uniqueName} ${setupExternFilesDependencies})
			endif ()
			list (APPEND projectDependencies_local SetupExternFiles_${uniqueName})
		endif ()
		# TODO: extract this part too from be_add_unreal_project so that it remains as generic as possible.
		if ("${uniqueName}" STREQUAL iTwinEngage)
			# Note 1: See Carrot.Build.cs for the addition of ffmpeg exe and license to the RuntimeDependencies
			# Note 2: Using a symlink got UBT to fail copying the dep (on windows) because it compares the link's size
			# (zero byte) and date when making a copy, but the actual file is copied (not the link).
			set(_targetDir "${projectAbsDir}${srcDirRel}/Binaries/${TargetPlatform}")
			set(_ffmpegExe "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg/ffmpeg${CMAKE_EXECUTABLE_SUFFIX}")
			set(_ffmpegLic "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ffmpeg/copyright")
			add_custom_target (SetupExternFiles_${uniqueName}_ffmpeg
				COMMAND echo 				make_directory \"${_targetDir}\"
				COMMAND ${CMAKE_COMMAND} -E make_directory  "${_targetDir}"
				COMMAND echo 				copy -t \"${_targetDir}\" \"${_ffmpegExe}\"
				COMMAND ${CMAKE_COMMAND} -E copy -t  "${_targetDir}"   "${_ffmpegExe}"
				COMMAND echo 				copy \"${_ffmpegLic}\" \"${_targetDir}/ffmpeg-copyright.txt\"
				COMMAND ${CMAKE_COMMAND} -E copy  "${_ffmpegLic}"   "${_targetDir}/ffmpeg-copyright.txt"
				VERBATIM
			)
			set_target_properties (SetupExternFiles_${uniqueName}_ffmpeg PROPERTIES FOLDER "UnrealProjects/Detail")
			list (APPEND projectDependencies_local SetupExternFiles_${uniqueName}_ffmpeg)
		endif()
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
	endfunction () # setupDirs
	cmake_parse_arguments (
		funcArgs
		"NO_TEST"
		""
		"MAIN_DEPENDS;PLUGINS"
		${ARGN}
	)
	set (includeExts .h .hpp .inl)
	get_filename_component (projectAbsDir "${projectDir}" ABSOLUTE)
	getProjectName(projectName "${projectDir}")
	set (projectBinDir "${CMAKE_BINARY_DIR}/UnrealProjects/${projectName}")
	# We will record all external dependencies in the variable below.
	set (projectDependencies "")
	# We will record additional tests in the variable below.
	set (extraTests "")
	# We have to ensure that all obsolete files/symlinks created during the previous run cmake
	# are cleaned up, otherwise UBT may report errors (or produce incorrect build output).
	# In the first version of this function, we would simply clean everything at the beginning of the function.
	# This would have the unwanted effect of triggering a full rebuild from UBT (because it relies on file timestamps).
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
		if (NOT FOR_VERACODE)
			be_create_plugin_packager_target("${pluginArgs_PLUGIN}" "${projectDir}")
		endif()
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
		# (which is generated by CMake) is not the .sln file generated by UBT and is not in the same folder.
		# So we create our own project files by duplicating & fixing the files generated by UBT.
		file (MAKE_DIRECTORY "${projectAbsDir}/Intermediate/ProjectFiles_be/")
		foreach (extension .vcxproj .vcxproj.user .vcxproj.filters)
			file (READ "${projectAbsDir}/Intermediate/ProjectFiles/${projectName}${extension}" vcxprojContent)
			string (REPLACE "$(SolutionDir)" "${projectAbsDir}/" vcxprojContent "${vcxprojContent}")
			# In the vcxproj file, also set the "ProjectName" tag, which is the name of the project as it appears in the solution browser.
			# UBT creates a project named eg. "MyApp" but we want to rename it "MyApp_Editor" so that it's clear it builds the Editor version.
			# The tag should be a sibling of the (existing) "RootNamespace" tag so we just add it next to it.
			# Also append a fixed list of folders to be used as hints for Intellisense, otherwise it will only work with ITwinTestApp
			# (maybe because all other apps use the ITwinForUnreal plugin through symlinks?)
			if (${extension} STREQUAL .vcxproj)
				string (REPLACE "</RootNamespace>" "</RootNamespace><ProjectName>${projectName}_Editor</ProjectName>" vcxprojContent "${vcxprojContent}")
				if ("${projectAbsDir}" MATCHES "/Private/UnrealProjects/") # UnrealPublish doesn't have it and ITwinTestApp shouldn't need this hack
					file (READ "${CMAKE_SOURCE_DIR}/Private/CMake/IntellisenseHints.txt" intellisenseHints)
					string(REPLACE "@projectAbsDir@" "${projectAbsDir}" intellisenseHints "${intellisenseHints}")
					string(REPLACE "/" "\\" intellisenseHints "${intellisenseHints}")
					# Prepend to the existing huge list of UE source folders there:
					string(REPLACE "<SourcePath>" "<SourcePath>${intellisenseHints};" vcxprojContent "${vcxprojContent}")
				endif()
			endif ()
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
			include_external_msproject (${projectName}_Editor "${projectAbsDir}/Intermediate/ProjectFiles_be/${projectName}.vcxproj")
		# Since Unreal uses its own specific set of configs, we have to map them to CMake configs.
		set_target_properties(${projectName}_Editor PROPERTIES
			MAP_IMPORTED_CONFIG_DEBUG DebugGame_Editor
			MAP_IMPORTED_CONFIG_UNREALDEBUG DebugGame_Editor
			MAP_IMPORTED_CONFIG_RELEASE Development_Editor
		)
		# Add a target that will build the non-unity Editor version of the app.
		add_custom_target (${projectName}_Editor_NoUnity ALL
			COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Build.bat" ${projectName}Editor Win64 $<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development> -Project="${projectAbsDir}/${projectName}.uproject" -WaitMutex -FromMsBuild -beNoUnity
		)
		set_property (TARGET ${projectName}_Editor_NoUnity PROPERTY VS_DEBUGGER_COMMAND "${UnrealLaunchPath}")
		set_property (TARGET ${projectName}_Editor_NoUnity PROPERTY VS_DEBUGGER_COMMAND_ARGUMENTS -Project="${projectAbsDir}/${projectName}.uproject"  -skipcompile)
		# Add a target that will build the Game (non-editor) version of the app.
		add_custom_target (${projectName}_Game ALL
			COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Build.bat" ${projectName} Win64 $<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development> -Project="${projectAbsDir}/${projectName}.uproject" -WaitMutex -FromMsBuild "$<$<BOOL:${BE_COMFY}>:-beComfy>"
		)
		set_property (TARGET ${projectName}_Game PROPERTY VS_DEBUGGER_COMMAND "${projectAbsDir}/Binaries/Win64/${projectName}$<$<CONFIG:UnrealDebug>:-Win64-DebugGame>.exe")

		if (NOT FOR_VERACODE)
			set (packagingOutputDir_Game "${CMAKE_BINARY_DIR}/Packaging_Output/${projectName}/$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>")

			# Add a target that will build & package the Game version of the app.
			add_custom_target (${projectName}_Game_Packaged ALL
				# Delete the output folder, as it may contain temporary files generated while debugging the iTwinStudio app
				# (since the output folder is symlinked inside the iTwinStudio Carrot app folder).
				# Due to these (potentially large) files, the "studio-cli apps package" command may fail to create the zip file.
				# Notes:
				# - UAT command does not delete the output folder.
				# - Deleting the output folder before running UAT has not effect on build time.
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${packagingOutputDir_Game}"
				COMMAND "${Python3_EXECUTABLE}"
						"${CMAKE_SOURCE_DIR}/Public/CMake/run_uat_with_config.py"
						--ini-source "${projectAbsDir}/Config/DefaultGame.ini"
						--override-dir "${CMAKE_BINARY_DIR}/UnrealConfigOverrides/${projectName}/$<CONFIG>"
						--comfy-flag "$<$<BOOL:${BE_COMFY}>:ON>$<$<NOT:$<BOOL:${BE_COMFY}>>:OFF>"
						# passed to the RunUAT.bat command ---
						"${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/RunUAT.bat"
						-ScriptsForProject="${projectAbsDir}/${projectName}.uproject"
						Turnkey
						-command=VerifySdk
						-platform=Win64
						-UpdateIfNeeded
						-project="${projectAbsDir}/${projectName}.uproject"
						BuildCookRun
						-nop4 -utf8output -nocompileeditor -skipbuildeditor -cook
						-project="${projectAbsDir}/${projectName}.uproject"
						-target=${projectName}
						-unrealexe=${UnrealLaunchPath}
						-platform=Win64 -installed -stage -archive -package -build -pak -iostore -compressed -prereqs
						-archivedirectory="${packagingOutputDir_Game}"
						-clientconfig=$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>
						-nocompile -nocompileuat
				| "${Python3_EXECUTABLE_NATIVE}" "${CMAKE_SOURCE_DIR}/Public/CMake/FixStdoutForVS.py"
				VERBATIM
			)
				
			# Add a target that will build the Shipping version of the app.
			add_custom_target (${projectName}_Shipping ALL
				# This target is only compatible with CMake Release config, since it uses Release extern binaries.
				# Hence the dummy command that will output an explicit error message.
				COMMAND "$<$<NOT:$<CONFIG:Release>>:TARGET_NOT_COMPATIBLE_WITH_THIS_CONFIG>"
				COMMAND "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Build.bat" ${projectName} Win64 Shipping -Project=${projectAbsDir}/${projectName}.uproject -WaitMutex -FromMsBuild
			)
			# Add a target that will build & package the Shipping version of the app.
			set_property (TARGET ${projectName}_Shipping PROPERTY VS_DEBUGGER_COMMAND "${projectAbsDir}/Binaries/Win64/${projectName}-Win64-Shipping.exe")
			set (packagingOutputDir_Shipping "${CMAKE_BINARY_DIR}/Packaging_Output/${projectName}/Shipping")
			add_custom_target (${projectName}_Shipping_Packaged ALL
				COMMAND "$<$<NOT:$<CONFIG:Release>>:TARGET_NOT_COMPATIBLE_WITH_THIS_CONFIG>"
				# Delete the output folder, see comment above for the XXX_Game_Packaged target.
				COMMAND ${CMAKE_COMMAND} -E rm -rf "${packagingOutputDir_Shipping}"
				COMMAND "${Python3_EXECUTABLE}"
						"${CMAKE_SOURCE_DIR}/Public/CMake/run_uat_with_config.py"
						--ini-source "${projectAbsDir}/Config/DefaultGame.ini"
						--override-dir "${CMAKE_BINARY_DIR}/UnrealConfigOverrides/${projectName}/$<CONFIG>"
						--comfy-flag "$<$<BOOL:${BE_COMFY}>:ON>$<$<NOT:$<BOOL:${BE_COMFY}>>:OFF>"
						# passed to the RunUAT.bat command ---
						"${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/RunUAT.bat"
						-ScriptsForProject="${projectAbsDir}/${projectName}.uproject"
						Turnkey
						-command=VerifySdk
						-platform=Win64
						-UpdateIfNeeded
						-project="${projectAbsDir}/${projectName}.uproject"
						BuildCookRun
						-nop4 -utf8output -nocompileeditor -skipbuildeditor -cook
						-project="${projectAbsDir}/${projectName}.uproject"
						-target=${projectName}
						-unrealexe=${UnrealLaunchPath}
						-platform=Win64 -installed -stage -archive -package -build -pak -iostore -compressed -prereqs
						-archivedirectory="${packagingOutputDir_Shipping}"
						-clientconfig=Shipping
						-nocompile -nocompileuat
				| "${Python3_EXECUTABLE_NATIVE}" "${CMAKE_SOURCE_DIR}/Public/CMake/FixStdoutForVS.py"
				VERBATIM
			)
		endif()
	elseif (APPLE)
		message( "generating unreal project for ${projectAbsDir}/${projectName}.uproject")
		# Mac has a risk of freeze of UnrealBuildTools, try multiple time with timeout
		execute_process (
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Public/CMake/LaunchWithTimeout.py" -r 3 -t 300 "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Mac/Build.sh" -ProjectFiles -Project "${projectAbsDir}/${projectName}.uproject" -WaitMutex
			COMMAND_ERROR_IS_FATAL ANY
		)
		
		# Create a symlink to engine for debugging purpose
		message("createSymlink " "${BE_UNREAL_ENGINE_DIR}" to  "${projectAbsDir}/../Engine")	
		createSymlink ("${BE_UNREAL_ENGINE_DIR}" "${projectAbsDir}/../Engine" addedFiles)
		
		# Create targets linking to xcode proj generated earlier
		if (CMAKE_OSX_ARCHITECTURES)
			add_custom_target( ${projectName}_Game
				COMMAND "xcodebuild" "-quiet" "build" "-scheme" "${projectName}" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName} (Mac).xcodeproj" "-arch" "${CMAKE_OSX_ARCHITECTURES}"
				VERBATIM
			)
		else()
			add_custom_target( ${projectName}_Game
				COMMAND "xcodebuild" "-quiet" "build" "-scheme" "${projectName}" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName} (Mac).xcodeproj"
				VERBATIM
			)
		endif()
		if (NOT FOR_VERACODE)
			add_custom_target (${projectName}_Game_Packaged) # TODO
		endif()
	
		if (CMAKE_OSX_ARCHITECTURES)
			add_custom_target( ${projectName}_Editor 
				COMMAND "xcodebuild" "-quiet" "build" "-scheme" "${projectName}Editor" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName}Editor (Mac).xcodeproj" "-arch" "${CMAKE_OSX_ARCHITECTURES}"
				VERBATIM
			)
		else()
			add_custom_target( ${projectName}_Editor 
				COMMAND "xcodebuild" "-quiet" "build" "-scheme" "${projectName}Editor" "-configuration" "$<$<CONFIG:UnrealDebug>:DebugGame>$<$<CONFIG:Release>:Development>" "-project" "${projectAbsDir}/Intermediate/ProjectFiles/${projectName}Editor (Mac).xcodeproj"
				VERBATIM
			)
		endif()
		if (NOT FOR_VERACODE)
			# Add a target that will build the Shipping version of the app.
			add_custom_target (${projectName}_Shipping ALL
				# This target is only compatible with CMake Release config, since it uses Release extern binaries.
				COMMAND "$<$<CONFIG:Release>:${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles/Mac/Build.sh;${projectName};Mac;Shipping;-Project=${projectAbsDir}/${projectName}.uproject;-WaitMutex>"
				COMMAND "$<$<NOT:$<CONFIG:Release>>:${CMAKE_COMMAND};-E;echo;Target not compatible with this config, skipping.>"
				COMMAND_EXPAND_LISTS
				VERBATIM
				)
			add_custom_target (${projectName}_Shipping_Packaged) # TODO
		endif()
		add_custom_target( Run_${projectName}_Editor
			COMMAND "${BE_UNREAL_ENGINE_DIR}/Binaries/Mac/UnrealEditor" "${projectAbsDir}/${projectName}.uproject"
			WORKING_DIRECTORY "${BE_UNREAL_ENGINE_DIR}/.."
		)
		add_dependencies (Run_${projectName}_Editor ${projectName}_Editor)
		set_target_properties (Run_${projectName}_Editor  PROPERTIES FOLDER "UnrealProjects")
		
	endif()
	
	# Add dependencies to all external libs used by the project.
	if (projectDependencies)
		add_dependencies (${projectName}_Editor ${projectDependencies})
		if (TARGET ${projectName}_Editor_NoUnity)
			add_dependencies (${projectName}_Editor_NoUnity ${projectDependencies})
		endif()
		add_dependencies (${projectName}_Game ${projectDependencies})
		if (NOT FOR_VERACODE)
			add_dependencies (${projectName}_Shipping ${projectDependencies})
		endif()
	endif ()
	# The *_packaged targets will launch the Editor build, hence these dependencies.
	if (NOT FOR_VERACODE)
		add_dependencies (${projectName}_Game_Packaged ${projectName}_Editor)
		add_dependencies (${projectName}_Shipping_Packaged ${projectName}_Editor)
	endif ()
	
	set_target_properties (${projectName}_Editor PROPERTIES FOLDER "UnrealProjects")
	if (TARGET ${projectName}_Editor_NoUnity)
		set_target_properties (${projectName}_Editor_NoUnity PROPERTIES FOLDER "UnrealProjects/NoUnity")
	endif()
	set_target_properties (${projectName}_Game PROPERTIES FOLDER "UnrealProjects/Game")
	if (NOT FOR_VERACODE)
		set_target_properties (${projectName}_Game_Packaged PROPERTIES FOLDER "UnrealProjects/Game")
		set_target_properties (${projectName}_Shipping PROPERTIES FOLDER "UnrealProjects/Shipping")
		set_target_properties (${projectName}_Shipping_Packaged PROPERTIES FOLDER "UnrealProjects/Shipping")
	endif ()
	
	# Update the BuildUnrealProjects target so that it will build this project too.
	if (NOT TARGET BuildUnrealProjects)
		add_custom_target (BuildUnrealProjects ALL)
	endif ()
	add_dependencies (BuildUnrealProjects ${projectName}_Editor)
	add_dependencies (BuildUnrealProjects ${projectName}_Game)
	# "Shipping" config is very long to build so for now it is disabled in Check builds.
	#add_dependencies (BuildUnrealProjects ${projectName}_Shipping)
	set_target_properties (BuildUnrealProjects PROPERTIES FOLDER "UnrealProjects/Detail")
	# Update the target that will build all XXX_NoUnity targets.
	# We create a separate target here (BuildUnrealProjects_NoUnity) so that we can ensure the ADO agent
	# builds the targets in this order:
	# - XXX_NoUnity
	# - XXX (this will overwrite the binaries built by XXX_NoUnity)
	# - RunTests_XXX, which depends on XXX, which is up-to-date.
	if (NOT TARGET BuildUnrealProjects_NoUnity)
		add_custom_target (BuildUnrealProjects_NoUnity ALL)
	endif ()
	if (TARGET ${projectName}_Editor_NoUnity)
		add_dependencies (BuildUnrealProjects_NoUnity ${projectName}_Editor_NoUnity)
	endif ()
	set_target_properties (BuildUnrealProjects_NoUnity PROPERTIES FOLDER "UnrealProjects/Detail")
	
	if (NOT funcArgs_NO_TEST AND NOT FOR_VERACODE)
		# Add a target to run the tests.
		# We are actually adding 2 targets:
		# - Target RunTests_XXX that runs the tests for project XXX as you would expect.
		# - Target RunTestsChained_XXX that runs tests for project XXX, but also depends on previously added RunTestsChained_YYY target.
		#   This creates a linear dependency chain betwenn all RunTestsChained_XXX targets, allowing the ADO agent to run the tests sequentially.
		#   This is required to avoid random errors due to several Unreal test processes running in parallel
		#   (eg. simultaneous access to cesium sqlite database used for web request caching).
		foreach (chainedFlag "" "Chained")
			add_custom_target (RunTests${chainedFlag}_${projectName} ALL # "ALL" so that target is enabled and MSBuild can build it.
				# UnrealEditor.exe loads the Development binaries (this config is mapped to CMake Release config).
				# UnrealEditor-Win64-DebugGame.exe loads the DebugGame binaries (this config is mapped to CMake Debug config).
				# Other CMake configs (if any) are not supported, so we add a dummy command that will try to run an unexisting command with an explicit name.
				COMMAND "$<$<NOT:$<CONFIG:UnrealDebug,Release>>:TARGET_NOT_COMPATIBLE_WITH_THIS_CONFIG>"
				# Add -nosound option to avoid weird errors in FAudioDevice::Teardown() at end of tests on some machines,
				# which causes the target to fail even if all tests succeed.
				COMMAND "${UnrealLaunchPath}" "${projectAbsDir}/${projectName}.uproject" "-ExecCmds=\"Automation RunTests Bentley${extraTests};Quit\"" -unattended -nopause -editor -stdout -nosound -nullrhi
			)
			add_dependencies (RunTests${chainedFlag}_${projectName} ${projectName}_Editor)
		endforeach ()
		add_custom_target (RunTests_AssetVerification_${projectName} ALL # "ALL" so that target is enabled and MSBuild can build it.
				# UnrealEditor.exe loads the Development binaries (this config is mapped to CMake Release config).
				# UnrealEditor-Win64-DebugGame.exe loads the DebugGame binaries (this config is mapped to CMake Debug config).
				# Other CMake configs (if any) are not supported, so we add a dummy command that will try to run an unexisting command with an explicit name.
				COMMAND "$<$<NOT:$<CONFIG:UnrealDebug,Release>>:TARGET_NOT_COMPATIBLE_WITH_THIS_CONFIG>"
				# Add -nosound option to avoid weird errors in FAudioDevice::Teardown() at end of tests on some machines,
				# which causes the target to fail even if all tests succeed.
				COMMAND "${UnrealLaunchPath}" "${projectAbsDir}/${projectName}.uproject" "-ExecCmds=\"Automation RunTest Editor.Python.iTwinEngage.test_VerifyMeshes+Editor.Python.iTwinEngage.test_VerifyTextures;Quit\"" -unattended -nopause -editor -stdout -nosound -nullrhi
				)
		set_target_properties (RunTests_${projectName} PROPERTIES FOLDER "UnrealProjects/Tests")
		set_target_properties (RunTests_AssetVerification_${projectName} PROPERTIES FOLDER "UnrealProjects/Tests")
		set_target_properties (RunTestsChained_${projectName} PROPERTIES FOLDER "UnrealProjects/Detail/TestsChained")
		# Add a target that will make sure all required build targets are complete before running the first chained test.
		# This ensures that all XXX_Editor targets are built before running the first chained test.
		# This avoids having a (potentially cpu intensive) XXX_Editor being built while running RunTestsChained_YYY.
		if (NOT TARGET PrepareUnrealTests)
			add_custom_target (PrepareUnrealTests ALL)
			set_target_properties (PrepareUnrealTests PROPERTIES FOLDER "UnrealProjects/Detail")
		endif ()
		add_dependencies (RunTestsChained_${projectName} PrepareUnrealTests)
		# Gather all dependencies of RunTests_XXX (eg. XXX_Editor) and add them to PrepareUnrealTests.
		# Thus, building PrepareUnrealTests will make sure required build targets are complete.
		get_property (testDeps TARGET RunTests_${projectName} PROPERTY MANUALLY_ADDED_DEPENDENCIES)
		foreach (testDep ${testDeps})
			add_dependencies (PrepareUnrealTests ${testDep})
		endforeach ()
		# Update dependency chain between tests.
		get_property (RunUnrealTests_CurrentDependency GLOBAL PROPERTY "RunUnrealTests_CurrentDependency")
		if (RunUnrealTests_CurrentDependency)
			add_dependencies (RunTestsChained_${projectName} ${RunUnrealTests_CurrentDependency})
		endif ()
		set_property (GLOBAL PROPERTY "RunUnrealTests_CurrentDependency" RunTestsChained_${projectName})
		# Add the target that will run all chained tests.
		if (NOT TARGET RunUnrealTests)
			add_custom_target (RunUnrealTests ALL)
			set_target_properties (RunUnrealTests PROPERTIES FOLDER "UnrealProjects/Detail")
		endif ()
		add_dependencies (RunUnrealTests RunTestsChained_${projectName})
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