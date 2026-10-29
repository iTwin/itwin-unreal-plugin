/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRendering.Build.cs $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using UnrealBuildTool;

// Small module holding global shaders and scene view extensions used by ITwinRuntime.
// It must be loaded at PostConfigInit (see ITwinForUnreal.uplugin) because global shader types
// have to be registered before the engine compiles the global shader map.
public class ITwinRendering : ModuleRules
{
	public ITwinRendering(ReadOnlyTargetRules Target) : base(Target)
	{
		bWarningsAsErrors = true;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[]{
			"Core"
		});
		PrivateDependencyModuleNames.AddRange(new string[]{
			"CoreUObject",
			"Engine",
			"Projects",
			"RenderCore",
			"Renderer",
			"RHI",
		});
	}
}
