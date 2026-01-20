/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestAppEditor.Target.cs $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using UnrealBuildTool;
using System.Collections.Generic;

public class ITwinTestAppEditorTarget : TargetRules
{
	public ITwinTestAppEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		bUseAdaptiveUnityBuild = false;
	}
}
