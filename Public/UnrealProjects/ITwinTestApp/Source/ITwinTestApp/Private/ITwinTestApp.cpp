/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestApp.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinTestApp.h>

#include <ITwinRuntime\Private\Compil\BeforeNonUnrealIncludes.h>
#	include <Core/Visualization/Visualization.h>
#	include <filesystem>
#include <ITwinRuntime\Private\Compil\AfterNonUnrealIncludes.h>

#include <Modules/ModuleManager.h>
#include <ITwinServerConnection.h>
#include <ITwinTestAppConfig/ITwinTestAppConfig.h>


class FITwinGameModuleImpl : public FDefaultGameModuleImpl
{
	using Super = FDefaultGameModuleImpl;

public:
	virtual void StartupModule() override;
};

void FITwinGameModuleImpl::StartupModule()
{
	///// Temporary code to test library link
	using namespace SDK::Core;
	std::filesystem::path filePath("test.conf");
	std::filesystem::remove(filePath);

	{
		std::ofstream f(filePath);
		f << "{\"server\":{\"server\":\"plop\", \"port\":2345, \"urlapiprefix\":\"api/v1\"}}";
	}

	auto config = SDK::Core::Config::LoadFromFile(filePath);
	Config::Init(config);

	/////
	
	
	Super::StartupModule();

	// propagate current App IDs to the ITwin plugin
	AITwinServerConnection::SetITwinAppID(iTwinAppId);
}

IMPLEMENT_PRIMARY_GAME_MODULE( FITwinGameModuleImpl, ITwinTestApp, "ITwinTestApp" );
