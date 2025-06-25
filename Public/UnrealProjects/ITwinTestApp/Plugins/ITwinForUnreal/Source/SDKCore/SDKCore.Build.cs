/*--------------------------------------------------------------------------------------+
|
|     $Source: SDKCore.Build.cs $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using System.IO;
using UnrealBuildTool;

public class SDKCore : ModuleRules
{
	public SDKCore(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;       
		CppStandard = CppStandardVersion.Cpp20;
		PublicIncludePaths.AddRange(new string []{
			Path.Combine(ModuleDirectory, "../ThirdParty/Include/SDK"),
		});
		string libExtension = ".lib";
		string libPrefix = "";
		string libSuffix = "lib";
        string dllExtension = ".dll";
		string libFolder = "UnrealDebug";
		string configDefine = "UNREALDEBUG_CONFIG";
        string libPostfixDynamic = ".lib";
		string dllExePath = Path.Combine(PluginDirectory, "Binaries/Win64/");

		if (Target.Configuration == UnrealTargetConfiguration.Development ||
			Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			libFolder = "Release";
			configDefine = "RELEASE_CONFIG";
		}

		PublicDefinitions.Add(configDefine);

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			libExtension = ".a";
			libPrefix = "lib";
			libSuffix = "";
            dllExtension = ".dylib";
            libPostfixDynamic = ".dylib";
            dllExePath = Path.Combine(PluginDirectory, "Binaries/Mac/");
		}
		PublicAdditionalLibraries.AddRange(new string[]{
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "Visualization" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "reflectcpp" + libExtension),
			//Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "cpr" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "z" + libSuffix + libExtension),
			//Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, "libcurl" + libExtension),
            Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "assert" + libExtension),
            Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "cpptrace" + libExtension),
            Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "Singleton" + libPostfixDynamic),
        });

        if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.AddRange(new string[]{
				Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "dwarf" + libExtension), // dependency of cpptrace for macos
			});
		}

        string singletonDllName = libPrefix + "Singleton" + dllExtension;
        RuntimeDependencies.Add("$(TargetOutputDir)/"+ singletonDllName, Path.Combine(dllExePath, singletonDllName));

        if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("crypt32.lib");
		}
		else
		{
			PublicFrameworks.AddRange(new string[] { "SystemConfiguration" });
        }
	}
}
