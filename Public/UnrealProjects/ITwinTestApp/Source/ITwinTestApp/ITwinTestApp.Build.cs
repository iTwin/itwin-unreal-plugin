/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestApp.Build.cs $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using System.IO;
using UnrealBuildTool;

public class ITwinTestApp : ModuleRules
{
	public ITwinTestApp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        
		PublicDependencyModuleNames.AddRange(new string[]{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"HTTPServer",
			"InputCore",
			"ITwinCesiumRuntime",
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
