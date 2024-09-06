/*--------------------------------------------------------------------------------------+
|
|     $Source: SDKCore.Build.cs $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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
		string libFolder = "UnrealDebug";
		if (Target.Configuration == UnrealTargetConfiguration.Development ||
			Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			libFolder = "Release";
		}
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			libExtension = ".a";
			libPrefix = "lib";
			libSuffix = "";
		}
		PublicAdditionalLibraries.AddRange(new string[]{
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "Visualization" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "reflectcpp" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "cpr" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "z" + libSuffix + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, "libcurl" + libExtension),
            Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "assert" + libExtension),
            Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "cpptrace" + libExtension),
        });

        if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.AddRange(new string[]{
				Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "dwarf" + libExtension), // dependency of cpptrace for macos
			});
		}

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
