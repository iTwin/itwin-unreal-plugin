/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestApp.Target.cs $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using UnrealBuildTool;
using System.Collections.Generic;

public class ITwinTestAppTarget : TargetRules
{
	public ITwinTestAppTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
	}
}
