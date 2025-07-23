option(BE_PROJECT_NAME "" "")

if (APPLE)
	if(EXISTS "/Users/Shared/Epic Games/UE_5.3/Engine" AND NOT EXISTS "/Applications/Epic Games/UE_5.3/Engine")
		set (default_UE_PATH "/Users/Shared/Epic Games/UE_5.3/Engine")
	else ()
		set (default_UE_PATH "/Applications/Epic Games/UE_5.3/Engine")
	endif ()
else ()
	set (default_UE_PATH "C:/Program Files/Epic Games/UE_5.3/Engine")
endif ()
mark_as_advanced (default_UE_PATH)

if (NOT DEFINED BE_UNREAL_ENGINE_DIR)
	if (DEFINED ENV{UNREAL_ENGINE_ROOT} AND EXISTS $ENV{UNREAL_ENGINE_ROOT})
		option_path (BE_UNREAL_ENGINE_DIR "Path to Unreal Engine directory" "$ENV{UNREAL_ENGINE_ROOT}")
	else ()
		option_path (BE_UNREAL_ENGINE_DIR "Path to Unreal Engine directory" "${default_UE_PATH}")
	endif ()
endif()

# Note: testing an expected subdir is a way to make sure the right path component was set,
# because the error in case of mistake is not very explicit...
# (eg. "/Users/Shared/Epic Games/UE_5.4/Engine" and not just "/Users/Shared/Epic Games/UE_5.4"!)
if (NOT EXISTS "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles")
	Message(FATAL_ERROR "Can't find 'Build/BatchFiles' subfolder in BE_UNREAL_ENGINE_DIR specified (or defaulted to): ${BE_UNREAL_ENGINE_DIR}")
endif()

be_add_feature_option( Material_Tuning "Allow editing the iModel's materials" "ITwin" ON )
advanced_option_path(BE_VCPKG_BINARY_CACHE "Full path to the shared binary cache, currently EONNAS' Exchange/vcpkg_cache, if you don't want to use the default mount point for some reason" "")
