/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUserContentTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Content/ITwinContentManager.h>
#include <Population/ITwinPopulation.h>
#include <Editor.h>
#include <Engine/World.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformFileManager.h>
#include <HAL/PlatformProcess.h>
#include <IPlatformFilePak.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <UObject/StrongObjectPtr.h>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinPakMountFailureTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.UserContent.PakMountFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinPakMountFailureTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UITwinContentManager> Manager(NewObject<UITwinContentManager>());
	const FString Id(TEXT("/CC/aaaaaaaa/bbbbbbbb/Test"));
	const FString Asset = Id + TEXT("/Game/Test");
	const FString Missing = FPaths::CreateTempFilename(*FPaths::AutomationTransientDir(), TEXT("missing"), TEXT(".pak"));
	TestFalse(TEXT("Missing pak fails"), Manager->MountPak(Missing, Id));
	TestEqual(TEXT("Failure does not register download"), Manager->ShouldDownloadComponent(Asset), Id);

	// No platform file initialized: an existing file must not hide that failure either.
	const FString File = FPaths::CreateTempFilename(*FPaths::AutomationTransientDir(), TEXT("pak"), TEXT(".pak"));
	FFileHelper::SaveStringToFile(TEXT("not a pak"), *File);
	TestFalse(TEXT("Unavailable pak platform file fails"), Manager->MountPak(File, Id));
	TestEqual(TEXT("Failure remains retryable"), Manager->ShouldDownloadComponent(Asset), Id);
	Manager->InitializePakPlatformFile();
	TestFalse(TEXT("Actual platform mount rejects a corrupt pak"), Manager->MountPak(File, Id));
	TestEqual(TEXT("Mount failure does not register download"), Manager->ShouldDownloadComponent(Asset), Id);
	Manager->DeinitializePakPlatformFile();
	TestFalse(TEXT("Retry after deinitialization still fails"), Manager->MountPak(File, Id));
	IFileManager::Get().Delete(*File);

	TestTrue(TEXT("Metadata-only fake pak still succeeds"), Manager->MountPak(TEXT(""), Id));
	TestTrue(TEXT("Fake pak registers the id"), Manager->ShouldDownloadComponent(Asset).IsEmpty());
	return true;
}

#if PLATFORM_WINDOWS
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinPakMountLifecycleTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.UserContent.PakMountLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinPakMountLifecycleTest::RunTest(const FString& Parameters)
{
	const FString UnrealPak = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64/UnrealPak.exe")));
	if (!FPaths::FileExists(UnrealPak))
	{
		AddInfo(FString::Printf(TEXT("Skipping PakMountLifecycle coverage: UnrealPak is unavailable at %s"), *UnrealPak));
		return true;
	}

	// Generate a tiny non-asset pak in Automation/Tmp; no binary fixture is committed.
	// Use an absolute path: MountPak mounts the full path, and FPakPlatformFile::Unmount compares
	// filenames verbatim, so a relative Automation dir (engine and project on one drive) would not match.
	const FString Folder = FPaths::ConvertRelativePathToFull(
		FPaths::CreateTempFilename(*FPaths::AutomationTransientDir(), TEXT("mount")));
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FString Payload = FPaths::Combine(Folder, TEXT("probe.txt"));
	const FString Response = FPaths::Combine(Folder, TEXT("response.txt"));
	const FString Pak = FPaths::Combine(Folder, TEXT("probe.pak"));
	FFileHelper::SaveStringToFile(TEXT("pak mount probe"), *Payload);
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("\"%s\" \"../../../iTwinEngage/Content/UserContentMountProbe.txt\""),
		*Payload), *Response);
	int32 ReturnCode = -1;
	FString StdOut, StdErr;
	const bool bExecuted = FPlatformProcess::ExecProcess(*UnrealPak,
		*FString::Printf(TEXT("\"%s\" -Create=\"%s\""), *Pak, *Response), &ReturnCode, &StdOut, &StdErr);
	if (!TestTrue(TEXT("Create temporary pak using installed UnrealPak"), bExecuted && ReturnCode == 0))
	{
		AddError(StdOut + StdErr);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
		return false;
	}

	TStrongObjectPtr<UITwinContentManager> Manager(NewObject<UITwinContentManager>());
	Manager->InitializePakPlatformFile();
	auto* PlatformFile = static_cast<FPakPlatformFile*>(FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")));
	if (!TestNotNull(TEXT("Pak platform file"), PlatformFile))
	{
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
		return false;
	}
	auto MountCount = [&]()
	{
		TArray<FString> Mounted;
		PlatformFile->GetMountedPakFilenames(Mounted);
		return Mounted.FilterByPredicate([&](const FString& Path)
		{
			return FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Path), Pak);
		}).Num();
	};
	TestTrue(TEXT("Real pak mounts"), Manager->MountPak(Pak, TEXT("/CC/aaaaaaaa/bbbbbbbb/First")));
	TestTrue(TEXT("Same pak can register another component without remounting"),
		Manager->MountPak(Pak, TEXT("/CC/aaaaaaaa/bbbbbbbb/Second")));
	TestEqual(TEXT("Only one actual mount"), MountCount(), 1);
	TestTrue(TEXT("Second component registered"), Manager->ShouldDownloadComponent(
		TEXT("/CC/aaaaaaaa/bbbbbbbb/Second/Game/Test")).IsEmpty());
	TestTrue(TEXT("External unmount"), PlatformFile->Unmount(*Pak));
	TestEqual(TEXT("Actually unmounted"), MountCount(), 0);
	TestTrue(TEXT("Retry after external unmount mounts again"), Manager->MountPak(Pak));
	TestEqual(TEXT("Not hidden by a stale cache"), MountCount(), 1);
	Manager->DeinitializePakPlatformFile();
	Manager->InitializePakPlatformFile();
	TestTrue(TEXT("Mount after reinitialization succeeds"), Manager->MountPak(Pak));
	PlatformFile = static_cast<FPakPlatformFile*>(FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")));
	TestEqual(TEXT("No duplicate after reinitialization"), MountCount(), 1);
	PlatformFile->Unmount(*Pak);
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}
#endif

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinPopulationComponentTypeTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.UserContent.ComponentType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinPopulationComponentTypeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world"), World))
		return false;
	const FString Prefix(TEXT("/CC/aaaaaaaa/bbbbbbbb/Character Vehicle Crane Vegetation ClippingPlane ClippingBox"));
	struct FCase { const TCHAR* Asset; EITwinInstantiatedObjectType Type; };
	for (const FCase& Case : {
		FCase{ TEXT("/Game/CarrotLibrary/Vehicles/Bus"), EITwinInstantiatedObjectType::Vehicle },
		FCase{ TEXT("/Game/CarrotLibrary/Construction/Crane001"), EITwinInstantiatedObjectType::Crane },
		FCase{ TEXT("/Game/CarrotLibrary/Characters/Person"), EITwinInstantiatedObjectType::Character },
		FCase{ TEXT("/Game/CarrotLibrary/Vegetation/Tree"), EITwinInstantiatedObjectType::Vegetation },
		FCase{ TEXT("/Game/CarrotLibrary/Clipping/ClippingPlane"), EITwinInstantiatedObjectType::ClippingPlane },
		FCase{ TEXT("/Game/CarrotLibrary/Clipping/ClippingBox"), EITwinInstantiatedObjectType::ClippingBox },
		FCase{ TEXT("/Game/CarrotLibrary/UserContent/Prop"), EITwinInstantiatedObjectType::Other } })
	{
		auto* Population = World->SpawnActor<AITwinPopulation>();
		if (!TestNotNull(TEXT("Population"), Population))
			return false;
		const FString Reference = Prefix + Case.Asset;
		Population->SetObjectRef(TCHAR_TO_UTF8(*Reference));
		TestTrue(*Reference, Population->GetObjectType() == Case.Type);
		TestEqual(TEXT("Full component reference is preserved"), UTF8_TO_TCHAR(Population->GetObjectRef().c_str()), *Reference);
		Population->Destroy();
	}
	return true;
}

#endif
