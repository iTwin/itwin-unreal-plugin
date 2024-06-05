/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestApp.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinTestApp.h>
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
	Super::StartupModule();

	// propagate current App IDs to the ITwin plugin
	AITwinServerConnection::SetITwinAppID(iTwinAppId);
}

IMPLEMENT_PRIMARY_GAME_MODULE( FITwinGameModuleImpl, ITwinTestApp, "ITwinTestApp" );
