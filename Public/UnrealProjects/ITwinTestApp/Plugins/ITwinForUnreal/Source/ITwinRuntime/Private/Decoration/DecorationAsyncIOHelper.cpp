/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationAsyncIOHelper.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Decoration/DecorationAsyncIOHelper.h>
#include <Decoration/ITwinDecorationServiceSettings.h>
#include <ITwinIModel.h>
#include <ITwinServerConnection.h>

#include <Kismet/GameplayStatics.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Config.h"
#	include "SDK/Core/Visualization/MaterialPersistence.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	void InitDecorationServiceConnection(const UObject* WorldContextObject)
	{
		// Initialize the connection to the decoration service
		static bool needInitConfig = true;
		if (needInitConfig)
		{
			EITwinEnvironment Env = EITwinEnvironment::Prod;

			// Override it through environment variable, if applicable
			FString bentleyEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("BENTLEY_ENV"));
			if (bentleyEnv == "DEV")
			{
				Env = EITwinEnvironment::Dev;
			}
			else if (bentleyEnv == "QA")
			{
				Env = EITwinEnvironment::QA;
			}

			// Deduce environment from current iTwin authorization, if any.
			AITwinServerConnection const* ServerConnection = Cast<AITwinServerConnection const>(
				UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinServerConnection::StaticClass()));
			if (ServerConnection
				&& ServerConnection->Environment != EITwinEnvironment::Invalid)
			{
				Env = ServerConnection->Environment;
			}

			UITwinDecorationServiceSettings const* DecoSettings = GetDefault<UITwinDecorationServiceSettings>();

			SDK::Core::Config::SConfig sconfig;

			if (DecoSettings->UseLocalServer)
			{
				sconfig.server.server = "localhost";
				sconfig.server.port = DecoSettings->LocalServerPort;
				sconfig.server.urlapiprefix = "/advviz/v1";
			}
			else
			{
				// use our sandbox server (temporary solution for the YII)
				if (Env == EITwinEnvironment::Prod)
				{
					sconfig.server.server = "https://itwindecoration-eus.bentley.com";
				}
				else
				{
					sconfig.server.server = "https://qa-itwindecoration-eus.bentley.com";
				}
				sconfig.server.urlapiprefix = "/advviz/v1";

				//if (Env == EITwinEnvironment::Dev)
				//{
				//	sconfig.server.server = "https://dev-api.bentley.com";
				//}
				//else
				//{
				//	sconfig.server.server = "https://api.bentley.com";
				//}
				//sconfig.server.urlapiprefix = "/";
			}

			if (!DecoSettings->CustomServer.IsEmpty())
			{
				sconfig.server.server = TCHAR_TO_UTF8(*DecoSettings->CustomServer);
			}
			if (!DecoSettings->CustomUrlApiPrefix.IsEmpty())
			{
				sconfig.server.urlapiprefix = TCHAR_TO_UTF8(*DecoSettings->CustomUrlApiPrefix);
			}
			SDK::Core::Config::Init(sconfig);
			needInitConfig = false;
		}
	}
}

void FDecorationAsyncIOHelper::SetLoadedITwinInfo(FITwinLoadInfo const& InLoadedITwinInfo)
{
	LoadedITwinInfo = InLoadedITwinInfo;
}

FITwinLoadInfo const& FDecorationAsyncIOHelper::GetLoadedITwinInfo() const
{
	return LoadedITwinInfo;
}

void FDecorationAsyncIOHelper::RequestStop()
{
	*shouldStop = true;
}

bool FDecorationAsyncIOHelper::IsInitialized() const
{
	return (decoration && instancesManager && materialPersistenceMngr);
}

void FDecorationAsyncIOHelper::InitDecorationService(const UObject* WorldContextObject)
{
	if (decoration && instancesManager && materialPersistenceMngr)
	{
		// Already done.
		return;
	}
	ITwin::InitDecorationServiceConnection(WorldContextObject);

	decoration = SDK::Core::IDecoration::New();
	decorationITwin = std::make_shared<FString>();

	instancesManager = SDK::Core::IInstancesManager::New();

	instancesGroup = SDK::Core::IInstancesGroup::New();
	instancesGroup->SetName("InstGroup");
	instancesManager->AddInstancesGroup(instancesGroup);

	/***   TEMPORARY CODE FOR MATERIAL PERSISTENCE   ***/
	materialPersistenceMngr = std::make_shared<SDK::Core::MaterialPersistenceManager>();
	AITwinIModel::SetMaterialPersistenceManager(materialPersistenceMngr);
	/***   --------------------------------------    ***/
	scene = SDK::Core::IScenePersistence::New();
}


bool FDecorationAsyncIOHelper::LoadITwinDecoration(std::string const& accessToken)
{
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to load any decoration"));
		return false;
	}
	if (!decoration)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty())
		return false;

	if (decoration
		&& !decoration->GetId().empty()
		&& decorationITwin
		&& *decorationITwin == LoadedITwinInfo.ITwinId)
	{
		// Decoration already loaded for current iTwin => nothing to do.
		return true;
	}

	// Get the decoration associated with the current ITwin
	std::string itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));
	std::vector<std::shared_ptr<SDK::Core::IDecoration>> decorations =
		SDK::Core::GetITwinDecorations(itwinid, accessToken);

	if (decorations.empty() || !decorations[0] ||
		decorations[0]->GetId().empty())
		return false;

	if (*shouldStop)
	{
		BE_LOGI("ITwinDecoration", "aborted load decoration task - would select decoration " << decoration->GetId() << " for itwin " << itwinid);
		return false;
	}
	decoration = decorations[0];
	*decorationITwin = LoadedITwinInfo.ITwinId;

	BE_LOGI("ITwinDecoration", "Selected decoration " << decoration->GetId() << " for itwin " << itwinid);

	return true;
}

bool FDecorationAsyncIOHelper::LoadPopulationsFromServer(std::string const& accessToken)
{
	if (!instancesManager)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (!LoadITwinDecoration(accessToken))
	{
		return false;
	}
	instancesManager->LoadDataFromServer(decoration->GetId(), accessToken);
	return true;
}

bool FDecorationAsyncIOHelper::LoadCustomMaterials(std::string const& accessToken)
{
	if (!materialPersistenceMngr)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	// [TEMPORARY - FOR YII]
	// Load material customizations from the Decoration Service
	if (!LoadITwinDecoration(accessToken))
	{
		return false;
	}
	materialPersistenceMngr->LoadDataFromServer(decoration->GetId(), accessToken);
	return true;
}

bool FDecorationAsyncIOHelper::SaveDecorationToServer(std::string const& accessToken)
{
	bool const saveInstances = instancesManager	&& instancesManager->HasInstancesToSave();
	bool const saveMaterials = materialPersistenceMngr && materialPersistenceMngr->NeedUpdateDB();
	if (!saveInstances && !saveMaterials)
	{
		return false;
	}
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to save decoration"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty() || !decoration)
	{
		return false;
	}
	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));

	if (decoration->GetId().empty())
	{
		decoration->Create("Decoration", itwinid, accessToken);
	}

	if (*shouldStop)
	{
		BE_LOGI("ITwinDecoration", "aborted save decoration task for itwin " << itwinid);
		return false;
	}

	if (!decoration->GetId().empty())
	{
		BE_LOGI("ITwinDecoration", "Saving decoration " << decoration->GetId()
			<< " for itwin " << itwinid << "...");

		if (saveInstances)
			instancesManager->SaveDataOnServer(decoration->GetId(), accessToken);
		if (saveMaterials)
			materialPersistenceMngr->SaveDataOnServer(decoration->GetId(), accessToken);

		return true;
	}
	return false;
}

bool FDecorationAsyncIOHelper::LoadSceneFromServer(std::string const& accessToken)
{
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to load any decoration"));
		return false;
	}
	if (!scene)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty())
		return false;

	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));

	if (scene
		&& !scene->GetId().empty()
		&& scene->GetITwinId() == itwinid)
	{
		// scene already loaded for current iTwin => nothing to do.
		return true;
	}
	using namespace SDK::Core;
	std::vector<std::shared_ptr<IScenePersistence>> scenes  = GetITwinScenes(itwinid, accessToken);
	if (scenes.empty())
	{
		scene->Create("default scene", itwinid, accessToken);
		return false;
	}
	else
	{
		//todo choose one scene
		scene = scenes[0];
		return true;
	}

}

bool FDecorationAsyncIOHelper::SaveSceneToServer(std::string const& accessToken)
{
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to save decoration"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty() || !scene)
	{
		return false;
	}
	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));
	if (scene->GetId().empty())
	{
		scene->Create("default scene", itwinid, accessToken);
	}
	else if(scene->ShoudlSave())
	{
		scene->Save(accessToken);
	}
	return true;
}
