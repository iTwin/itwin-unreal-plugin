# itwin-unreal-plugin

## Supported platforms

Only Windows is supported at the moment.<br>
Only [Unreal Engine 5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/installing-unreal-engine?application_version=5.3) is supported at the moment.

## Supported features

Only iModels and their Saved Views are supported at the moment ; Reality Data can be loaded through blueprints only (no GUI in ITwinTestApp for them at the moment).
4D Schedules are not yet supported in this version.

## <a id="install-plugin"></a> Installing iTwin for Unreal

1. Go to the [Releases](https://github.com/iTwin/itwin-unreal-plugin/releases) page.
2. Download ITwinForUnreal.zip from the latest release.
3. Extract the zip archive to Unreal Engine's `Engine/Plugins/Marketplace` folder.<br>
   For example, on Unreal Engine 5.3 on Windows, this is typically `C:\Program Files\Epic Games\UE_5.\Engine\Plugins\Marketplace`.<br>
   You may need to create the `Marketplace` folder yourself.<br>
   After extraction, you should have a folder `Engine/Plugins/Marketplace/ITwinForUnreal/Binaries` for example.
4. The plugin is now ready to use.

If you do not want to install the plugin in the Unreal Engine folder and instead prefer to put it directly in your Unreal project, just extract the zip archive inside your app's `Plugins` folder (you may need to create this folder).<br>
For example: `C:\MyUnrealApp\Plugins\ITwinForUnreal`.

## Using iTwin for Unreal

### <a id="use-plugin-in-new-project"></a> In a new project

1. If you installed the plugin in the Unreal Engine folder, enable plugin `iTwin for Unreal` as explained [here](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-plugins-in-unreal-engine).
2. If not done yet, [configure your iTwin Platform account](#configure-itwin-platform) and take note of your app's client ID.
3. To access your iModels from your Unreal app, you will have to give the iTwin app ID to the plugin.<br>
   This can be done in several ways:
   - Drag and drop an `ITwinAppIdHelper` actor from the Content Browser into your level.<br>
     This actor can be found in the content browser inside folder `Plugins/iTwin for Unreal C++ Classes/ITwinRuntime/Public`.<br>
     Then in the actor's Details panel, paste you iTwin app ID inside the field `ITwin/App Id` and validate.<br>
     Now, the plugin will use this app ID whenever this level is loaded.<br>
     This method is useful if you want to simply add an iModel manually in your level inside the Unreal Editor (see below).
   - If you use C++ code or Blueprint, you can instead directly call the static function `AITwinServerConnection::SetITwinAppID()`.<br>
     This is typically done in your app's module `StartupModule()` function (if using C++), or in the `BeginPlay` event of your Game Mode (if using Blueprint).
4. To manually add an iModel in your level inside the Unreal Editor, drag and drop an `ITwinIModel` actor from the Content Browser into your level.<br>
   This actor can be found in the content browser inside folder `Plugins/iTwin for Unreal C++ Classes/ITwinRuntime/Public`.<br>
   Then in the actor's Details panel, go to the `Loading` section and fill these fields:
   - `Loading Method`: `Automatic`
   - `iModel Id`: the ID of your iModel
   - `Changeset Id`: the ID of the changeset you want to import
   Then the iModel should appear in the viewport.<br>
   This may take some time if the iModel has never been imported yet (iTwin server needs to convert it to Cesium format).

### In an existing project that uses the [3DFT plugin](https://github.com/iTwin/unreal-engine-3dft-plugin)

1. Make sure your project is using Unreal Engine version 5.3.<br>
   See "Change a Project's Unreal Engine Version" [here](https://dev.epicgames.com/documentation/en-us/unreal-engine/managing-game-code-in-unreal-engine?application_version=5.3).
2. Remove dependency to the 3DFT plugin. This is done by following these steps:
   - If the 3DFT plugin folder is located inside your project:
     1. Make sure your project is not open in Unreal Editor.
     2. Remove folder `Plugins\iTwin` from your project.
   - If the 3DFT plugin folder is located inside in the Unreal Engine folder:<br>
     In Unreal Editor, disable the `iTwin` plugin as explained [here](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-plugins-in-unreal-engine?application_version=5.3).
3. Make sure your project is not open in Unreal Editor.
4. If not done yet, [install the `ITwinForUnreal` plugin](#install-plugin).
5. Remove the folders `Binaries` and `Intermediate` that may exist at the root of your project's folder.
6. If your project contains C++ code, these additional steps are needed:
   - Enable C++20 support, this can be done by adding/updating this line in your *.Target.cs files:<br>
     `DefaultBuildSettings = BuildSettingsVersion.V4;`
   - Open your .uproject file in a text editor, and remove any dependency on module `iTwin` inside the `Modules/AdditionalDependencies` section.
   - Some classes have been renamed (eg. `AiModel` -> `AITwinIModel`), you may need to update your code accordingly.<br>
     Please refer to file `ITwinForUnreal/Config/BaseITwinForUnreal.ini` for the list of changes.
7. Modify your app so that it gives the iTwin app ID to the plugin, as explained in the ["using the plugin in a new project"](#use-plugin-in-new-project) section.

Now, if you open your .uproject in the Unreal Editor, it should (build and) run without error.

### Behavior changes compared to 3DFT plugin

#### Saved views

If you use function `ITwinPositionToUE()` to convert the camera position of a saved view from "iTwin" space to "Unreal" space, you should now pass (0,0,0) to parameter `ModelOrigin`.<br>
This is a "breaking" change compared to the 3DFT plugin, which required to pass the iModel's `ModelCenter` to get correct result.

## <a id="configure-itwin-platform"></a> Configure access to the iTwin Platform

Create and configure your iTwin Platform account.<br>
1. Go to the [iTwin Platform developer portal](https://developer.bentley.com/) and create an account.<br>
2. Go to [My Models](https://developer.bentley.com/my-imodels/) and create a new iModel.<br>
3. Go to [My Apps](https://developer.bentley.com/my-apps/) and [register a new iTwin App](https://developer.bentley.com/tutorials/quickstart-web-and-service-apps/#12-register-your-application):
   - Application type: Desktop / Mobile
   - Redirect URIs: http://localhost:3000/signin-callback
   - Scopes: `savedviews:read savedviews:modify itwins:read imodels:read mesh-export:read mesh-export:modify offline_access realitydata:read`

## Developing with Unreal Engine

### Prerequisites

- [CMake 3.28 or newer](https://cmake.org/download/)
- [Python 3.9 or newer](https://www.python.org/downloads/)
- [Visual Studio 2022](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.3)

### Configure your machine for develoment

Developer Mode for Windows must be enabled, as explained [here](https://learn.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development).

### <a id="configure-itwin-platform"></a> Configure the iTwin app ID

1. If not done yet, [configure your iTwin Platform account](#configure-itwin-platform) and take note of your app's client ID.
2. Configure your iTwin app ID in the plugin:
   File: "\Public\CMake\main.cmake"<br>
   `set (ITWIN_TEST_APP_ID "your_client_id_goes_here")`<br>

### Build and run

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

### Clean Build

1. remove your cmake Build folder
2. call `git clean -dfX` in your source folder

