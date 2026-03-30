/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.Build.cs $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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
		bWarningsAsErrors = true;
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
			"Chaos",
			"CinematicCamera",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"Foliage",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"PakFile",
			"RenderCore",
			"RHI",
			"SDKCore",
			"Slate",
			"SlateCore",
            "UMG",
            "UE5Coro",
		});
		if (Target.Version.MinorVersion == 5) // ie Unreal 5.5
		{
			PrivateDependencyModuleNames.Add("PlatformCryptoOpenSSL");
		}
		else // Unreal 5.6(+)
		{
			PrivateDependencyModuleNames.AddRange(new string[]{
				"PlatformCrypto",
				"PlatformCryptoContext",
				"PlatformCryptoTypes"
			});
		}
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("CQTest");
		}
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
		string libSuffix = "ud";
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			libExtension = ".a";
			libPrefix = "lib";
		}
		if (Target.Configuration == UnrealTargetConfiguration.Development ||
			Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			libFolder = "Release";
			libSuffix = "";
		}
		else if (Target.Configuration == UnrealTargetConfiguration.Debug)
		{
			libFolder = "Debug";
			libSuffix = "d";
		}
		PublicAdditionalLibraries.AddRange(new string[]{
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "BeUtils" + libSuffix + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "yyjson" + libExtension),
		});
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PublicAdditionalLibraries.AddRange(new string[]{
				Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "httpmockserver" + libSuffix + libExtension),
				Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, "libmicrohttpd" + libExtension),
			});
		}
	}
}
