/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestApp.Build.cs $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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
			"CesiumRuntime",
			"ITwinRuntime",
			"Json",
			"WebBrowser",
        });
		if (Target.Version.MinorVersion == 5) // ie Unreal 5.5
		{
			PublicDependencyModuleNames.Add("PlatformCryptoOpenSSL");
		}
		else // Unreal 5.6(+)
		{
			PublicDependencyModuleNames.Add("PlatformCryptoContext");
		}
		PrivateDependencyModuleNames.AddRange(new string[]{
            "Slate",
            "SlateCore",
            "SunPosition",
            "RenderCore",
        });
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../ThirdParty/Include"));
	}
}
