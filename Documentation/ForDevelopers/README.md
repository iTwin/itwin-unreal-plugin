# Developing with Unreal Engine: Compiling the plugin yourself

If you prefer to not use the pre-compiled version of the plugin, you can compile the plugin yourself instead and also add custom source code optionally.

## Supported platforms & requirements
To use the iTwin plugin, you need to create an account at [developer.bentley.com](https://developer.bentley.com). There is a free 90 day trial available. The developer account is required for utilizing the streaming API within the plugin. If your trial period is over and you would like to continue using and testing the plugin, please get in touch with us.<br>
This initial release supports Windows 11; Windows 10 might work, but has not been tested officially (you may conduct tests on Windows 10 yourself if you would like). A version for Mac is in development.<br>
Only iModels and their Saved Views are supported at the moment; Reality Data can be loaded through blueprints only (no GUI is available in the ITwinTestApp for them at the moment).<br>
[Unreal Engine 5.5](https://dev.epicgames.com/documentation/en-us/unreal-engine/installing-unreal-engine?application_version=5.5) is the currently supported version. Other Unreal Engine versions will be supported in future updates.<br>

To run Unreal Engine, make sure you are using a dedicated GPU. The performance largely depends on the power of your graphics card. For more information on recommended system specs for Unreal Engine, please visit [Epic's website](https://dev.epicgames.com/documentation/de-de/unreal-engine/hardware-and-software-specifications-for-unreal-engine).


## Prerequisites

- [CMake 3.28 or newer 3.xx version (_not_ CMake 4.x)](https://cmake.org/download/)
- [Python 3.9 or newer](https://www.python.org/downloads/)
- [Visual Studio 2022 version **17.14**](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.5)

### <a id="configure-itwin-platform"></a> Configure access to the iTwin Platform

Before you install the plugin, you need to create and configure your iTwin Platform account:<br>
1. Go to the [iTwin Platform developer portal](https://developer.bentley.com/) and create an account.<br>
2. Go to [My Models](https://developer.bentley.com/my-imodels/) and create a new iModel.<br>
3. Go to [My Apps](https://developer.bentley.com/my-apps/) and [register a new iTwin App](https://developer.bentley.com/tutorials/quickstart-web-and-service-apps/#12-register-your-application):
   - Application type: Native
   - Redirect URIs: http://127.0.0.1:3000/signin-callback
      - Note: for more flexibility, you can now customize the value of the port used in that URI (3000 by default). If you do, ensure you call `AITwinServerConnection::SetAuthRedirectUriPort` (through a Blueprint or C++), or, in Editor, instantiate a `ITwinAppIdHelper` actor and modify its `AuthRedirectUriPort` property (you can then save the level for later use).

### Configure your machine for development

Developer Mode for Windows must be enabled, as explained [here](https://learn.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development).

### <a id="configure-itwin-platform"></a> Configure the iTwin app ID

1. If not done yet, [configure your iTwin Platform account](#configure-itwin-platform) and take note of your app's client ID.
2. Configure your iTwin app ID in the plugin:
   File: "\Public\CMake\main.cmake"<br>
   `set (ITWIN_TEST_APP_ID "your_client_id_goes_here")`<br>
   
## Build process

### Build and run on windows

1. Clone the vcpkg repo somewhere on your machine.<br>
`git clone https://github.com/microsoft/vcpkg.git`
2. Create an environment variable VCPKG_ROOT pointing to your local vcpkg repo.
   You can also set the VCPKG_ROOT as a cmake variable.
3. Clone the repo and its submodules.<br>
`git clone https://github.com/iTwin/itwin-unreal-plugin.git`<br>
`cd itwin-unreal-plugin`<br>
`git submodule update --init --recursive`
4. Run CMake (command-line or GUI) and generate your build folder.<br>
   You need to select CMake preset "win64Unreal".<br>
   Command-line example, supposing current directory is your repo root:<br>
`cmake -S . -B C:\Build\itwin-unreal-plugin --preset win64Unreal`
5. Open ITwinTestApp.sln (in VS 2022) located inside the generated build directory.
6. Hit F5 to build and run the test app (UnrealProjects/ITwinTestApp in the solution explorer).
7. Once compilation is finished and VS has launched UnrealEditor, hit alt+P (play in editor) to actually start the app.

### Build and run on MAC ARM

1. Clone the vcpkg repo somewhere on your machine.<br>
`git clone https://github.com/microsoft/vcpkg.git`
2. Create an environment variable VCPKG_ROOT pointing to your local vcpkg repo.
   You can also set the VCPKG_ROOT as a cmake variable.
3. Clone the repo and its submodules.<br>
`git clone https://github.com/iTwin/itwin-unreal-plugin.git`<br>
`cd itwin-unreal-plugin`<br>
`git submodule update --init --recursive`<br>
4. Run CMake (command-line or GUI) and generate your build folder.<br>
   You need to select CMake preset "macosXcodeARM".<br>
   You also have to ensure that clang will compile for arm and not x86.<br>
   Command-line example, supposing current directory is your repo root:<br>
`cmake -S . -B ../Build/itwin-unreal-plugin --preset macosXcodeARM -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_APPLE_SILICON_PROCESSOR=arm64`
5. Build the targets InstallCesiumDependencies and then ITwinTestApp.<br>
   Some of the targets use xcodebuild that does not handle well multithreading, we recommend that you use --parallel 1:<br>
`cmake --build . --parallel 12 --target InstallCesiumDependencies`<br>
`cmake --build . --parallel 1 --target ITwinTestApp`
6. To launch ITwinTestApp in Unreal, use the target Run_ITwinTestApp_Editor:<br>
`cmake  --build . --parallel 1 --target  Run_ITwinTestApp_Editor`
7. Once compilation is finished and cmake has launched UnrealEditor, hit alt+P (play in editor) to actually start the app.


### Clean Build

1. Remove your cmake Build folder
2. Call `git clean -dfX` in your source folder


## Troubleshooting

### Error "401 Unauthorized" or "403 Forbidden"

These errors mean there was a problem with authentication or checking permissions. It can affect some or all requests to the remote services providing the data needed by the plugin. There can be several reasons why this error appears in the logs:

* You do not have sufficient permissions to access the iTwin project
   * Check you user permissions on Bentley Infrastructure Cloud's "Manage your team" page for this iTwin
* Your user is not registered as a Bentley developer, or your Trial period has expired
   * Go to https://developer.bentley.com to comply with this requirement
* Your app ID is no longer valid: app ID's created with a Trial developer account are only valid as long as the Trial period, after which new app ID's must be created.

If you have checked the above and the error still occurs, here a several things you can try:
* Close your Unreal application, and remove all files with extension `.dat` in `C:\Users\<YOUR_USER_NAME>\AppData\Local\Bentley\Cache` before retrying it
   * This seems especially useful when you see occasional `Authorization found - expires in (...)` messages in the logs but with many `[InsufficientPermissions]: userId tag do not match` errors as well.
* Log out from the Bentley account in your web browser, then log back in.

### The namespace (...) already contains a definition for 'CesiumRuntime'

It probably means you have the official _CesiumForUnreal_ plugin installed but, since the _ITwinForUnreal_ plugin itself supplies the _CesiumRuntime_ module, both cannot be present at build time.

A workaround is to rename the _CesiumForUnreal_'s `uplugin` file, for example appending a `.disabled` suffix to the file, while you are building a project based on the iTwin plugin source code.

### Numerous syntax errors, sometimes referring to C++20

When you have many errors like those:
```
UATHelper: Packaging (Windows): C:\Users\(...)\ITwinRuntime\Private\Tests\WebServicesTest.cpp(1111): error C3791: 'this' cannot be explicitly captured when the default capture mode is by copy (=)
UATHelper: Packaging (Windows): C:\Users\(...)\ITwinRuntime\Private\Timeline\SchedulesStructs.h(55): error C7582: 'bUseOriginalColor': default member initializers for bit-fields requires at least '/std:c++20'
```
It means you need to enable C++20 support, which may be needed explicitly typically when porting older projects.
This can be done by adding/updating this line in your `*.Target.cs` files:<br>
     `DefaultBuildSettings = BuildSettingsVersion.V4;`

### Visual Studio 17.12+ and "__has_feature"

_This is only relevant for older versions of this plugin that supported Unreal Engine 5.3_

Unreal Engine 5.3 (and 5.4) no longer work by default since Visual Studio version 17.12, with these errors:

`C4668 ‘__has_feature’ is not defined as a preprocessor macro, replacing ‘0’ with ‘#if/#elif’`
`C4067 Unexpected tokens following preprocessor directive - expected a newline`

A header file must be modified inside your Unreal Engine's installation folder, by default `C:\Program Files\Epic Games\UE_5.3\Engine\Source\Runtime\Core\Public\Experimental\ConcurrentLinearAllocator.h`.
Make the file writable as it is installed read-only, and add these lines near the top of the file, above the line with `#if PLATFORM_HAS_ASAN_INCLUDE` (patch should start at line 27):

```
#ifndef __has_feature
#define __has_feature(x) 0
#endif
```

### Link errors and multiple Visual Studio versions

With this kind of errors, note the mismatch in toolchain versions mentioned (14.36.32546, then later 14.40.33807)
```
79>Using Visual Studio 2022 14.36.32546 toolchain (C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.36.32532) and Windows 10.0.22621.0 SDK (C:\Program Files (x86)\Windows Kits\10).
79>Determining max actions to execute in parallel (6 physical cores, 12 logical cores)
(...)
79>[1/38] Compile [x64] PCH.CesiumRuntime.cpp
79>C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.40.33807\include\yvals_core.h(902): error C2338: static_assert failed: 'error STL1001: Unexpected compiler version, expected MSVC 19.40 or newer.'
```
This happens because Unreal Engine has a list of preferred versions and compilation can fail with newer compiler versions, especially if several versions of the toolchain are installed.
Solutions:
* Uninstall all but the latest toolchain version using Microsoft Visual Studio Installer application (or delete older toolchain folders directly inside `C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC`)
   - Then check that the remaining versions is correctly selected in all files named like this: `Microsoft.VCToolsVersion.v143.default.***`, inside the folder `C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build`
* Alternately, you can modify the `BuildConfiguration.xml` file located in `%APPDATA%\Unreal Engine\UnrealBuildTool` (or `My Documents\Unreal Engine\UnrealBuildTool`) so that it always points to the latest version (or to a fixed version number that you might want to use):
```
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
(...)
<WindowsPlatform>
<Compiler>VisualStudio2022</Compiler>
<CompilerVersion>Latest</CompilerVersion>
</WindowsPlatform>
(...)
</Configuration>
```

### Image size (OBJ) exceeds maximum allowable size

(See also similar issue about EXE size below)

Symptom:
```
..\Plugins\Marketplace\ITwinForUnreal\Intermediate\Build\Win64\x64\UnrealGame\Development\CesiumRuntime\Module.CesiumRuntime.2.cpp.obj : fatal error LNK1248: image size (11E2FF130) exceeds maximum allowable size (FFFFFFFF)
```
Solution: modify (or create) the `BuildConfiguration.xml` file located in `%APPDATA%\Unreal Engine\UnrealBuildTool` (or `My Documents\Unreal Engine\UnrealBuildTool`), using the `NumIncludedBytesPerUnityCPP` as shown below to  reduce the amount of code UnrealBuildTool will collate together to speed up the builds. A value of a few tens of thousands of bytes does not slow down the build very much, and will avoid excessively large OBJ files:
```
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
(...)
<BuildConfiguration>
<NumIncludedBytesPerUnityCPP>30000</NumIncludedBytesPerUnityCPP>
</BuildConfiguration>
(...)
</Configuration>
```

### Invalid file or disk full: cannot seek to (...)

This error is subtly different from the previous one:
```
UATHelper: Packaging (Windows): [13/14] Link [x64] XXXXXXXXXX-Win64-Shipping.exe
UATHelper: Packaging (Windows): ..\Plugins\Marketplace\ITwinForUnreal\Intermediate\Build\Win64\x64\UnrealGame\Shipping\CesiumRuntime\CesiumPrimitiveFeatures.cpp.obj : fatal error LNK1106: invalid file or disk full: cannot seek to 0xA1247918
```
Note that the name of the "xxxx.cpp.obj" file mentioned can be anything, what's important is that the 
Unreal Build Tool is actually trying to link the _executable_ file.

1. A solution is to adjust the project's `YouProject.Target.cs` file to try and reduce the size of the files handled by the linker:
```
if (Target.Configuration == UnrealTargetConfiguration.Shipping)
{
	bUseUnityBuild = false; // smaller OBJs instead of huge Unity OBJs
	bAllowLTCG = false; // avoid excessive linker memory/IO from LTCG
}
```
2. You may have to move the plugin to the project's local plugins folder (and delete its `Intermediate` subfolder) if for some reason the build tool cannot find some of the binary files while the plugin is in the `Marketplace` folder.
3. When building Unreal Engine (entirely!) from its source repository solution, you can also opt for modular linking, for example if you want to keep the `LTCG` optimizations that the above change would disable:
```
// Avoid oversized monolithic executables in non-Shipping builds by using modular linking.
// Only when not building against an installed (binary) engine (BuildEnvironment must be Unique)
// because installed engines do not include UnrealGame import libraries for modular game linking.
if (Target.Platform == UnrealTargetPlatform.Win64
	&& Target.Configuration != UnrealTargetConfiguration.Shipping
	&& BuildEnvironment == TargetBuildEnvironment.Unique)
{
	LinkType = TargetLinkType.Modular;
}
```
