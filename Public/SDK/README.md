
# Common prerequisites

- CMake
- git
- vcpkg:
	- ```git clone https://github.com/microsoft/vcpkg.git```
	- ```cd vcpkg && bootstrap-vcpkg.bat```
 
# Build

## Windows
### Prerequisites
- Visual Stusio 2022

Add VCPKG_ROOT env variable that contains the path of vcpkg repository globably in your system (or use ```set VCPKG_ROOT=...```)
You can also set the VCPKG_ROOT as a cmake variable. (add```-DVCPKG_ROOT=...``` to cmake command )
 
```
mkdir build
cd build
cmake --preset win64static -B. <repopath>/UnrealSandbox\Public\SDK
```
open CarrotSDK.sln in visual studio

