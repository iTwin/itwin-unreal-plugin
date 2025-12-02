// Copyright 2020-2024 CesiumGS, Inc. and Contributors

using UnrealBuildTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using Microsoft.Extensions.Logging;

public class CesiumRuntime : ModuleRules
{
    public CesiumRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "../ThirdParty/include")
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
              Path.Combine(GetModuleDirectory("Renderer"), "Private"),
              Path.Combine(GetModuleDirectory("Renderer"), "Internal")
            }
        );

        string platform;
        string libExtension;
        string libPrefix;
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            platform = "Windows-AMD64-";
            libPrefix = "";
            libExtension = ".lib";
        }
        else
        {
            libPrefix = "lib";
            libExtension = ".a";
            if (Target.Platform == UnrealTargetPlatform.Mac)
            {
                platform = "Darwin-universal-";
                PublicFrameworks.Add("SystemConfiguration");
            }
            else if (Target.Platform == UnrealTargetPlatform.Android)
            {
                platform = "Android-aarch64-";
            }
            else if (Target.Platform == UnrealTargetPlatform.Linux)
            {
                platform = "Linux-x86_64-";
            }
            else if (Target.Platform == UnrealTargetPlatform.IOS)
            {
                platform = "iOS-ARM64-";
            }
            else
            {
                throw new InvalidOperationException("Cesium for Unreal does not support this platform.");
            }
        }

        string libPathBase = Path.Combine(ModuleDirectory, "../ThirdParty/lib/" + platform);
        string libPathDebug = libPathBase + "Debug";
        string libPathRelease = libPathBase + "Release";

        bool useDebug = false;
        if (Target.Configuration == UnrealTargetConfiguration.Debug)
        {
            if (Directory.Exists(libPathDebug))
            {
                useDebug = true;
            }
        }

        string libPath = useDebug ? libPathDebug : libPathRelease;
        string[] allCesiumLibs = Directory.Exists(libPath) // We can safely glob that
            ? Directory.GetFiles(libPath, libPrefix + "Cesium*" + libExtension) : new string[0];
        if (!Directory.Exists(libPath))
        {
			Logger.LogInformation($"iTwinForUnreal/CesiumRuntime: Cannot find cesium-native libraries (OK when running CMake and CesiumDependencies has not been built yet): {libPath}");
        }
        PublicAdditionalLibraries.AddRange(allCesiumLibs);
        string[] allAbseilLibs = Directory.Exists(libPath) // these too, there are a lot...
            ? Directory.GetFiles(libPath, libPrefix + "absl_*" + libExtension) : new string[0];
        PublicAdditionalLibraries.AddRange(allAbseilLibs);
        // AdvViz: Don't glob the rest, we would get ALL libraries built by vcpkg, be them for this module
        // or for other modules in the project, like ITwinRuntime... Or even libs not linked with any
        // Unreal module (side DLLs, ffmpeg executable deps...), because of the way we link everything built
        // by vcpkg. This for example caused link errors because of ffmpeg's dependency 'avcodec' which
        // probably conflicted with an Unreal Engine version of the same...
        string[] allOtherLibs = {
            "ada",
            "asmjit",
            "astcenc-avx2-static",
            "async++",
            "blend2d",
            "brotlicommon",
            "brotlidec",
            "brotlienc",
            "draco",
            "fmt",
            "glm",
            "jpeg",
            "ktx",
            "libmodpbase64",
            "libsharpyuv",
            "libwebp",
            "libwebpdecoder",
            "libwebpdemux",
            "libwebpmux",
            "meshoptimizer",
            "s2",
            "spdlog",
            "sqlite3",
            "tidy_static",
            "tinyxml2",
            "turbojpeg",
            "zlibstatic-ng",
            "zstd",
        };
        foreach (string libName in allOtherLibs)
        {
            PublicAdditionalLibraries.Add(Path.Combine(libPath, libPrefix + libName + libExtension));
        }
        // On Linux, cpp-httplib uses getaddrinfo_a, which is in the anl library.
        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicSystemLibraries.Add("anl");
        }
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "RHI",
                "CoreUObject",
                "Engine",
                "MeshDescription",
                "StaticMeshDescription",
                "HTTP",
                "LevelSequence",
                "Projects",
                "RenderCore",
                "SunPosition",
                "DeveloperSettings",
                "UMG",
                "Renderer",
                "OpenSSL",
                "Json",
                "JsonUtilities",
                "Slate",
                "SlateCore"
            }
        );

        // Use UE's MikkTSpace on most platforms, except Android and iOS.
        // On those platforms, UE's isn't available, so we use our own.
        if (Target.Platform != UnrealTargetPlatform.Android && Target.Platform != UnrealTargetPlatform.IOS)
        {
            PrivateDependencyModuleNames.Add("MikkTSpace");
        }
        else
        {
            PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../ThirdParty/include/mikktspace"));
        }

        PublicDefinitions.AddRange(
            new string[]
            {
                "SPDLOG_COMPILED_LIB",
                "LIBASYNC_STATIC",
                "GLM_FORCE_XYZW_ONLY",
                "GLM_FORCE_EXPLICIT_CTOR",
                "GLM_ENABLE_EXPERIMENTAL",
                "TIDY_STATIC",
                "URI_STATIC_BUILD",
                "SWL_VARIANT_NO_CONSTEXPR_EMPLACE",
                // Define to record the state of every tile, every frame, to a SQLite database.
                // The database will be found in [Project Dir]/Saved/CesiumDebugTileStateDatabase.
                // "CESIUM_DEBUG_TILE_STATES",
            }
        );

        PrivateDependencyModuleNames.Add("Chaos");

        if (Target.bBuildEditor == true)
        {
            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "UnrealEd",
                    "WorldBrowser",
                    "ContentBrowser",
                    "MaterialEditor"
                }
            );
        }

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
                // ... add any modules that your module loads dynamically here ...
            }
        );

        ShadowVariableWarningLevel = WarningLevel.Off;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        CppStandard = CppStandardVersion.Cpp20;
        bEnableExceptions = true;
    }
}
