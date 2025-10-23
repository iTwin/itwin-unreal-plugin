# Installation and usage of the itwin-unreal-plugin

Follow these instructions to install the pre-compiled plugin into Unreal Engine. If you would like to compile the plugin yourself and then install it, please see [the Readme for developers](../ForDevelopers/).

## Supported platforms & requirements
To use the iTwin plugin, you need to create an account at [developer.bentley.com](https://developer.bentley.com). There is a free 90 day trial available. The developer account is required for utilizing the streaming API within the plugin. If your trial period is over and you would like to continue using and testing the plugin, please get in touch with us.<br>
This initial release supports Windows 11; Windows 10 might work, but has not been tested officially (you may conduct tests on Windows 10 yourself if you would like). A version for Mac is in development.<br>
Only iModels and their Saved Views are supported at the moment; Reality Data can be loaded through blueprints only (no GUI is available in the ITwinTestApp for them at the moment).<br>
[Unreal Engine 5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/installing-unreal-engine?application_version=5.3) is the currently supported version. Other Unreal Engine versions will be supported in future updates.<br>

To run Unreal Engine, make sure you are using a dedicated GPU. The performance largely depends on the power of your graphics card. For more information on recommended system specs for Unreal Engine, please visit [Epic's website](https://dev.epicgames.com/documentation/de-de/unreal-engine/hardware-and-software-specifications-for-unreal-engine).

We recorded a quick start video on [YouTube](https://www.youtube.com/watch?v=quf4t4LsqXw). Read on for the written steps of the installation process.

## Installing the pre-compiled plugin
### <a id="configure-itwin-platform"></a> Step 1: Configure access to the iTwin Platform

Before you install the plugin, you need to create and configure your iTwin Platform account:<br>
1. Go to the [iTwin Platform developer portal](https://developer.bentley.com/) and create an account.<br>
2. Go to [My Models](https://developer.bentley.com/my-imodels/) and create a new iModel.<br>
3. Go to [My Apps](https://developer.bentley.com/my-apps/) and [register a new iTwin App](https://developer.bentley.com/tutorials/quickstart-web-and-service-apps/#12-register-your-application):
   - Application type: Native
   - Redirect URIs: http://localhost:3000/signin-callback

### <a id="install-plugin"></a> Step 2: Installing the precompiled iTwin plugin for Unreal

1. Go to the [Releases](https://github.com/iTwin/itwin-unreal-plugin/releases) page.
2. Download ITwinForUnreal.zip from the latest release.
3. Extract the zip archive to Unreal Engine's `Engine/Plugins/Marketplace` folder.<br>
   For example, on Unreal Engine 5.3 on Windows, this is typically `C:\Program Files\Epic Games\UE_5.3\Engine\Plugins\Marketplace`.<br>
   You may need to create the `Marketplace` folder yourself.<br>
   After extraction, you should have a folder `Engine/Plugins/Marketplace/ITwinForUnreal/Binaries` for example.
4. The plugin is now ready to use.

If you do not want to install the plugin in the Unreal Engine folder and instead prefer to put it directly in your Unreal project, just extract the zip archive inside your app's `Plugins` folder (you may need to create this folder).<br>
For example: `C:\MyUnrealApp\Plugins\ITwinForUnreal`.

### Step 3: Using the installed iTwin plugin inside Unreal Engine

1. If you installed the plugin in the Unreal Engine folder, enable the plugin `iTwin for Unreal` as explained [in the Epic documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-plugins-in-unreal-engine).
   - Note that you can have both the Cesium and iTwin plugins for Unreal installed but **not** use them together in the same project! All of the core Cesium features are available through the iTwin plugin by adding the right actors and components (the _CesiumEditor_ UI/UX is not included in the iTwin plugin).
   - Note also that you cannot compile a project using iTwin source code if Cesium is installed, unless you rename the _CesiumForUnreal_'s `uplugin` file, for example appending a `.disabled` suffix to the file, while you are building a project based on the iTwin plugin source code.
2. If not done yet, [configure your iTwin Platform account](#configure-itwin-platform) and take note of your app's client ID.
3. Make sure that:
   - the plugin and its content is visible in the content browser: in the "Settings" menu of the content browser window, enable "Show Engine Content" and "Show Plugin Content".
   - your level does not use a "World Partition": it may work in the Editor but has been known to cause issues when packaging a level as a standalone application.
4. To access your iModels from your Unreal app, you will have to provide the iTwin app ID to the plugin.<br>
   This can be done in several ways:
   - Drag and drop an `ITwinAppIdHelper` actor from the Content Browser into your level.<br>
     This actor can be found in the content browser inside folder `Plugins/iTwin for Unreal C++ Classes/ITwinRuntime/Public`.<br>
     Then, in the actor's Details panel, paste your iTwin app ID inside the field `ITwin/App Id` and validate.<br>
     Now, the plugin will use this app ID whenever this level is loaded.<br>
     This method is useful if you want to simply add an iModel manually into your level inside the Unreal Editor (see below).
   - If you use C++ code or Blueprint, you can instead directly call the static function `AITwinServerConnection::SetITwinAppID()`.<br>
     This is typically done in your app's module `StartupModule()` function (if using C++), or in the `BeginPlay` event of your Game Mode (if using Blueprints).
5. To manually add an iModel into your level inside the Unreal Editor, drag and drop an `ITwinIModel` actor from the Content Browser into your level.<br>
   This actor can be found in the content browser inside folder `Plugins/iTwin for Unreal C++ Classes/ITwinRuntime/Public`.<br>
   Then, in the actor's Details panel, go to the `Loading` section and fill in these fields:
   - `Loading Method`: `Automatic`,
   - `iModel Id`: the ID of your iModel,
   - `Changeset Id`: the ID of the specific changeset you want to import, or `latest` to let the plugin decide which.
Then the iModel should appear in the viewport.

Note: to determine the iModel ID or changeset ID, you can find them in the URL when you choose to visualize an iModel in the online viewer:

![Finding the iModel and changeset IDs](../../docs/Finding-the-IDs.png)

   If the selected iModel/changeset has never been imported yet, the iTwin server needs to convert (ie. export) it into the Cesium format.<br>
   In such case the export will be automatically started, and the "Export Status" label will say "In progress" until the export is complete.<br>
   This can take quite some time, depending on the complexity of your iModel. Once the export is complete, the iModel will appear in the viewport.
   
### <a id="load-external-tileset"></a>Ability to load external tilesets
It is also possible to load "external" tilesets (ie. non-iModels, like tileset coming from Cesium Ion server) by following these steps:
- Drag and drop a `Cesium3DTileset` actor from the Content Browser into your level.<br>
  This actor can be found in the content browser inside folder `Plugins/iTwin for Unreal C++ Classes/CesiumRuntime/Public`.<br>
- In the actor's Details panel, in the `Cesium` section, fill the `Source`, `Url`, `Ion Asset ID`, `Ion Access token` fields depending on the source of your tileset.

## Packaging issues

Many packaging issues are actually build issues, for which there is a dedicated Troubleshooting section in [the developers documentation](../ForDevelopers/)

### Unable to copy file Singleton.dll

The packaging process may try to access a file that is already loaded by the Unreal Editor.
When this happens, you will need to:
* Close the Unreal Editor,
* Delete the `Binaries`, `Intermediate` and `Saved` folders in your project root,
* Force a rebuild of the project with Visual Studio.

## Configuration file
Some advanced settings can be configured in a ".ini" configuration file located at `C:\Users\<YOUR_USERNAME>\AppData\Local\Unreal Engine\Engine\Config\UserEngine.ini`.
Be careful to use the exact subfolders as shown above, as Unreal Engine uses other configuration files at similar locations, but only editing this one will allow the plugin to access the settings.

The setting entries must be added to a `[/Script/ITwinRuntime.ITwinIModelSettings]` section as shown in this example (here with all default values):

```
[/Script/ITwinRuntime.ITwinIModelSettings]
CesiumMaximumCachedMegaBytes=1024
IModelMaximumCachedMegaBytes=4096
IModelCreatePhysicsMeshes=true
TilesetMaximumScreenSpaceError=16.0
Synchro4DMaxTimelineUpdateMilliseconds=50
Synchro4DQueriesDefaultPagination=10000
Synchro4DQueriesBindingsPagination=30000
```
Use a semi-colon `;` at the beginning of a line to comment it out.

See the plugin's `ITwinForUnreal\Source\ITwinRuntime\Public\ITwinIModelSettings.h` file for a detailed documentation of all available settings.