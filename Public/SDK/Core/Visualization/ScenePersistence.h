/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistence.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include "Core/ITwinAPI/ITwinScene.h"
#include "Core/Visualization/Timeline.h"
#include "Core/Tools/Types.h"
#include "MaterialPersistence.h"

MODULE_EXPORT namespace AdvViz::SDK
{
	class ADVVIZ_LINK  ILink : public Tools::ExtensionSupport, public Tools::TypeId<ILink>, public Tools::IDynType
	{

	public:
		virtual ~ILink() {}
		virtual const std::string& GetType() const = 0;
		virtual const std::string& GetRef() const = 0;
		virtual std::string GetName() const = 0;
		virtual bool GetVisibility() const = 0;
		virtual double GetQuality() const = 0;
		virtual dmat4x3 GetTransform() const = 0;

		virtual void SetType(const std::string&) = 0;
		virtual void SetRef(const std::string&) = 0;
		virtual void SetName(const std::string&) = 0;
		virtual void SetVisibility(bool) = 0;
		virtual void SetQuality(double) = 0;
		virtual void SetTransform(const dmat4x3&) = 0;

		virtual void SetGCS(const std::string&, const std::array<float, 3>&) = 0;
		virtual std::pair<std::string, std::array<float, 3>> GetGCS() const = 0;
		virtual bool HasGCS() const = 0;
		virtual bool HasName() const = 0;
		virtual bool HasVisibility() const = 0;
		virtual bool HasQuality() const = 0;
		virtual bool HasTransform() const = 0;
		virtual void Delete(bool value = true) = 0;
		virtual bool ShouldDelete() = 0;
		virtual const std::string& GetId() = 0;
		virtual bool ShouldSave()const = 0;
		virtual void SetShouldSave(bool shouldSave) = 0;


		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }
	};
	class IScenePersistence : public Tools::ExtensionSupport, public Tools::TypeId<IScenePersistence>, public Tools::IDynType
	{
	public:
		virtual ~IScenePersistence() {}
		//store data necessary for creation in the future
		virtual void PrepareCreation(const std::string& name, const std::string& itwinid) = 0;

		/// Create new Scene on server
		virtual bool Create(
			const std::string& name, const std::string& itwinid) = 0;
		/// Retrieve the Scene from server
		virtual bool Get(const std::string& itwinid, const std::string& id) = 0;
		/// Delete the Scene on server
		virtual bool Delete() = 0;
		/// Get scene identifiers
		virtual const std::string& GetId() const = 0;
		virtual const std::string& GetITwinId() const = 0;
		virtual const std::string& GetName()  const = 0;
		virtual std::string GetLastModified() const { return {}; }

		//setter/getter for scene members
		virtual void SetAtmosphere(const ITwinAtmosphereSettings&) = 0;
		virtual ITwinAtmosphereSettings GetAtmosphere() const = 0;
		virtual void SetSceneSettings(const ITwinSceneSettings&) = 0;
		virtual ITwinSceneSettings GetSceneSettings() const = 0;

		/// save to the decoration server
		virtual bool Save() = 0;
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool shouldSave) const = 0;
		//links management
		virtual std::vector<std::shared_ptr<ILink>> GetLinks() const = 0;
		virtual void AddLink(std::shared_ptr<ILink>) = 0;
		virtual std::shared_ptr<ILink> MakeLink() = 0;

		using Tools::TypeId<IScenePersistence>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }


		virtual void SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline) = 0;
		virtual std::shared_ptr<AdvViz::SDK::ITimeline> GetTimeline() = 0;

		// HDRI import/export, same for DS and API, may need to be combined later
		virtual std::string ExportHDRIAsJson(ITwinHDRISettings const& hdri) const = 0;

		virtual bool ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const = 0;
	};

#define ITWIN_DEFAULT_SCENE_NAME "default scene"

}