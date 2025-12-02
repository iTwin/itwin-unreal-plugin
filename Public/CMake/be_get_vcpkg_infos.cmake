# The goal of this script is to retrieve informations about the vcpkg packages we use.
# Such informations are:
# - the list of packages we use
# - for each package, the list of its dependencies
# - for each package, the list of its source and lib files.
# Retrieveing these infos involves running some vcpkg commands and parse their output.
# Parsing is much simpler in python than in CMake, so what we do is run a python script
# that does all the job and generates files that are used to set CMake variables,
# which can be used later eg. in be_add_unreal_project().
# For example, the list of dependencies of package catch2 will be written by the python script
# to a file named "DEPENDENCIES_catch2", whose content is then set to a CMake variable named
# BE_VCPKG_DEPENDENCIES_catch2.
execute_process (
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Public/GetVcpkgInfos.py"
		"{\
\"vcpkgExe\":\"${Z_VCPKG_EXECUTABLE}\",\
\"triplet\":\"${VCPKG_TARGET_TRIPLET}\",\
\"installedDir\":\"${VCPKG_INSTALLED_DIR}\",\
\"sourceDir\":\"${CMAKE_SOURCE_DIR}\",\
\"outDir\":\"${CMAKE_BINARY_DIR}/_beVcpkgInfos\"\
}"
	COMMAND_ERROR_IS_FATAL ANY
)
file (GLOB vcpkgInfoItems RELATIVE "${CMAKE_BINARY_DIR}/_beVcpkgInfos" "${CMAKE_BINARY_DIR}/_beVcpkgInfos/*")
foreach (vcpkgInfoItem ${vcpkgInfoItems})
	file (READ "${CMAKE_BINARY_DIR}/_beVcpkgInfos/${vcpkgInfoItem}" BE_VCPKG_${vcpkgInfoItem})
endforeach ()
