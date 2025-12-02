/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistenceAPI.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include "Core/ITwinAPI/ITwinScene.h"
#include "../Tools/Types.h"
#include "ScenePersistence.h"
#include "Config.h"

MODULE_EXPORT namespace AdvViz::SDK 
{
	class ADVVIZ_LINK  LinkAPI : public ILink, public Tools::Factory<LinkAPI>
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

		 void SetType(const std::string&) override;
		 void SetRef(const std::string&) override;
		 void SetName(const std::string&) override;
		 void SetVisibility(bool) override;
		 void SetQuality(double) override;
		 void SetTransform(const dmat4x3&) override;

		 void SetGCS(const std::string&, const std::array<float, 3>&) override;
		 std::pair<std::string, std::array<float, 3>> GetGCS() const  override;
		 bool HasGCS() const override;
		 bool HasName() const override;
		 bool HasVisibility() const override;
		 bool HasQuality() const override;
		 bool HasTransform() const override;


		 bool ShouldSave()const override;
		 void SetShouldSave(bool shouldSave) override;

		 void Delete(bool value = true) override;
		 bool ShouldDelete() override;
		 const std::string& GetId() override;

		 // for type check
		 using TypeId<LinkAPI>::GetTypeId;
		 std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		 bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || ILink::IsTypeOf(i); }



	protected:
		struct Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;

		friend class ScenePersistenceAPI;
	};

	class ADVVIZ_LINK  ScenePersistenceAPI : public IScenePersistence, public Tools::Factory<ScenePersistenceAPI>
	{
	public:
		//store data necessary for creation in the future
		void PrepareCreation(const std::string& name, const std::string& itwinid) override;
		/// Create new Scene on server
		bool Create(
			const std::string& name, const std::string& itwinid) override;
		/// Retrieve the Scene from server
		bool Get(const std::string& itwinid, const std::string& id) override;
		/// Delete the Scene on server
		bool Delete() override;
		/// Get Scene identifier
		const std::string& GetId() const override;
		const std::string& GetName() const override;
		const std::string& GetITwinId() const override;
		virtual std::string GetLastModified() const override;

		// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);
		static void SetDefaultHttp(std::shared_ptr<Http> http);

		ScenePersistenceAPI(ScenePersistenceAPI&) = delete;
		ScenePersistenceAPI(ScenePersistenceAPI&&) = delete;
		virtual ~ScenePersistenceAPI();
		ScenePersistenceAPI();

		 // IScenePersistence values
		void SetAtmosphere(const ITwinAtmosphereSettings&) override;
		ITwinAtmosphereSettings GetAtmosphere() const override;
		void SetSceneSettings(const ITwinSceneSettings&) override;
		ITwinSceneSettings GetSceneSettings() const override;
		bool Save() override;
		bool ShouldSave() const override;
		void SetShouldSave(bool shouldSave) const override;

		//links management
		std::vector<std::shared_ptr<ILink>> GetLinks() const override;
		void AddLink(std::shared_ptr<ILink>) override;
		std::shared_ptr<ILink> MakeLink() override;

		void SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline) override;
		std::shared_ptr<AdvViz::SDK::ITimeline> GetTimeline() override;

		// for type check
		using TypeId<ScenePersistenceAPI>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IScenePersistence::IsTypeOf(i); }

		std::string ExportHDRIAsJson(ITwinHDRISettings const& hdri) const override;
		bool ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const override;
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;


		//links management
		void LoadLinks();
		void SaveLinks();

		std::string GenerateBody(const std::shared_ptr<LinkAPI>& link, bool forPatch); //else for post
		std::vector<std::shared_ptr<LinkAPI>> GenerateSubLinks();
		std::vector<std::shared_ptr<LinkAPI>> GeneratePreLinks();
	};

	//global function to get all scenes from a Itwin
	ADVVIZ_LINK  AdvViz::expected<std::vector<std::shared_ptr<IScenePersistence>>, int>  GetITwinScenesAPI(const std::string& itwinid);


	ADVVIZ_LINK void SetSceneAPIConfig(const Config::SConfig& c);
}