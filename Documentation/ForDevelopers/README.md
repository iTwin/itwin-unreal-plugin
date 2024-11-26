# Developing with Unreal Engine: Compiling the plugin yourself

If you prefer to not use the pre-compiled version of the plugin, you can compile the plugin yourself instead and also add custom source code optionally.

## Supported platforms & requirements
To use the iTwin plugin, you need to create an account at [developer.bentley.com](https://developer.bentley.com). There is a free 90 day trial available. The developer account is required for utilizing the streaming API within the plugin. If your trial period is over and you would like to continue using and testing the plugin, please get in touch with us.<br>
This initial release supports Windows 11; Windows 10 might work, but has not been tested officially (you may conduct tests on Windows 10 yourself if you would like). A version for Mac is in development.<br>
Only iModels and their Saved Views are supported at the moment; Reality Data can be loaded through blueprints only (no GUI is available in the ITwinTestApp for them at the moment).<br>
[Unreal Engine 5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/installing-unreal-engine?application_version=5.3) is the currently supported version. Other Unreal Engine versions will be supported in future updates.<br>

To run Unreal Engine, make sure you are using a dedicated GPU. The performance largely depends on the power of your graphics card. For more information on recommended system specs for Unreal Engine, please visit [Epic's website](https://dev.epicgames.com/documentation/de-de/unreal-engine/hardware-and-software-specifications-for-unreal-engine).


## Prerequisites

- [CMake 3.28 or newer](https://cmake.org/download/)
- [Python 3.9 or newer](https://www.python.org/downloads/)
- [Visual Studio 2022](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.3)

### <a id="configure-itwin-platform"></a> Configure access to the iTwin Platform

Before you install the plugin, you need to create and configure your iTwin Platform account:<br>
1. Go to the [iTwin Platform developer portal](https://developer.bentley.com/) and create an account.<br>
2. Go to [My Models](https://developer.bentley.com/my-imodels/) and create a new iModel.<br>
3. Go to [My Apps](https://developer.bentley.com/my-apps/) and [register a new iTwin App](https://developer.bentley.com/tutorials/quickstart-web-and-service-apps/#12-register-your-application):
   - Application type: Native
   - Redirect URIs: http://localhost:3000/signin-callback

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

1. remove your cmake Build folder
2. call `git clean -dfX` in your source folder


