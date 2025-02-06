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

	const AITwinCesium3DTileset* GetTileset() const;
	AITwinCesium3DTileset* GetMutableTileset();
	bool HasTileset() const;
	void HideTileset(bool bHide);
	bool IsTilesetHidden();
	void SetMaximumScreenSpaceError(double InMaximumScreenSpaceError);
	//Helper of SetMaximumScreenSpaceError :  Adjust the tileset quality, given a percentage (value in range [0;1])
	void SetTilesetQuality(float Value);
	float GetTilesetQuality() const;
	std::optional<FCartographicProps> GetNativeGeoreference() const;

	/// Return true if the required identifiers for loading reality data are all set.
	bool HasRealityDataIdentifiers() const;

		UFUNCTION()
	void OnSceneLoaded(bool success);

	void SetOffset(const FVector &Pos, const FVector& Rot);
	void GetOffset(FVector &Pos, FVector &Rot) const;

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
};
