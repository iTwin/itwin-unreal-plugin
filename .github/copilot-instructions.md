# Carrot / iTwin Engage -- Copilot Agent Instructions

## Project Overview

This is the **Carrot** repository (aka **iTwin Engage**), a C++20 project built on **Unreal Engine 5.6**. It integrates Bentley Systems' iTwin platform with Unreal Engine to visualize infrastructure digital twins (iModels, reality meshes, IoT data, saved views, etc.).

The repository is hosted on Azure DevOps at `https://dev.azure.com/bentleycs/e-onsoftware/_git/Carrot`.

### Repository Structure

```
Carrot/
+-- Public/                              # Open-source / shared components
|   +-- SDK/Core/                        # Core SDK libraries (CMake, C++20, engine-agnostic)
|   |   +-- ITwinAPI/                    # iTwin REST API client (ITwinWebServices, Http, etc.)
|   |   +-- Json/                        # JSON utilities (Json::FromString / Json::ToString)
|   |   +-- Network/                     # HTTP/network layer (Http class, HttpRequest)
|   |   +-- Singleton/                   # Singleton pattern utilities
|   |   +-- Visualization/               # 3D visualization logic
|   |   +-- Tools/                       # Build tools, Factory pattern, logging (BE_LOGE, etc.)
|   +-- BeUtils/                         # Bentley utility libraries
|   |   +-- UnitTests/                   # BeUtils unit tests
|   +-- BeHeaders/                       # Shared Bentley headers (Compil/, etc.)
|   +-- CesiumDependencies/              # cesium-native build (3D Tiles, glTF, geospatial)
|   |   +-- build/cesium-native/         # All Cesium* libraries (CMake)
|   +-- UnrealProjects/
|       +-- ITwinTestApp/                # Public test Unreal project
|           +-- Plugins/ITwinForUnreal/  # Main UE plugin (open source)
|               +-- Source/
|                   +-- ITwinRuntime/    # Core runtime module (UITwinWebServices, etc.)
|                   +-- CesiumRuntime/   # Cesium integration module
|                   +-- ThirdParty/      # Copied SDK Core headers for UE consumption
+-- Private/                             # Proprietary / internal components
|   +-- BentleyStaticLib/                # Internal Bentley static libraries
|   +-- CrossLang/                       # Cross-language bridge (C++ <-> TypeScript via gRPC)
|   |   +-- Cpp/CrossLangGrpcCpp/        # C++ gRPC layer
|   |   +-- TS/Backend/                  # TypeScript backend library
|   |   +-- TS/Frontend/                 # TypeScript frontend library
|   |   +-- TestApp/                     # Cross-language test application
|   +-- ITwinStudioApp/                  # iTwin Engage desktop app
|   |   +-- StudioBackendAddon/          # Backend addon (Node.js native module)
|   |   +-- StudioCppUtils/              # C++ utilities for Studio
|   |   +-- StudioMainWindowHook/        # Main window integration
|   |   +-- UnrealWindowHook/            # Unreal viewport embedding
|   |   +-- CrossLang/                   # Studio-specific CrossLang interfaces
|   |   +-- carrot/                      # Build/package/test targets
|   |   +-- Licensing/                   # Licensing module
|   +-- UnrealProjects/
|       +-- Carrot/                      # Main private Unreal project (iTwin Engage)
|       |   +-- Plugins/ITwinForUnreal/  # Plugin copy (with private extensions)
|       +-- UnrealTestApp/               # Private test Unreal project
|       +-- DecorationTests/             # Decoration test project
+-- extern/
    +-- httpmockserver/                  # HTTP mocking library for tests
```

### Key Technologies

| Technology | Usage |
|---|---|
| **C++20** | SDK Core, UE plugin, Studio app |
| **Unreal Engine 5.6** | 3D rendering, editor integration |
| **CMake** | SDK Core libraries, CesiumDependencies |
| **cesium-native** | 3D Tiles streaming, glTF loading, geospatial math |
| **gRPC / Protobuf** | CrossLang C++ <-> TypeScript communication |
| **TypeScript / Node.js** | iTwin Engage backend & frontend (Electron-based) |
| **GoogleTest** | SDK Core unit tests |
| **httpmockserver** | HTTP mocking for SDK Core tests |
| **spdlog** | Logging (static build, `SPDLOG_COMPILED_LIB`) |
| **GLM** | Math library (`GLM_FORCE_XYZW_ONLY`, `GLM_FORCE_EXPLICIT_CTOR`) |
| **UE5Coro** | Coroutine support in UE plugin |
| **CQTest** | UE test framework used in plugin tests |

---

## Coding Standards

### File Header

All Bentley-authored source files MUST use the standard Bentley copyright header:

```cpp
/*--------------------------------------------------------------------------------------+
|
|     $Source: <FileName.ext> $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/
```

### C++ -- SDK Core (CMake libraries under `Public/SDK/Core/`)

- **Standard**: C++20. Use `std::optional`, `std::variant`, `std::unique_ptr`, `std::shared_ptr`, `std::filesystem`, `std::atomic`, `std::shared_mutex`, structured bindings, etc. where appropriate.
- **Guard**: `#pragma once` (no include guards).
- **Namespace**: `AdvViz::SDK` -- all SDK Core public types live in this namespace. Use `MODULE_EXPORT namespace AdvViz::SDK { ... }` in headers.
- **Naming**:
  - Classes/Structs: `PascalCase` (e.g., `ITwinWebServices`, `SavedViewInfo`, `ThreadSafeAccessToken`)
  - Functions/Methods: `PascalCase` (e.g., `GetITwinIModels`, `SetEnvironment`, `IsSuccessful`)
  - Parameters: `camelCase` (e.g., `iTwinId`, `iModelId`, `changesetId`)
  - Member variables: trailing underscore `_` (e.g., `impl_`, `http_`, `env_`, `baseUrl_`, `accessToken_`)
  - Nested namespaces: `lowercase::PascalCase` (e.g., `bentley::StaticLib`)
  - Enums: `E` prefix with `PascalCase` (e.g., `EITwinEnvironment`, `EAsyncCallbackExecutionMode`)
  - Macros: `ADVVIZ_LINK` for DLL export, `BE_LOGE` / `BE_LOGW` for logging
- **Pimpl pattern**: Heavily used -- `class Impl; std::unique_ptr<Impl> impl_;`
- **Factory pattern**: Classes use `Tools::Factory<T>` for extensible construction (e.g., `Http` derives from `Tools::Factory<Http>`).
- **Strings**: Use `std::string` and `std::string const&` for parameters. Prefer `const&` over copies.
- **Error handling**: Use error strings via `GetLastError()` / `ConsumeLastError()` pattern, observer callbacks. Avoid exceptions in SDK code.
- **Comments**: Use `//!` for Doxygen-style member docs, `///` for method docs.
- **Includes**: Use angle-bracket paths for SDK modules: `#include <Core/Json/Json.h>`, `#include <Core/Network/HttpError.h>`, `#include <Core/Tools/Tools.h>`.
- **Logging**: Use `BE_LOGE("tag", "message" << variable)` stream-style logging macros.

### C++ -- Unreal Engine Plugin (`ITwinForUnreal`)

- Follow **Unreal Engine coding standards**:
  - `F` prefix for structs (e.g., `FITwinInfo`, `FSavedViewInfo`, `FEcefLocation`, `FCartographicProps`)
  - `U` prefix for UObjects (e.g., `UITwinWebServices`)
  - `A` prefix for AActors
  - `E` prefix for enums
  - `I` prefix for interfaces
- Use `UPROPERTY()`, `UFUNCTION()`, `USTRUCT()`, `UCLASS()` macros for reflection.
- UE string types: `FString`, `FName`, `FText`.
- UE containers: `TArray`, `TMap`, `TSet`, `TSharedPtr`, `TWeakPtr`, `TStrongObjectPtr`.
- Module API macros: `ITWINRUNTIME_API`, `CESIUMRUNTIME_API`.
- Include order deprecation: All `UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_*` are set to `0` -- use modern UE include ordering.
- Test UE code with the **CQTest** framework or UE automation tests.

### TypeScript (CrossLang / iTwin Engage frontend & backend)

- Follow standard TypeScript conventions.
- CrossLang interfaces generate TypeScript stubs automatically -- do NOT manually edit `*-ts-stub` generated files.
- **Detailed TypeScript guidelines** for the iTwin Engage desktop app (frontend, backend, IPC, React, pnpm) are in a dedicated instructions file: `Private/ITwinStudioApp/carrot/.github/copilot-instructions.md`. Refer to that file for TypeScript-specific architecture, conventions, build commands, and test setup.

---

## Build System

### CMake (SDK Core)

- SDK Core libraries are built via CMake with C++20 enabled.
- Key library targets: `ITwinAPI`, `Json`, `Network`, `Singleton`, `Visualization`, `Tools`.
- Test targets: `JsonTest`, `NetworkTest`, `ToolsTest`, `VisualizationTest`, `Run_BeUtils_UnitTests`.
- CesiumDependencies are built separately via CMake under `Public/CesiumDependencies/build/`.
- Third-party defines: `SPDLOG_COMPILED_LIB=1`, `LIBASYNC_STATIC=1`, `GLM_FORCE_XYZW_ONLY=1`, `GLM_FORCE_EXPLICIT_CTOR=1`, `GLM_ENABLE_EXPERIMENTAL=1`, `TIDY_STATIC=1`, `URI_STATIC_BUILD=1`.

### Unreal Build Tool (UE Plugin)

- The ITwinForUnreal plugin uses `.Build.cs` and `.Target.cs` module files.
- Unreal projects: `ITwinTestApp` (public), `UnrealTestApp` (private), `DecorationTests` (private), `iTwinEngage` / `Carrot` (main private).
- Build configurations: `Editor_NoUnity`, `Game`, `Game_Packaged`, `Shipping`, `Shipping_Packaged`.
- Plugin packagers: `ITwinForUnreal_PluginPackager`, `BeStudioIntegration_PluginPackager`, `MyPlugin_PluginPackager`, `ZPlugin_PluginPackager`.

### CrossLang Build

- CrossLang interfaces generate both C++ and TypeScript bindings from interface definitions.
- Build targets follow the pattern: `<InterfaceName>`, `<InterfaceName>-ts-stub_Build`, `<InterfaceName>-ts-stub_Install`.
- Interfaces: `CrossLangInterface_Carrot_Its`, `CrossLangInterface_Carrot_Unreal`, `CrossLangTestAppInterface1`, `CrossLangTestAppInterface2`.

---

## Testing

| Target | Framework | Scope |
|---|---|---|
| `JsonTest`, `NetworkTest`, `ToolsTest`, `VisualizationTest` | GoogleTest | SDK Core unit tests |
| `Run_BeUtils_UnitTests` | GoogleTest | BeUtils tests |
| `Run_cesium-native-tests` | GoogleTest | cesium-native tests |
| `RunTests_ITwinTestApp` | UE Automation / CQTest | Public UE plugin tests |
| `RunTests_UnrealTestApp` | UE Automation / CQTest | Private UE tests |
| `RunTests_iTwinEngage` | UE Automation / CQTest | Full app tests |
| `RunTestsChained_*` | -- | Sequential test execution |
| `RunTests_AssetVerification_*` | -- | Asset validation |
| `cross-lang-test-app_Test` | Custom | CrossLang integration tests |
| `RunAllTests` / `RunSafeTests` | -- | Aggregate test runners |

- Mock HTTP with `httpmockserver` library in `extern/httpmockserver/`.
- `PrepareTests` and `PrepareUnrealTests` targets set up test environments.

---

## Key Domain Concepts

| Concept | Description |
|---|---|
| **iTwin** | A digital twin container that aggregates iModels, reality data, IoT, etc. |
| **iModel** | A Bentley infrastructure model (bridges, buildings, roads, etc.) |
| **Changeset** | A version/revision of an iModel |
| **SavedView** | A bookmarked camera position + display state in an iTwin |
| **Export** | A 3D Tiles export of an iModel changeset for streaming |
| **3D Tiles** | OGC standard for streaming massive 3D datasets (via cesium-native) |
| **Reality Data / Reality Mesh** | Photogrammetry-derived 3D mesh of real-world environments |
| **ECEF** | Earth-Centered Earth-Fixed coordinate system used for geolocation |
| **CrossLang** | Internal bridge framework for C++ <-> TypeScript interop via gRPC |
| **ECSQLQuery** | iModel query language for querying element properties |

---

## Agent Guidelines

1. **Determine code context first**: Check whether the file belongs to SDK Core (CMake/C++20, `AdvViz::SDK` namespace, STL types) vs. the UE plugin (UBT, UE types and macros) vs. CrossLang (gRPC/TS) before suggesting code.
2. **Respect the Public/Private boundary**: `Public/` contains open-source components. `Private/` contains proprietary code. Never leak Private API details into Public headers.
3. **cesium-native is a dependency, not owned code**: Do not suggest modifications to files under `Public/CesiumDependencies/build/cesium-native/`. Reference its API but do not alter it.
4. **UE plugin code must follow Unreal conventions**: Use UE macros, types, and patterns. Do not use STL containers directly in UE module code unless interfacing with SDK Core via the `ThirdParty/Include/SDK/` bridge headers.
5. **SDK Core must remain engine-agnostic**: Never introduce Unreal Engine dependencies in `Public/SDK/Core/` libraries. All code there must be pure C++20 and STL, within the `AdvViz::SDK` namespace.
6. **Generated files are read-only**: Do not modify files in `Intermediate/` directories, `*.generated.h` files, or `*-ts-stub` generated TypeScript.
7. **Always include the Bentley copyright header**: Every new `.h`, `.cpp`, or `.cs` file must start with the standard Bentley header block shown above.
8. **Use the pimpl pattern in SDK Core**: Classes that manage state should use `class Impl; std::unique_ptr<Impl> impl_;`, consistent with `ITwinWebServices` and other SDK classes.
9. **Use the Factory pattern where applicable**: HTTP and similar extensible classes derive from `Tools::Factory<T>` and may use `Tools::ExtensionSupport`. Follow this pattern for new SDK Core services.
10. **Test alongside implementation**: When adding or modifying SDK Core code, update corresponding GoogleTest cases. When modifying UE plugin code, consider CQTest or UE automation tests.
11. **ThirdParty/Include/SDK/ is a copy**: The UE plugin consumes SDK Core headers via `Source/ThirdParty/Include/SDK/Core/`. These are copies of `Public/SDK/Core/` -- do not diverge them. Edit the source in `Public/SDK/Core/` and keep the copy in sync.
12. **Use SDK logging macros**: In SDK Core code, use `BE_LOGE("tag", ...)`, `BE_LOGW("tag", ...)` stream-style macros from `<Core/Tools/Tools.h>`. Do not use `std::cout` or UE `UE_LOG` in SDK Core.
13. **Keep instruction files ASCII-only**: This file and other `.github/copilot-instructions.md` files are copied across build directories by CMake. Use only ASCII characters (no Unicode em-dashes, arrows, or box-drawing glyphs) to avoid encoding corruption. Use `--` instead of em-dashes, `<->` instead of arrows, and `+--`/`|` instead of tree-drawing characters.
