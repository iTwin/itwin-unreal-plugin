/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationHelper.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <Templates/PimplPtr.h>

#include <ITwinLoadInfo.h>

#include <memory>
#include <string>

#include <ITwinDecorationHelper.generated.h>

class FDecorationAsyncIOHelper;
class FViewport;
class AITwinPopulation;
namespace SDK { namespace Core { struct ITwinAtmosphereSettings; } }


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDecorationIODone, bool, bSuccess);

UCLASS()
class ITWINRUNTIME_API AITwinDecorationHelper : public AActor
{
	GENERATED_BODY()

public:
	AITwinDecorationHelper();

	// Callbacks for the different I/O operations

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnDecorationSaved;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnPopulationsLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnMaterialsLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnnSceneLoaded;

	//! Set information about the associated iTwin/iModel
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetLoadedITwinInfo(FITwinLoadInfo InLoadedSceneInfo);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	FITwinLoadInfo GetLoadedITwinInfo() const;


	//! Start loading the decoration attached to current model, if any (asynchronous).
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void LoadDecoration();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationEnabled() const { return bPopulationEnabled; }

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsMaterialEditionEnabled() const { return bMaterialEditionEnabled; }

	//! Start saving the decoration attached to current model, if some modifications were applied.
	//! If bPromptUser is true, a message box will be displayed to confirm he wants to save his editions.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void SaveDecoration(bool bPromptUser = true);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool ShouldSaveDecoration(bool bPromptUser = true) const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SaveDecorationOnExit();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void BeforeCloseLevel();


	//! Permanently deletes all material customizations for current model (cannot be undone!)
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteAllCustomMaterials();

	AITwinPopulation* GetOrCreatePopulation(FString assetPath);

	SDK::Core::ITwinAtmosphereSettings GetAtmosphereSettings() const;
	void SetAtmosphereSettings(const SDK::Core::ITwinAtmosphereSettings&) const;


protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


private:
	AITwinPopulation* GetPopulationToAddInstances();
	
	// Initialize the connection with the decoration service (if needed). This will not try trigger any
	// communication with the server.
	void InitDecorationService();

	std::string GetDecorationAccessToken() const;

	bool HasITwinID() const;

	// Load the decoration associated to the current iTwin, if any.
	bool LoadITwinDecoration(std::string const& accessToken);

	// This will return a copy sharing all data with decorationIO.
	std::shared_ptr<FDecorationAsyncIOHelper> GetDecorationAsyncIOHelper() const;

	void LoadPopulationsInGame(bool bHasLoadedPopulations);

	// Load custom materials (from the Decoration Service, currently).
	bool LoadCustomMaterials(std::string const& accessToken);
	void OnCustomMaterialsLoaded_GameThread(bool bHasLoadedMaterials);

	void OnDecorationSaved_GameThread(bool bSuccess, bool bHasResetMaterials);

	void OnSceneLoad_GameThread(bool bSuccess);
	void OnCloseRequested(FViewport* Viewport);



	// For writing and saving
	std::shared_ptr<FDecorationAsyncIOHelper> decorationIO;
	class FImpl;
	TPimplPtr<FImpl> Impl;

	// Initially, both Population and Material edition are disabled, until we have loaded the corresponding
	// information (which can be empty of course) from the decoration service.
	bool bPopulationEnabled = false;
	bool bMaterialEditionEnabled = false;
};
