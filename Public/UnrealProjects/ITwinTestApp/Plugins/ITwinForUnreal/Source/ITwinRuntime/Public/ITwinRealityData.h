/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRealityData.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <optional>
#include <ITwinServiceActor.h>
#include <Templates/PimplPtr.h>
#include <ITwinRealityData.generated.h>

struct FCartographicProps;
class FITwinTilesetAccess;
class UITwinClippingCustomPrimitiveDataHelper;

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

	UPROPERTY(Category = "iTwin",
		BlueprintAssignable)
	FOnRealityDataLoaded OnRealityDataLoaded;

	std::optional<FCartographicProps> GetNativeGeoreference() const;

	/// Return true if the required identifiers for loading reality data are all set.
	bool HasRealityDataIdentifiers() const;

	UFUNCTION()
	void OnSceneLoaded(bool success);

	bool IsGeolocated() const { return bGeolocated; }

	TUniquePtr<FITwinTilesetAccess> MakeTilesetAccess();

	UITwinClippingCustomPrimitiveDataHelper* GetClippingHelper() const;
	bool MakeClippingHelper();

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void ZoomOnRealityData();

private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	bool bGeolocated;

	class FImpl;
	TPimplPtr<FImpl> Impl;


	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver:
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info) override;

	/// overridden from FITwinDefaultWebServicesObserver
	virtual const TCHAR* GetObserverName() const override;

	class FTilesetAccess;
};
