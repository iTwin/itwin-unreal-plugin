/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.Build.cs $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using System.IO;
using UnrealBuildTool;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

public class ITwinRuntime : ModuleRules
{
	[CommandLine("-beNoUnity", Value = "false")]
	public bool bBeUseUnity = true;

	public ITwinRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		new CommandLineArguments(System.Environment.GetCommandLineArgs()).ApplyTo(this);
		Logger.LogInformation($"bBeUseUnity {GetType().Name} = {bBeUseUnity}");
		bUseUnity = bBeUseUnity;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[]{
			"Core"
		});
		PrivateDependencyModuleNames.AddRange(new string[]{
			"CesiumRuntime",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"Foliage",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"PlatformCryptoOpenSSL",
			"RenderCore",
			"RHI",
			"SDKCore",
			"Slate",
			"SlateCore",
			"UE5Coro",
			"LevelSequence",
			"CinematicCamera",
			"MovieScene",
			"MovieSceneTracks"
		});
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]{
				"FunctionalTesting",
				"Projects",
				"ScreenShotComparisonTools",
				"UnrealEd",
			});
		}
		string libFolder = "UnrealDebug";
		string libExtension = ".lib";
		string libPrefix = "";
		string libSuffix = "d";
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			libExtension = ".a";
			libPrefix = "lib";
		}

        if (Target.Configuration == UnrealTargetConfiguration.Development)
        {
            PublicDependencyModuleNames.AddRange(new string[]{
                "CQTest"
            });
		}
		if (Target.Configuration == UnrealTargetConfiguration.Development ||
			Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			libFolder = "Release";
			libSuffix = "";
		}
		PublicAdditionalLibraries.AddRange(new string[]{
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "BeUtils" + libSuffix + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "cpr" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, "libcurl" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "yyjson" + libExtension),
		});
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PublicAdditionalLibraries.AddRange(new string[]{
				Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "httpmockserver" + libSuffix + libExtension),
				Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, "libmicrohttpd" + libExtension),
			});
		}
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("crypt32.lib"); // for curl
		}
		else
		{
			PublicFrameworks.AddRange(new string[] { "SystemConfiguration" }); //for curl
		}
	}
}
