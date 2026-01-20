/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistenceDS.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include "Core/ITwinAPI/ITwinScene.h"
#include "../Tools/Types.h"
#include "ScenePersistence.h"

MODULE_EXPORT namespace AdvViz::SDK
{
	class ADVVIZ_LINK  LinkDS : public ILink, public Tools::Factory<LinkDS>
	{
	public:
		LinkDS();
		~LinkDS();
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

		std::uint64_t GetDynTypeId() const override { return TypeId<LinkDS>::GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == TypeId<LinkDS>::GetTypeId()) || ILink::IsTypeOf(i); }

	protected:
		struct Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;

		friend class ScenePersistenceDS;
	};

	class ADVVIZ_LINK ScenePersistenceDS : public IScenePersistence, public Tools::Factory<ScenePersistenceDS>
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

		// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		ScenePersistenceDS(ScenePersistenceDS&) = delete;
		ScenePersistenceDS(ScenePersistenceDS&&) = delete;
		virtual ~ScenePersistenceDS();
		ScenePersistenceDS();

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

		std::uint64_t GetDynTypeId() const override { return TypeId<ScenePersistenceDS>::GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == TypeId<ScenePersistenceDS>::GetTypeId()) || IScenePersistence::IsTypeOf(i); }

		void SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline) override;
		std::shared_ptr<AdvViz::SDK::ITimeline> GetTimeline() override;

		std::string ExportHDRIAsJson(ITwinHDRISettings const& hdri) const override;
		bool ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const override;
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;


		//links management
		void LoadLinks();
		void SaveLinks();
	};

	//global function to get all scenes from a Itwin
	ADVVIZ_LINK std::vector<std::shared_ptr<IScenePersistence>> GetITwinScenesDS(const std::string& itwinid);
}