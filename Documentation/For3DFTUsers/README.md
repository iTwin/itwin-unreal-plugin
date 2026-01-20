# Transitioning a project from 3DFT to the iTwin for Unreal plugin

If you would like to transition a project which uses the (now deprecated) [3DFT plugin](https://github.com/iTwin/unreal-engine-3dft-plugin) to the new iTwin for Unreal plugin, please follow these stepes.

1. Make sure your project is using Unreal Engine version 5.3.<br>
   See "Change a Project's Unreal Engine Version" [here](https://dev.epicgames.com/documentation/en-us/unreal-engine/managing-game-code-in-unreal-engine?application_version=5.3).
2. Remove the dependency to the 3DFT plugin. This is done by following these steps:
   - If the 3DFT plugin folder is located inside your project:
     1. Make sure your project is not open in Unreal Editor.
     2. Remove the folder `Plugins\iTwin` from your project.
   - If the 3DFT plugin folder is located inside in the Unreal Engine folder:<br>
     In the Unreal Editor, disable the `iTwin` plugin as explained [here](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-plugins-in-unreal-engine?application_version=5.3).
3. Make sure your project is not open in the Unreal Editor.
4. If not done yet, install the iTwin for Unreal plugin as described on the [Installation Readme](https://github.com/iTwin/itwin-unreal-plugin/tree/main/Documentation/ForDevelopers/).
5. Remove the folders `Binaries` and `Intermediate` that may exist at the root of your project's folder.
6. If your project contains C++ code, these additional steps are needed:
   - Enable C++20 support; this can be done by adding/updating this line in your *.Target.cs files:<br>
     `DefaultBuildSettings = BuildSettingsVersion.V4;`
   - Open your .uproject file in a text editor, and remove any dependency on module `iTwin` inside the `Modules/AdditionalDependencies` section.
   - Some classes have been renamed (eg. `AiModel` -> `AITwinIModel`), you may need to update your code accordingly.<br>
     Please refer to file `ITwinForUnreal/Config/BaseITwinForUnreal.ini` for the list of changes.
7. Modify your app so that it provides the iTwin app ID to the plugin, as explained in the ["using the plugin in a new project"](#use-plugin-in-new-project) section.

Now, if you open your .uproject in the Unreal Editor, it should (build and) run without any error.

