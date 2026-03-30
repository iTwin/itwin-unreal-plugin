/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRealityData.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <optional>
#include <ITwinModelType.h>
#include <ITwinServiceActor.h>
#include <Templates/PimplPtr.h>
#include <ITwinRealityData.generated.h>

struct FCartographicProps;
class FITwinTilesetAccess;
class UITwinClipping3DTilesetHelper;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRealityDataInfoLoaded, bool, bSuccess, FString, RealityDataId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRealityDataLoaded, bool, bSuccess, FString, RealityDataId);

UCLASS()
class ITWINRUNTIME_API AITwinRealityData : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString RealityDataId;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString ITwinId;

	UPROPERTY(Category = "iTwin",
		BlueprintAssignable)
	FOnRealityDataInfoLoaded OnRealityDataInfoLoaded;

	UPROPERTY(Category = "iTwin",
		BlueprintAssignable)
	FOnRealityDataLoaded OnRealityDataLoaded;



	AITwinRealityData();
	~AITwinRealityData();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateRealityData();
	
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable,
		Meta = (EditCondition = "bGeolocated"))
	void UseAsGeolocation();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void Reset();

	const ACesium3DTileset* GetTileset() const;
	ACesium3DTileset* GetMutableTileset();
	bool HasTileset() const;

	UFUNCTION()
	void OnTilesetLoaded();

	std::optional<FCartographicProps> GetNativeGeoreference() const;

	/// Return true if the required identifiers for loading reality data are all set.
	bool HasRealityDataIdentifiers() const;

	UFUNCTION()
	void OnSceneLoaded(bool success);

	bool IsGeolocated() const { return bGeolocated; }

	TUniquePtr<FITwinTilesetAccess> MakeTilesetAccess();

	ITwin::ModelLink GetModelLink() const {
		return std::make_pair(EITwinModelType::RealityData, RealityDataId);
	}

	UITwinClipping3DTilesetHelper* GetClippingHelper() const;
	bool MakeClippingHelper();

	bool GetBoundingBox(FBox& OutBox, bool bClampOutlandishValues);

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void ZoomOnRealityData();

	/// overridden from IITwinWebServicesObserver:
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info) override;

private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	bool bGeolocated = false;

	class FImpl;
	TPimplPtr<FImpl> Impl;


	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from FITwinDefaultWebServicesObserver
	virtual const TCHAR* GetObserverName() const override;

	class FTilesetAccess;
};
