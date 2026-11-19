/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistence.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <string_view>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include "Core/ITwinAPI/ITwinScene.h"
#include "Core/Visualization/Cutout.h"
#include "Core/Visualization/SavableItem.h"
#include "Core/Visualization/Timeline.h"
#include "Core/Tools/Types.h"
#include "MaterialPersistence.h"
#include <functional>
#include <iosfwd>

MODULE_EXPORT namespace AdvViz::SDK
{
	class ADVVIZ_LINK ILink : public Tools::ExtensionSupport, public Tools::TypeId<ILink>, public Tools::IDynType, public ISavableItem
	{
	public:
		virtual ~ILink() {}
		virtual const std::string& GetType() const = 0;
		virtual const std::string& GetRef() const = 0;
		virtual std::string GetName() const = 0;
		virtual bool GetVisibility() const = 0;
		virtual double GetQuality() const = 0;
		virtual dmat4x3 GetTransform() const = 0;
		virtual double GetFov() const = 0; // in radians
		virtual double GetAspectRatio() const = 0;

		virtual void SetType(const std::string&) = 0;
		virtual void SetRef(const std::string&) = 0;
		virtual void SetName(const std::string&) = 0;
		virtual void SetVisibility(bool) = 0;
		virtual void SetQuality(double) = 0;
		virtual void SetTransform(const dmat4x3&) = 0;
		virtual void SetFov(double) = 0;
		virtual void SetAspectRatio(double) = 0;

		virtual void SetGCS(const std::string&, const std::array<float, 3>&) = 0;
		virtual std::pair<std::string, std::array<float, 3>> GetGCS() const = 0;
		virtual bool HasGCS() const = 0;
		virtual bool HasName() const = 0;
		virtual bool HasVisibility() const = 0;
		virtual bool HasQuality() const = 0;
		virtual bool HasTransform() const = 0;
		virtual bool HasFov() const = 0;
		virtual bool HasAspectRatio() const = 0;

		virtual void Delete(bool value = true) = 0;
		virtual bool ShouldDelete() const = 0;

		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }
	};
	using ILinkPtr = std::shared_ptr<ILink>;

	ADVVIZ_LINK std::ostream& operator<< (std::ostream& os, ILink const& link);


	class IScenePersistence : public Tools::ExtensionSupport, public Tools::TypeId<IScenePersistence>, public Tools::IDynType
	{
	public:
		virtual ~IScenePersistence() {}
		//store data necessary for creation in the future
		virtual void PrepareCreation(const std::string& name, const std::string& itwinid) = 0;

		/// Create new Scene on server
		virtual void AsyncCreate(
			const std::string& name, const std::string& itwinid,
			std::function<void(bool)>&& onCreationDoneFunc = {}) = 0;
		/// Retrieve the Scene from server
		virtual bool Get(const std::string& itwinid, const std::string& id) = 0;
		virtual void AsyncGet(const std::string&, const std::string& id, std::function<void(expected<void, std::string> const&)> onFinish) = 0;

		/// Delete the Scene on server
		virtual bool Delete() = 0;
		/// Get scene identifiers
		virtual const std::string& GetId() const = 0;
		virtual const std::string& GetITwinId() const = 0;
		virtual const std::string& GetName() const = 0;
		virtual std::string GetLastModified() const { return {}; }
		virtual std::string GetCreationTime() const { return {}; }
		virtual bool HasTag(const std::string_view& /*tagDisplayName*/) const { return false; }

		//setter/getter for scene members
		virtual void SetAtmosphere(const ITwinAtmosphereSettings&) = 0;
		virtual ITwinAtmosphereSettings GetAtmosphere() const = 0;
		virtual void SetSceneSettings(const ITwinSceneSettings&) = 0;
		virtual ITwinSceneSettings GetSceneSettings() const = 0;

		// cutouts management
		virtual void SetCutout(RefID const& refID, const Cutout&) = 0;
		virtual void RemoveCutout(RefID const& refID) = 0;
		virtual std::map<RefID, Cutout> GetCutouts(std::set<ECutoutType> const& filteredTypes = {}) const = 0;
		virtual bool HasCutouts() const = 0;
		virtual ECutoutType FindCutoutType(RefID const& refID) const = 0;
		// shortcuts to avoid passing the full cutout when only a part of it is updated, for better
		// performance (for example in case of polygon point update)
		virtual void UpdateCutoutBaseInfo(RefID const& refID, const CutoutBase&) = 0;
		virtual void UpdateCutoutBox(RefID const& refID, const CutoutBox&) = 0;
		virtual void UpdateCutoutPlane(RefID const& refID, const CutoutPlane&, bool inverse) = 0;
		virtual void UpdateCutoutPolygonTransform(RefID const& refID, const std::array<double, 16>& transform) = 0;
		virtual void UpdateCutoutPolygonPoint(RefID const& refID, size_t polygonIndex, size_t pointIndex, double3 const& position) = 0;


		/// save to the cloud (decoration server or iTwin SceneAPI)
		virtual void AsyncSave(std::function<void(bool)>&& onDataSavedFunc = {}) = 0;
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool shouldSave) const = 0;
		// links management
		virtual std::vector<ILinkPtr> GetLinks() const = 0;
		virtual void AddLink(ILinkPtr) = 0;
		virtual ILinkPtr MakeLink() = 0;
		virtual void ResolvePendingCameraBases() = 0;

		using Tools::TypeId<IScenePersistence>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }


		virtual void SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline) = 0;
		virtual std::shared_ptr<AdvViz::SDK::ITimeline> GetTimeline() = 0;

		// HDRI import/export, same for DS and API, may need to be combined later
		virtual std::string ExportHDRIAsJson(ITwinHDRISettings const& hdri) const = 0;

		virtual bool ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const = 0;

		// Reload links from distant database if needed
		virtual void AsyncRefreshLinks(std::function<void(expected<void, std::string> const&)> callback) {
			if (callback)
			{
				callback(make_unexpected<std::string>("Not implemented"));
			}
		}
	};

	struct SceneInfo
	{
		std::string id;
		std::string iTwinId;
		std::string displayName;
	};
	using SceneInfoVec = std::vector<SceneInfo>;

	using ScenePtrVector = std::vector<std::shared_ptr<IScenePersistence>>;

#define ITWIN_DEFAULT_SCENE_NAME "default scene"
	static constexpr auto MAIN_CAMERA_REF = "Main Camera";
	static constexpr auto HOME_CAMERA_REF = "Home Camera";
	static constexpr auto ENGAGE_TAG_DISPLAY_NAME = "ENGAGE";
}
