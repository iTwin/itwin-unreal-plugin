/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.Build.cs $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using System.IO;
using UnrealBuildTool;

public class ITwinRuntime : ModuleRules
{
	public ITwinRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[]{
			"Core",
		});
		PrivateDependencyModuleNames.AddRange(new string[]{
			"ITwinCesiumRuntime",
			"CoreUObject",
			"Engine",
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
		});
		string libFolder = "UnrealDebug";
		string libExtension = ".lib";
		string libPrefix = "";
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			libExtension = ".a";
			libPrefix = "lib";
		}

		if (Target.Configuration == UnrealTargetConfiguration.Development ||
			Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			libFolder = "Release";
		}

		PublicAdditionalLibraries.AddRange(new string[]{
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "BeUtils" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "httpmockserver" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, libPrefix + "cpr" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, "libcurl" + libExtension),
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib", libFolder, "libmicrohttpd" + libExtension),
		});
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
