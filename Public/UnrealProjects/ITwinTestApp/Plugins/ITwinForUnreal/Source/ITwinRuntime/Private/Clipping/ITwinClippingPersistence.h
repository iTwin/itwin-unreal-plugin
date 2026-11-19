/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingPersistence.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <memory>
#include <optional>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Types.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include "ITwinClippingPersistence.generated.h"

class UITwinClippingEffectManager;
class AITwinDecorationHelper;
class AITwinPopulation;
class AITwinPopulationTool;

namespace AdvViz::SDK
{
	class IScenePersistence;
}

enum class EITwinClippingPrimitiveType : uint8;


/// Handles persistence management at the Unreal level for the AITwinClippingTool class.
UCLASS()
class UITwinClippingPersistence : public UObject
{
	GENERATED_BODY()
public:
	UITwinClippingPersistence();

	void SetEffectManager(UITwinClippingEffectManager* InEffectManager);
	void Connect(AITwinDecorationHelper* InPersistenceMgr);

	void OnCutoutAdded(EITwinClippingPrimitiveType EffectType, int32 Index,
		std::optional<bool> const& IsLoadingSpline = std::nullopt);
	bool SendToScene(EITwinClippingPrimitiveType EffectType, int32 Index);
	void RemoveFromScene(EITwinClippingPrimitiveType EffectType, int32 Index);

	void UpdateBaseInfo(EITwinClippingPrimitiveType EffectType, int32 Index);
	void UpdateInversionInfo(EITwinClippingPrimitiveType EffectType, int32 Index);
	void UpdateBox(int32 Index);
	void UpdatePlane(int32 Index);
	void UpdatePolygonTransform(int32 Index, const AdvViz::SDK::dmat3x4& Transform);
	void UpdatePolygonPoint(int32 Index, size_t PointIndex, const AdvViz::SDK::double3& Position);

	bool IsLoadingSceneCutoutsInGame() const { return bIsLoadingSceneAPICutoutsInGame; }

	/// Check if there are cutouts loaded from the Scene API.
	bool DoesLoadedSceneContainCutouts() const;

private:
	bool CheckPersistenceMgr() const;
	std::shared_ptr<AdvViz::SDK::IScenePersistence> GetScene() const;

	inline bool IsValidEffectIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const;
	inline bool CanSendToScene(EITwinClippingPrimitiveType EffectType, int32 Index) const;
	void LoadSceneAPICutoutInstancesInGame();

	bool PreLoadCutoutPrimitive(EITwinClippingPrimitiveType PrimitiveType);
	uint32 PreLoadClippingPrimitives();
	inline TWeakObjectPtr<AITwinPopulation> GetPopulation(EITwinClippingPrimitiveType Type) const;
	inline bool IsValidPopulationIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const;

	UFUNCTION()
	void OnSceneLoaded(bool bSuccess);

	UFUNCTION()
	void OnPopulationsLoaded(bool bSuccess);

private:
	UPROPERTY()
	TWeakObjectPtr<AITwinDecorationHelper> PersistenceMgr;
	bool bIsLoadingSceneAPICutoutsInGame = false;
	std::optional<bool> bDoesLoadedSceneContainCutouts;

	UPROPERTY()
	TWeakObjectPtr<UITwinClippingEffectManager> EffectManager;
};
