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
		PrivateDefinitions.AddRange(new string[]{
			"NOMINMAX",
		});
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
			"PlatformCryptoOpenSSL",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
        });
		string libFolder = "Debug";
		string libPostfix = ".lib";
		string libPrefix = "";
        string libSuffix = "d";
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            libPostfix = ".a";
            libPrefix = "lib";
        }

       if (Target.Configuration == UnrealTargetConfiguration.Development ||
			Target.Configuration == UnrealTargetConfiguration.Shipping)

		{
			libFolder = "Release";
			libSuffix = "";
        }
        PublicAdditionalLibraries.AddRange(new string[]{
			Path.Combine(ModuleDirectory, "../ThirdParty/Lib",libFolder, libPrefix + "BeUtils" + libSuffix + libPostfix)
		});
	}
}
