/*--------------------------------------------------------------------------------------+
|
|     $Source: SDKCore.Build.cs $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

using System.IO;
using UnrealBuildTool;
using System;
using static SDKCore;

public class SDKCore : ModuleRules
{
    public class BluidInfo
    {
        public string buildFolder { get; set; }
    }

    public SDKCore(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;       
		CppStandard = CppStandardVersion.Cpp20;

        string buildInfoPath = Path.Combine(ModuleDirectory, "../ThirdParty/BuildInfo.txt"); //is in ThirdParty because excluded in gitignore
        string buildFolder = File.ReadAllText(buildInfoPath);
        buildFolder = buildFolder.Replace("\n", "");
        buildFolder = buildFolder.Replace("\r", "");
        Console.WriteLine("buildFolder:'"+buildFolder+"'");

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../../../../../../SDK"));
        PublicIncludePaths.Add(Path.Combine(buildFolder, "_deps/reflect-cpp-src/include"));

        string libFolder = "UnrealDebug";
        string libPostfix = ".lib";
        string libPrefix = "";
        string libSuffix = "";
        string configFolder = "UnrealDebug";
        string vcpkgTriplet = "x64-windows-static-md-release";
        if (Target.Configuration == UnrealTargetConfiguration.Development ||
			Target.Configuration == UnrealTargetConfiguration.Shipping)
        {
            configFolder = "Release";
        }
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            libPostfix = ".a";
            libPrefix = "lib";
            vcpkgTriplet = "arm64-osx";
        }
        // Add any import libraries or static libraries
        PublicAdditionalLibraries.Add(Path.Combine(buildFolder, "Public/SDK/Core/Visualization/", configFolder , libPrefix + "Visualization" + libSuffix + libPostfix));
        PublicAdditionalLibraries.Add(Path.Combine(buildFolder, "_deps/reflect-cpp-build", configFolder, libPrefix + "reflectcpp" + libSuffix + libPostfix));

        string vcpkgFolder = Path.Combine(buildFolder, "vcpkg_installed/" + vcpkgTriplet + "/lib");
        PublicAdditionalLibraries.Add(Path.Combine(vcpkgFolder, libPrefix + "cpr" + libSuffix + libPostfix));
        PublicAdditionalLibraries.Add(Path.Combine(vcpkgFolder, libPrefix + "libcurl" + libSuffix + libPostfix ));
        PublicAdditionalLibraries.Add(Path.Combine(vcpkgFolder, libPrefix + "zlib" + libSuffix + libPostfix ));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.Add("crypt32.lib");
        }

    }
}
