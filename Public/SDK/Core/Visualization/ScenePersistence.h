/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistence.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include "Core/ITwinAPI/ITwinScene.h"

MODULE_EXPORT namespace SDK::Core 
{
	class IScenePersistence : public Tools::Factory<IScenePersistence>, public std::enable_shared_from_this<IScenePersistence>
	{
	public:
		/// Create new Scene on server
		virtual void Create(
			const std::string& name, const std::string& itwinid,
			const std::string& accessToken) = 0;
		/// Retrieve the Scene from server
		virtual void Get(const std::string& id, const std::string& accessToken) = 0;
		/// Delete the Scene on server
		virtual void Delete() = 0;
		/// Get scene identifiers
		virtual const std::string& GetId() const = 0;
		virtual const std::string& GetITwinId() const  = 0;
		virtual const std::string& GetName()  const = 0;

		//setter/getter for scene members
		virtual void SetAtmosphere(const ITwinAtmosphereSettings&) = 0;
		virtual ITwinAtmosphereSettings GetAtmosphere() const = 0;
		virtual void SetSceneSettings(const ITwinSceneSettings&) = 0;
		virtual ITwinSceneSettings GetSceneSettings() const = 0;

		/// save to the decoration server
		virtual void Save(const std::string& accessToken) = 0;
		virtual bool ShoudlSave() const = 0;
		virtual void SetShoudlSave(bool shouldSave) const = 0;


	};

	class ScenePersistence : public Tools::ExtensionSupport, public IScenePersistence
	{
	public:
		/// Create new Scene on server
		void Create(
			const std::string& name, const std::string& itwinid,
			const std::string& accessToken) override;
		/// Retrieve the Scene from server
		void Get(const std::string& id, const std::string& accessToken) override;
		/// Delete the Scene on server
		void Delete() override;
		/// Get Scene identifier
		const std::string& GetId() const override;
		const std::string& GetName() const override;
		const std::string& GetITwinId() const override;

		// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		ScenePersistence(ScenePersistence&) = delete;
		ScenePersistence(ScenePersistence&&) = delete;
		virtual ~ScenePersistence();
		ScenePersistence();

		 // IScenePersistence values
		void SetAtmosphere(const ITwinAtmosphereSettings&) override;
		ITwinAtmosphereSettings GetAtmosphere() const override;
		void SetSceneSettings(const ITwinSceneSettings&) override;
		ITwinSceneSettings GetSceneSettings() const override;
		void Save(const std::string& accessToken) override;
		bool ShoudlSave() const override;
		void SetShoudlSave(bool shouldSave) const override;

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;
	};

	//global function to get all scenes from a Itwin
	std::vector<std::shared_ptr<IScenePersistence>> GetITwinScenes(
		const std::string& itwinid, const std::string& accessToken);
}