set(overlayPortFiles
	"openssl/portfile.cmake"
	"openssl/vcpkg.json"
)
foreach(overlayPort ${overlayPortFiles})
	set(overlayPort "${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/extern/vcpkg-overlays/${overlayPort}")
	configure_file("${overlayPort}.in" "${overlayPort}" @ONLY)
endforeach()