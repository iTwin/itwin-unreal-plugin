option(BE_PROJECT_NAME "" "")
option(BE_SECONDARY_APPS "Build rarely used apps (= all but 'iTwin Engage')" ON)
advanced_option(BE_FILL_SHARED_VCPKG_BINARY_CACHE "Fill the shared vcpkg binary cache on V:" OFF)
advanced_option(BE_REBUILD_ALL_VCPKG_FOR_MEND "When non-empty, ignore all vcpkg binary caches so that everything is rebuilt, and a filtered copy of vcpkg's buildtrees folder is prepared. Pass a **relative** path (cwrsync requirement) from the **source** repository root to the folder into which to copy the buildtrees" "")
advanced_option(BE_VCPKG_FILTERED_BUILDTREES_RELPATH "**Relative** path (for consistency with BE_REBUILD_ALL_VCPKG_FOR_MEND) from the **source** repository root to the folder containing the filtered copy of vcpkg buildtrees prepared in advance" "../vcpkg-buildtrees-for-Mend")

# Note: BE_UNREAL_ENGINE_DIR detection/setting moved to detect_unreal_root.cmake
# because it needed to be included before the "project" call in the root CMakeLists

if (NOT EXISTS "${BE_UNREAL_ENGINE_DIR}")
	message(FATAL_ERROR "detect_unreal_root should have been included at this point and BE_UNREAL_ENGINE_DIR be set correctly")
endif()

be_add_feature_option( Material_Tuning "Allow editing the iModel's materials" "ITwin" ON )
be_add_feature_option( BE_COMFY "Enable the Comfy Upscale feature (cook /Game/ComfyUI instead of excluding it)" "ITwin" OFF )
advanced_option_path(BE_VCPKG_BINARY_CACHE "Full path to the shared binary cache, currently EONNAS' Exchange/vcpkg_cache, if you don't want to use the default mount point for some reason" "")
advanced_option (BE_CODE_COVERAGE "Measure code coverage when running unit tests" OFF)
