# itwin-unreal-plugin

## Supported platforms

Only Windows is supported at the moment.

## Supported features

Only iModels and their Saved Views are supported at the moment ; Reality Data can be loaded through blueprints only (no GUI in ITwinTestApp for them at the moment).
4D Schedules are not yet supported in this version.

## Prerequisites

- [Unreal Engine 5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/installing-unreal-engine?application_version=5.3)
- [CMake 3.28 or newer](https://cmake.org/download/)
- [Python 3.9 or newer](https://www.python.org/downloads/)
- [Visual Studio 2022](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.3)

## Configure your machine for develoment

Developer Mode for Windows must be enabled, as explained [here](https://learn.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development).

## Configure access to the iTwin Platform

1. Create and configure your iTwin Platform account.<br>
    1. Go to the [iTwin Platform developer portal](https://developer.bentley.com/) and create an account.<br>
    2. Go to [My Models](https://developer.bentley.com/my-imodels/) and create a new iModel.<br>
    3. Go to [My Apps](https://developer.bentley.com/my-apps/) and [register a new iTwin App](https://developer.bentley.com/tutorials/quickstart-web-and-service-apps/#12-register-your-application):
        - Application type: Desktop / Mobile
        - Redirect URIs: http://localhost:3000/signin-callback
        - Scopes: `savedviews:read savedviews:modify itwins:read imodels:read mesh-export:read mesh-export:modify offline_access realitydata:read`
2. Configure your iTwin **Client ID** in the plugin:
   File: "\Public\CMake\main.cmake"<br>
   `set (ITWIN_TEST_APP_ID "your_client_id_goes_here")`<br>

## Build and run

1. Clone the vcpkg repo somewhere on your machine.<br>
`git clone https://github.com/microsoft/vcpkg.git`
2. Create an environment variable VCPKG_ROOT pointing to your local vcpkg repo.
   You can also set the VCPKG_ROOT as a cmake variable.
3. Clone the repo and its submodules.<br>
`git clone https://github.com/iTwin/itwin-unreal-plugin.git`<br>
`cd itwin-unreal-plugin`<br>
`git submodule update --init --recursive`
4. Run CMake (command-line or GUI) and generate your build directory.<br>
   You need to select CMake preset "win64static".<br>
   Command-line example, supposing current directory is your repo root:<br>
`cmake -S . -B C:\Build\itwin-unreal-plugin --preset win64static`
5. Open ITwinTestApp.sln (in VS 2022) located inside the generated build directory.
6. Hit F5 to build and run the test app (UnrealProjects/ITwinTestApp in the solution explorer).
7. Once compilation is finished and VS has launched UnrealEditor, hit alt+P (play in editor) to actually start the app.
