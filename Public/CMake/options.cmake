option(BE_PROJECT_NAME "" "")
option(BE_SECONDARY_APPS "Build rarely used apps (= all but 'iTwin Engage')" ON)
advanced_option(BE_FILL_SHARED_VCPKG_BINARY_CACHE "Fill the shared vcpkg binary cache on V:" OFF)

# Note: BE_UNREAL_ENGINE_DIR detection/setting moved to detect_unreal_root.cmake
# because it needed to be included before the "project" call in the root CMakeLists

if (NOT EXISTS "${BE_UNREAL_ENGINE_DIR}")
	message(FATAL_ERROR "detect_unreal_root should have been included at this point and BE_UNREAL_ENGINE_DIR be set correctly")
endif()

be_add_feature_option( Material_Tuning "Allow editing the iModel's materials" "ITwin" ON )
be_add_feature_option( BE_COMFY "Enable the Comfy Upscale feature (cook /Game/ComfyUI instead of excluding it)" "ITwin" OFF )
advanced_option_path(BE_VCPKG_BINARY_CACHE "Full path to the shared binary cache, currently EONNAS' Exchange/vcpkg_cache, if you don't want to use the default mount point for some reason" "")
