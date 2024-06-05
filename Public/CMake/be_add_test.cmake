# be_add_test : Creates the appropriate targets/commands/dependencies to run a test executable,
# and possibly measure code coverage.
# Usage:
# be_add_test (testTarget
#               [JS]
#               [TIMEOUT timeout]
#               [EXE_ARGS arg arg ...]
#               [COVERED_TARGETS target target ...])
# testTarget: An existing target name (is must be added by be_add_binary beforehand).
# JS: Indicates the test is a javascript file, not an executable.
# OV_EXT: Indicates the test is for an Omniverse extension.
#         This requires that CAYMUS_CREATE_INSTALL_DIR points to a valid OV install directory.
# TIMEOUT: (in seconds) The test executable will be killed if it spends more than the specified time.
#          Default to 10 seconds.
# EXE_ARGS: optional arguments that will be passed to the test executable.
# COVERED_TARGETS: List of existing target names that are covered by this test.
#                  If this argument is given, then code coverage will be measured.
#                  If this argument is not given, then code coverage will be skipped.
#                  Tests will run slower if you specify several covered targets
#                  (execution speed seems to depend on the quatity of covered source code).
#                  Ideally there should be only one target (the lib which is tested by this unit test program).
# Examples:
# - be_add_test (MyLib_UnitTests TIMEOUT 30 EXE_ARGS "hi" "bye" COVERED_TARGETS MyLib)
#   Creates a test target "MyLib_UnitTests" with code coverage enabled.
#   The test executable will be started with 2 arguments: "hi" "bye".
#   The test executable will be killed if it is still running after 30 seconds.
#   Code coverage will be measured for the library built by target "MyLib".
function (be_add_test testTarget)
	cmake_parse_arguments (
		funcArgs
		"JS;OV_EXT"
		"TIMEOUT"
		"EXE_ARGS;COVERED_TARGETS"
		${ARGN}
	)
	set (exeArgs "")
	set (exeEnv "{}")
	# Retrieve test executable path.
	if (${funcArgs_JS})
		# JS test is executed by NodeJS.
		set (exePath "${BE_NODEJS_DIR}/node.exe")
		set (exeArgs "${rushJSWorkDir}/common/scripts/install-run.js" pnpm@${pnpmVersion} pnpm run test)
		# Add nodeJS folder to PATH env, so that pnpm etc are found.
		# A more natural solution would be to use "cmake -E env PATH=..." in the add_custom_target()
		# below, but I was unable to make it work due to the semicolons contained in the path.
		set (exeEnv "{\"PATH\":\"${BE_NODEJS_DIR};$ENV{PATH}\"}")
		# Replace backslashes with forward slashes.
		# Ideally I would prefer escaping backslashes, but for some reason some backslashes are not
		# escaped (the ones preceding a semicolon), leading to error when parsing the json.
		string (REPLACE "\\" "/" exeEnv "${exeEnv}")
	elseif (${funcArgs_OV_EXT})
		set (exePath "${CAYMUS_CREATE_INSTALL_DIR}/kit/kit.exe")
	else ()
		# General case (executable).
		set ( exePath "$<TARGET_FILE:${testTarget}>" )
	endif ()
	# We are going to create a target that runs a python script.
	# There may be many different arguments to this script, and handling all of them with argparse
	# (for example) would be tedious, because there may be arguments starting with "--", or with
	# special characters that we need to handle, etc.
	# So instead, we pass a json string containing all the arguments.
	set (jsonArgs "{\"exePath\":\"${exePath}\"")
	if (DEFINED funcArgs_TIMEOUT)
		set (jsonArgs "${jsonArgs},\"timeout\":${funcArgs_TIMEOUT}")
	endif ()
	if (DEFINED funcArgs_EXE_ARGS)
		list (APPEND exeArgs ${funcArgs_EXE_ARGS})
	endif ()
	toJsonStrList (exeArgsJsonList "${exeArgs}")
	set (jsonArgs "${jsonArgs},\"exeArgs\":${exeArgsJsonList}")
	if ( USE_AVX_EXTENSIONS AND BUILD_MULTI_AVX_ARCHS )
		# to choose between the AVX and NO_AVX version, the script will set the appropriate environment variable
		if ( CPU_SUPPORTS_AVX )
			set (jsonArgs "${jsonArgs},\"avx\":true")
		else ()
			set (jsonArgs "${jsonArgs},\"no_avx\":true")
		endif ()
	endif ()
	# Some tests currently fail on buildbot, they are considered "unsafe".
	# Other tests are "safe".
	# Buildbot validation and code coverage only run the safe tests.
	set (isSafeTest TRUE)
	if (
		FALSE
		# No unsafe test for now. They can be added like this:
		# "${testTarget}" STREQUAL "bentley.anim.timeline"
	)
		message (WARNING "Test ${testTarget} execution currently fails on this platform/product, and is thus disabled on the buildbot check build. TODO: fix it.")
		set (isSafeTest FALSE)
	endif ()
	if (BE_CODE_COVERAGE AND DEFINED funcArgs_COVERED_TARGETS AND ${isSafeTest})
		set (jsonArgs "${jsonArgs},\
\"coverageOutDir\":\"${beCodeCoverageOutDirBase}/${testTarget}\",\
\"coverageToolExe\":\"${COVERAGE_TOOL_EXE}\"\
")
		set (targetBinPaths "")
		set (sourceDirs "")
		foreach (target ${funcArgs_COVERED_TARGETS})
			list (APPEND targetBinPaths "$<TARGET_FILE:${target}>")
			# Retrieve the source dir for the covered target.
			# This will be added to the source filter for the coverage tool.
			# We need to filter the sources for performance reasons.
			# Without filter (or with a filter matching our entire repo), coverage can be very slow.
			get_property (srcDir TARGET ${target} PROPERTY SOURCE_DIR)
			list (APPEND sourceDirs "${srcDir}")
		endforeach ()
		toJsonStrList (jsonList "${targetBinPaths}")
		set (jsonArgs "${jsonArgs},\"coverageModules\":${jsonList}")
		toJsonStrList (jsonList "${sourceDirs}")
		set (jsonArgs "${jsonArgs},\"coverageSources\":${jsonList}")
		set_property (GLOBAL APPEND PROPERTY "beCoverageTestTargets" ${testTarget})
	endif ()
	# For Omniverse extensions, add standard failure patterns.
	if (${funcArgs_OV_EXT})
		toJsonStrList (jsonList "*[[]error]*;*[[]fatal]*")
		set (jsonArgs "${jsonArgs},\"failurePatterns\":${jsonList}")
	endif ()
	set (jsonArgs "${jsonArgs},\"env\":${exeEnv}")
	set (jsonArgs "${jsonArgs}}") # Closing brace
	# Hack to prevent bash' "brace expansion" on mac (curiously the issue does not occur on linux):
	# (http://www.gnu.org/software/bash/manual/bash.html#Brace-Expansion).
	# The problem is that bash performs brace expansion,
	# which breaks argument parsing in the python script.
	# The VERBATIM argument in add_custom_target() is supposed to prevent this,
	# but obviously it does not (cmake issue?).
	# So what we do is add a backslash before every opening curly brace,
	# so that bash does not do any expansion.
	# Then in the python script we remove these extra backslashes.
	string (REPLACE "{" "\\{" jsonArgs "${jsonArgs}")
	# Add targets & dependencies.
	# Add a test target that will only run the given test executable.
	set (runTestTarget "Run_${testTarget}")
	if (${funcArgs_JS})
		# JS tests must be executed in the project's working directory (ie. the directory containing package.json).
		set (workDir "${rushJSWorkDir}/${testTarget}")
	else ()
		set (workDir "${CMAKE_BINARY_DIR}")
	endif ()
	add_custom_target (${runTestTarget}
		${ModifiedEnvToRunInternalTools} ${ModifiedEnvToRunInternalToolsBis} ${ModifiedEnvToRunInternalToolsTer} ${ModifiedEnvToRunInternalToolsQuater}
		"${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Public/RunTests.py" "${jsonArgs}"
		WORKING_DIRECTORY "${workDir}"
		VERBATIM
	)
	set_target_properties (${runTestTarget} PROPERTIES EXCLUDE_FROM_ALL TRUE)
	set_target_properties (${runTestTarget} PROPERTIES FOLDER UnitTests)
	add_dependencies (${runTestTarget} ${testTarget})
	# Update the dependency chain.
	# For tests that currently fail on bbot, add them in the category "all" (so that they will be executed on the nightly build, but not on the check build).
	# Other tests are considered "safe" and will be executed on both check and nightly builds.
	if (${isSafeTest})
		add_dependencies (RunSafeTests ${runTestTarget})
	endif ()
	add_dependencies (RunAllTests ${runTestTarget})
	# Add the test to a global variable, that will be used later in FinalizeTests().
	set_property (GLOBAL APPEND PROPERTY "beCompileTestTargets" ${testTarget})
endfunction ()

# Add targets to run all tests.
add_custom_target (RunSafeTests ALL) # This target will be executed on the bbot check build.
set_target_properties (RunSafeTests PROPERTIES FOLDER UnitTests)
add_custom_target (RunAllTests ALL)	# This target will be executed on the bbot nighly build.
set_target_properties (RunAllTests PROPERTIES FOLDER UnitTests)
if (BE_CODE_COVERAGE)
	# Coverage reports will be stored in a configuration-specific folder.
	# If the folder is created by the test runner script, there is a risk that 2 tests try to
	# create the folder simultaneously and thus fail.
	# So, instead of handling this case in the test runner script, we ask our CopyAllLibs step
	# to copy a dummy file to this folder, thus creating it if necessary.
	set (beCodeCoverageOutDirBase "${CMAKE_BINARY_DIR}/CodeCoverage/${ConfigurationName}")
	CopyAllLibs_CopyFileToDir ("${CMAKE_SOURCE_DIR}/cmake/Dummy.txt" "${beCodeCoverageOutDirBase}")
	if (NO_DEBUG_SYMBOLS)
		message (SEND_ERROR "NO_DEBUG_SYMBOLS is not compatible with BE_CODE_COVERAGE")
	endif ()

	find_program (COVERAGE_TOOL_EXE OpenCppCoverage HINTS "C:/Program Files/OpenCppCoverage")
	if (COVERAGE_TOOL_EXE STREQUAL "COVERAGE_TOOL_EXE-NOTFOUND")
		message ( SEND_ERROR "OpenCppCoverage not found while being searched in: ${OpenCppCoverage_SearchDirs}" )
	else ()
		message ( "OpenCppCoverage found in: ${COVERAGE_TOOL_EXE}" )
	endif ()
endif ()

# To be called at the end of the top-level CMakeLists.txt.
# Adds some dependencies on unit test targets (eg. resources).
function (FinalizeTests)
	get_property (beCompileTestTargets GLOBAL PROPERTY "beCompileTestTargets")
	list(LENGTH beCompileTestTargets beCompileTestTargetsLength)
	if (${beCompileTestTargetsLength} EQUAL 0)
		return ()
	endif ()
	if (BE_CODE_COVERAGE)
		# Create a target that will merge all test reports.
		# This is done using a python script, taking a json string as single argument.
		# See comment inside be_add_test() for why we do this.
		set (jsonArgs "{\
\"outputDir\":\"${beCodeCoverageOutDirBase}/_Merged\",\
\"coverageToolExe\":\"${COVERAGE_TOOL_EXE}\"\
")
		get_property (beCoverageTestTargets GLOBAL PROPERTY "beCoverageTestTargets")
		set (inputDirs "")
		set (runTestTargets "")
		foreach (target ${beCoverageTestTargets})
			list (APPEND inputDirs "${beCodeCoverageOutDirBase}/${target}")
			list (APPEND runTestTargets "Run_${target}")
		endforeach ()
		toJsonStrList (jsonList "${inputDirs}")
		set (jsonArgs "${jsonArgs},\"inputDirs\":${jsonList}")
		set (jsonArgs "${jsonArgs}}") # Closing brace
		add_custom_target (MergeCodeCoverageReports
			"${PYTHON_INTERPRETER}" "${CMAKE_SOURCE_DIR}/MergeCodeCoverageReports.py" "${jsonArgs}"
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
			VERBATIM
		)
		add_dependencies (MergeCodeCoverageReports ${runTestTargets})
		add_dependencies (RunSafeTests MergeCodeCoverageReports)
		set_target_properties (MergeCodeCoverageReports PROPERTIES FOLDER UnitTests/Coverage)
	endif ()
endfunction ()
