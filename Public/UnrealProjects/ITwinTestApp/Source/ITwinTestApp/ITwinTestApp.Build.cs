/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestApp.Build.cs $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using System.IO;
using UnrealBuildTool;

public class ITwinTestApp : ModuleRules
{
	public ITwinTestApp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDefinitions.AddRange(new string[]{
			"NOMINMAX",
		});
		PublicDependencyModuleNames.AddRange(new string[]{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"HTTPServer",
			"InputCore",
			"ITwinRuntime",
			"Json",
			"PlatformCryptoOpenSSL",
			"WebBrowser",
		});
		PrivateDependencyModuleNames.AddRange(new string[]{
            "Slate",
            "SlateCore",
            "SunPosition",
            "RenderCore",
        });
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../ThirdParty/Include"));
	}
}
