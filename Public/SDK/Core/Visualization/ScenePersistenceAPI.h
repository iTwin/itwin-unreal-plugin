/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistenceAPI.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "ScenePersistence.h"
#include "Config.h"

MODULE_EXPORT namespace AdvViz::SDK 
{
	class ADVVIZ_LINK LinkAPI final : public ILink, public Tools::Factory<LinkAPI>
	{
	public:
		LinkAPI();
		 ~LinkAPI();
		 const std::string& GetType() const override;
		 const std::string& GetRef() const override;
		 std::string GetName() const override;
		 bool GetVisibility() const override;
		 double GetQuality() const override;
		 dmat4x3 GetTransform() const override;
		 double GetFov() const override; // in radians
		 double GetAspectRatio() const override;

		 void SetType(const std::string&) override;
		 void SetRef(const std::string&) override;
		 void SetName(const std::string&) override;
		 void SetVisibility(bool) override;
		 void SetQuality(double) override;
		 void SetTransform(const dmat4x3&) override;
		 void SetFov(double) override;
		 void SetAspectRatio(double) override;

		 void SetGCS(const std::string&, const std::array<float, 3>&) override;
		 std::pair<std::string, std::array<float, 3>> GetGCS() const  override;
		 bool HasGCS() const override;
		 bool HasName() const override;
		 bool HasVisibility() const override;
		 bool HasQuality() const override;
		 bool HasTransform() const override;
		 bool HasFov() const override;
		 bool HasAspectRatio() const override;

		 //------------------------------------------------------------------------------
		 /// overridden from ISavableItem
		 const RefID& GetId() const override;
		 void SetId(const RefID& id) override;

		 ESaveStatus GetSaveStatus() const override;
		 void SetSaveStatus(ESaveStatus status) override;
		 //------------------------------------------------------------------------------

		 void Delete(bool value = true) override;
		 bool ShouldDelete() const override;

		 // for type check
		 using TypeId<LinkAPI>::GetTypeId;
		 std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		 bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || ILink::IsTypeOf(i); }

		 // Information about a resource that is stored in a repository and can be linked to a
		 // scene (such as an iModel or RealityData resource).
		 struct RepositoryResourceInfo
		 {
			 std::string repositoryId;
			 // The type accepted by the SceneAPI for the resource. For example, "iModels" for iModel
			 // resources, and "RealityData" for reality data resources.
			 std::string strClass;
		 };
		 bool IsRepositoryResource() const;
		 std::optional<RepositoryResourceInfo> GetRepositoryResourceInfo() const;

	protected:
		struct Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;

		friend class ScenePersistenceAPI;
	};

	namespace SceneAPIDetails {
		struct SJsonOut;
	}

	class AsyncRequestGroupCallback;

	class ADVVIZ_LINK ScenePersistenceAPI : public IScenePersistence, public Tools::Factory<ScenePersistenceAPI>
	{
	public:
		//store data necessary for creation in the future
		void PrepareCreation(const std::string& name, const std::string& itwinid) override;
		/// Create new Scene on server
		void AsyncCreate(
			const std::string& name, const std::string& itwinid,
			std::function<void(bool)>&& onCreationDoneFunc = {}) override;
		/// Retrieve the Scene from server
		bool Get(const std::string& itwinid, const std::string& id) override;
		void AsyncGet(const std::string&, const std::string& id, std::function<void(expected<void, std::string> const&)> onFinish)override;

		/// Delete the Scene on server
		bool Delete() override;
		/// Get Scene identifier
		const std::string& GetId() const final;
		const std::string& GetName() const final;
		const std::string& GetITwinId() const final;
		virtual std::string GetLastModified() const final;
		virtual std::string GetCreationTime() const final;
		virtual bool HasTag(const std::string_view& tagDisplayName) const final;

		// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);
		static void SetDefaultHttp(std::shared_ptr<Http> http);

		//allow disable export of resources (IModels/RealityData) when saving the scene. This is useful for scenepersistence that are not responsible for managing these resources, and want to avoid sending them to the server when saving the scene ( in case of Itwin Engage)
		static void EnableExportOfResources(bool bEnable);

		ScenePersistenceAPI(ScenePersistenceAPI&) = delete;
		ScenePersistenceAPI(ScenePersistenceAPI&&) = delete;
		virtual ~ScenePersistenceAPI();
		ScenePersistenceAPI();

		 // IScenePersistence values
		void SetAtmosphere(const ITwinAtmosphereSettings&) override;
		ITwinAtmosphereSettings GetAtmosphere() const override;
		void SetSceneSettings(const ITwinSceneSettings&) override;
		ITwinSceneSettings GetSceneSettings() const override;

		void SetCutout(RefID const& refID, const Cutout&) override;
		void UpdateCutoutBaseInfo(RefID const& refID, const CutoutBase&) override;
		void UpdateCutoutBox(RefID const& refID, const CutoutBox&) override;
		void UpdateCutoutPlane(RefID const& refID, const CutoutPlane&, bool inverse) override;
		void UpdateCutoutPolygonTransform(RefID const& refID, const std::array<double, 16>& transform) override;
		void UpdateCutoutPolygonPoint(RefID const& refID, size_t polygonIndex, size_t pointIndex, double3 const& position) override;
		void RemoveCutout(RefID const& refID) override;
		bool HasCutouts() const override;
		ECutoutType FindCutoutType(RefID const& refID) const override;
		std::map<RefID, Cutout> GetCutouts(std::set<ECutoutType> const& filteredTypes = {}) const override;

		void AsyncSave(std::function<void(bool)>&& onDataSavedFunc = {}) override;
		bool ShouldSave() const final;
		void SetShouldSave(bool shouldSave) const final;

		// links management
		std::vector<ILinkPtr> GetLinks() const override;
		void AddLink(ILinkPtr) override;
		ILinkPtr MakeLink() override;

		// Camera management
		static double GetDefaultCameraAspectRatio();
		static void SetDefaultCameraAspectRatio(double aspectRatio);

		//! Sets the handedness of the client -> ECEF map, as returned by
		//! IGCSTransform::GetHandedness(). Must be set before any scene is loaded or saved,
		//! typically right after the georeference has been initialized.
		static void SetCameraHandedness(double value);
		static double GetCameraHandedness();
		static bool IsCameraHandednessInitialized();

		//! Rebuilds the camera placements of all links that were loaded from a View3d
		//! carrying only up/direction/position (identity or absent ecefTransform).
		//! The 'right' column depends on the handedness of the georeference, which is not
		//! known when the links are parsed, so the reconstruction is deferred until here.
		//! Must be called after SetCameraHandedness(), i.e. once the georeference is ready.
		void ResolvePendingCameraBases() override;

		void SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline) final;
		std::shared_ptr<AdvViz::SDK::ITimeline> GetTimeline() final;

		// for type check
		using TypeId<ScenePersistenceAPI>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IScenePersistence::IsTypeOf(i); }

		std::string ExportHDRIAsJson(ITwinHDRISettings const& hdri) const override;
		bool ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const override;


		void AsyncRefreshLinks(std::function<void(expected<void, std::string> const&)> callback) override;

	protected:
		class Impl;
		const std::shared_ptr<Impl> impl_;
		Impl& GetImpl() const;


		//links management
		using LinkAPIPtr = std::shared_ptr<LinkAPI>;
		using LinkAPIPtrVec = std::vector<LinkAPIPtr>;

		void LoadLinks();
		void AsyncSaveLinksVec(
			LinkAPIPtrVec links,
			bool bSubLink,
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr,
			std::function<void(bool)>&& onLinksSavedFunc);
		void AsyncSaveLinks(std::shared_ptr<AsyncRequestGroupCallback> callbackPtr);
		void OnLinkSaved(LinkAPIPtr link, std::string const& idOnServer);


		std::string GenerateBody(const LinkAPIPtr& link, bool forPatch, bool ignoretimelineID = true); // if not for patch, then it is for post
		LinkAPIPtrVec GenerateSubLinks();
		LinkAPIPtrVec GeneratePreLinks();

		void InvalidateCutoutLink(RefID const& refID);
	};

	//global function to get all scenes from a Itwin
	ADVVIZ_LINK AdvViz::expected<ScenePtrVector, HttpError> GetITwinScenesAPI(const std::string& itwinid);
	ADVVIZ_LINK void AsyncGetITwinSceneInfosAPI(const std::string& itwinid,
		std::function<void(AdvViz::expected<SceneInfoVec, HttpError>)>&& callback,
		Http::EAsyncCallbackExecutionMode asyncCBExecMode = Http::EAsyncCallbackExecutionMode::Default);

	ADVVIZ_LINK void SetSceneAPIConfig(const Config::SConfig& c);
}
