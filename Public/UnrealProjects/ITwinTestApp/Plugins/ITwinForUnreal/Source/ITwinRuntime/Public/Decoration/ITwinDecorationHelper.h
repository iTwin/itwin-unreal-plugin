/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <Templates/PimplPtr.h>

#include <ITwinLoadInfo.h>

#include <memory>
#include <string>
#include <optional>
#include <vector>

#include <ITwinDecorationHelper.generated.h>

class FDecorationAsyncIOHelper;
class FViewport;
class AITwinPopulation;

namespace SDK::Core {
	struct ITwinAtmosphereSettings;
	struct ITwinSceneSettings;
}

struct ITwinSceneInfo
{
	std::optional<bool> Visibility;
	std::optional<double> Quality;
	std::optional<FTransform> Offset;
};

UENUM(BlueprintType)
enum class EITwinDecorationClientMode : uint8
{
	/**
	 * Unspecified client. Default behavior will be used.
	 */
	Unknown,
	/**
	 * Advanced Visualization Application.
	 */
	AdvVizApp,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDecorationIODone, bool, bSuccess);

UCLASS()
class ITWINRUNTIME_API AITwinDecorationHelper : public AActor
{
	GENERATED_BODY()

public:

	class SaveLocker
	{
	protected:
		SaveLocker();
	public:
		virtual ~SaveLocker();

	};

	AITwinDecorationHelper();

	// Callbacks for the different I/O operations

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnDecorationSaved;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnPopulationsLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnMaterialsLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnSceneLoaded;

	/** Delegate when decoration is fully loaded. */
	FSimpleMulticastDelegate OnDecorationLoaded;


	//! Sets the decoration client mode. Can be used to customize the handling of decorations for specific
	//! usages.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void SetDecorationClientMode(EITwinDecorationClientMode ClientMode);

	//! Returns the current decoration client mode.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	EITwinDecorationClientMode GetDecorationClientMode() const;


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

	//! Returns true if the loading of a decoration is in progress.
	bool IsLoadingDecoration() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationEnabled() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsMaterialEditionEnabled() const;

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

	AITwinPopulation* GetPopulation(FString assetPath) const;
	AITwinPopulation* GetOrCreatePopulation(FString assetPath) const;
	int32 GetPopulationInstanceCount(FString assetPath) const;

	SDK::Core::ITwinAtmosphereSettings GetAtmosphereSettings() const;
	void SetAtmosphereSettings(const SDK::Core::ITwinAtmosphereSettings&) const;

	SDK::Core::ITwinSceneSettings GetSceneSettings() const;
	void SetSceneSettings(const SDK::Core::ITwinSceneSettings& as) const;

	ITwinSceneInfo GetSceneInfo(EITwinModelType ct, const FString& id) const;
	void SetSceneInfo(EITwinModelType ct, const FString& id,const  ITwinSceneInfo&) const;
	void DeleteLoadedScene();

	// return link identifiers found in scene
	std::vector<std::pair<EITwinModelType, FString>> GetLinkedElements() const;

	[[nodiscard]] std::shared_ptr<SaveLocker> LockSave();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


private:
	UFUNCTION()
	void OnIModelLoaded(bool bSuccess, FString StringId);
	UFUNCTION()
	void OnRealityDatalLoaded(bool bSuccess, FString StringId);
	void OnCloseRequested(FViewport* Viewport);


	class FImpl;
	TPimplPtr<FImpl> Impl;

	//save lock
	class SaveLockerImpl;

	void Lock(SaveLockerImpl*);
	void Unlock(SaveLockerImpl*);
};
