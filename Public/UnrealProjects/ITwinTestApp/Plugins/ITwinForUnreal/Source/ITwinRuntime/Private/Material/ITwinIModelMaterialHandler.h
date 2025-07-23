/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelMaterialHandler.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include <CoreMinimal.h>
#include <Containers/Map.h>

#include <MaterialPrediction/ITwinMaterialPredictionStatus.h>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

class AITwinIModel;
class FITwinSceneMapping;
class UITwinMaterialDefaultTexturesHolder;
class IITwinWebServicesObserver;
class UITwinWebServices;

namespace BeUtils
{
	class GltfMaterialHelper;
	class GltfTuner;
	class WLock;
}
namespace AdvViz::SDK
{
	enum class EChannelType : uint8_t;
	enum class ETextureSource : uint8_t;
	enum class EMaterialKind : uint8_t;
	struct ITwinMaterial;
	struct ITwinMaterialPrediction;
	struct ITwinMaterialPropertiesMap;
	struct ITwinTextureData;
	struct ITwinUVTransform;
	class MaterialPersistenceManager;
}

class FITwinIModelMaterialHandler
{
public:
	struct FITwinCustomMaterial
	{
		FString Name;
		FString DisplayName;
		bool bAdvancedConversion = false;
	};

	using MaterialPersistencePtr = std::shared_ptr<AdvViz::SDK::MaterialPersistenceManager>;
	static void SetGlobalPersistenceManager(MaterialPersistencePtr const& Mngr);
	static MaterialPersistencePtr const& GetGlobalPersistenceManager();


	FITwinIModelMaterialHandler();

	MaterialPersistencePtr const& GetPersistenceManager() const;
	void SetSpecificPersistenceManager(MaterialPersistencePtr const& Mngr);

	void Initialize(std::shared_ptr<BeUtils::GltfTuner> const& InTuner, AITwinIModel* OwnerIModel = nullptr);


	std::shared_ptr<BeUtils::GltfTuner> const& GetTuner() const {
		return GltfTuner;
	}

	/// Request a new tuning of the tileset.
	void Retune();

	std::shared_ptr<BeUtils::GltfMaterialHelper> const& GetGltfMatHelper() const {
		return GltfMatHelper;
	}

	void OnMaterialPropertiesRetrieved(AdvViz::SDK::ITwinMaterialPropertiesMap const& props, AITwinIModel& IModel);

	void OnTextureDataRetrieved(std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData);
	

	TMap<uint64, FITwinCustomMaterial> const& GetCustomMaterials() const;

	void DetectCustomizedMaterials(AITwinIModel* OwnerIModel = nullptr);

	/// Retune the tileset if needed, to ensure that all materials customized by the user (or about to be...)
	/// can be applied to individual meshes.
	void SplitGltfModelForCustomMaterials(bool bForceRetune = false);

	/// Enforce reloading material definitions as read from the decoration service.
	void ReloadCustomizedMaterials();

	void InitForSingleMaterial(FString const& IModelId, uint64_t const MaterialId);

	//-----------------------------------------------------------------------------------
	// Access/Modify properties
	//-----------------------------------------------------------------------------------

	double GetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const;
	void SetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, double Intensity,
		FITwinSceneMapping& SceneMapping);

	FLinearColor GetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const;
	void SetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, FLinearColor const& Color,
		FITwinSceneMapping& SceneMapping);

	FString GetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, AdvViz::SDK::ETextureSource& OutSource) const;
	void SetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
		FString const& TextureId, AdvViz::SDK::ETextureSource eSource,
		FITwinSceneMapping& SceneMapping,
		UITwinMaterialDefaultTexturesHolder const& DefaultTexturesHolder);

	AdvViz::SDK::ITwinUVTransform GetMaterialUVTransform(uint64_t MaterialId) const;
	void SetMaterialUVTransform(uint64_t MaterialId, AdvViz::SDK::ITwinUVTransform const& UVTransform,
		FITwinSceneMapping& SceneMapping);

	AdvViz::SDK::EMaterialKind GetMaterialKind(uint64_t MaterialId) const;
	void SetMaterialKind(uint64_t MaterialId, AdvViz::SDK::EMaterialKind NewKind,
		FITwinSceneMapping& SceneMapping);

	//! Retrieves some properties which have an impact on the base material used at render time.
	//! Returns whether the given material has a custom definition.
	bool GetMaterialCustomRequirements(uint64_t MaterialId, AdvViz::SDK::EMaterialKind& OutMaterialKind,
		bool& bOutRequiresTranslucency) const;

	//! Rename a material.
	bool SetMaterialName(uint64_t MaterialId, FString const& NewName);


	//-----------------------------------------------------------------------------------
	// Load properties from the Material Library
	//-----------------------------------------------------------------------------------

	//! Load a material from an asset file (expecting an asset of class #UITwinMaterialDataAsset).
	bool LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath,
		AITwinIModel& IModel);

	bool LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath,
		FString const& IModelId,
		FITwinSceneMapping& SceneMapping,
		UITwinMaterialDefaultTexturesHolder const& DefaultTexturesHolder,
		bool bForceRefreshAllParameters = false,
		std::function<void(AdvViz::SDK::ITwinMaterial&)> const& CustomizeMaterialFunc = {});

	bool LoadMaterialWithoutRetuning(AdvViz::SDK::ITwinMaterial& OutNewMaterial,
		uint64_t MaterialId,
		FString const& AssetFilePath,
		FString const& IModelId,
		BeUtils::WLock const& Lock,
		bool bForceRefreshAllParameters = false);


	//-----------------------------------------------------------------------------------
	// ML-based material prediction
	//-----------------------------------------------------------------------------------
	void ActivateMLMaterialPrediction(bool bActivate);
	void SetMaterialMLPredictionStatus(EITwinMaterialPredictionStatus InStatus);
	bool VisualizeMaterialMLPrediction() const {
		return bActivateMLMaterialPrediction
			&& (MLMaterialPredictionStatus == EITwinMaterialPredictionStatus::Complete
				|| MLMaterialPredictionStatus == EITwinMaterialPredictionStatus::Validated);
	}
	void SetMaterialMLPredictionObserver(IITwinWebServicesObserver* observer);
	IITwinWebServicesObserver* GetMaterialMLPredictionObserver() const { return MLPredictionMaterialObserver; }
	void OnMatMLPredictionRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPrediction const& Prediction,
		std::string const& error, AITwinIModel& IModel);
	void OnMatMLPredictionProgress(float fProgressRatio, AITwinIModel const& IModel);
	void ValidateMLPrediction();

private:
	/// Fills the map of known iTwin materials, if it was read from the tileset.
	void FillMaterialInfoFromTuner(AITwinIModel const* IModel);

	TMap<uint64, FITwinCustomMaterial>& GetMutableCustomMaterials();

	void InitTextureDirectory(AITwinIModel const* IModel);

	template <typename MaterialParamHelper>
	void TSetMaterialChannelParam(MaterialParamHelper const& Helper, uint64_t MaterialId,
		FITwinSceneMapping& SceneMapping);

	// Persistence management for ML material prediction.
	void SaveMLPredictionState();
	void LoadMLPredictionState(bool& bActivateML, BeUtils::WLock const& Lock);

	void UpdateModelFromMatMLPrediction(bool bSuccess,
		AdvViz::SDK::ITwinMaterialPrediction const& Prediction,
		std::string const& error,
		AITwinIModel& IModel);


private:
	std::shared_ptr<BeUtils::GltfTuner> GltfTuner;
	std::unordered_set<uint64_t> MatIDsToSplit; // stored to detect the need for retuning
	std::shared_ptr<BeUtils::GltfMaterialHelper> GltfMatHelper;

	//! Map of iTwin materials (the IDs are retrieved from the meta-data provided by the M.E.S)
	TMap<uint64, FITwinCustomMaterial> ITwinMaterials;
	//! Map of materials returned by the ML-based prediction service.
	TMap<uint64, FITwinCustomMaterial> MLPredictionMaterials;
	//! Secondary (optional) observer used for UI typically.
	IITwinWebServicesObserver* MLPredictionMaterialObserver = nullptr;

	struct FMatPredictionEntry
	{
		uint64_t MatID = 0;
		std::vector<uint64_t> Elements;
	};
	std::vector<FMatPredictionEntry> MaterialMLPredictions;

	bool bActivateMLMaterialPrediction = false;
	EITwinMaterialPredictionStatus MLMaterialPredictionStatus = EITwinMaterialPredictionStatus::Unknown;


	//! Persistence manager for material settings. A given instance will use either a specific manager, or
	//! the global one.
	MaterialPersistencePtr SpecificPersistenceMngr;
	static MaterialPersistencePtr GlobalPersistenceMngr;
};
