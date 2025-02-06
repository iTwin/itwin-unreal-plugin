/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistence.h $
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

MODULE_EXPORT namespace SDK::Core 
{
	class Link : public Tools::Factory<Link>
	{

	public:
		 Link();
		 ~Link();
		 const std::string& GetType() const;
		 const std::string& GetRef() const;
		 std::string GetName() const; 
		 bool GetVisibility() const;
		 double GetQuality() const;
		 dmat4x3 GetTransform() const;

		 void SetType(const std::string&);
		 void SetRef(const std::string&);
		 void SetName(const std::string&);
		 void SetVisibility(bool);
		 void SetQuality(double);
		 void SetTransform(const dmat4x3&);

		 void SetGCS(const std::string&, const std::array<float, 3>&);
		 std::pair<std::string, std::array<float, 3>> GetGCS() const ;
		 bool HasGCS() const;
		 bool HasName() const;
		 bool HasVisibility() const;
		 bool HasQuality() const;
		 bool HasTransform() const;


		 bool ShouldSave()const ;
		 void SetShouldSave(bool shouldSave);

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;

		friend class ScenePersistence;
	};
	class IScenePersistence : public Tools::Factory<IScenePersistence>
	{
	public:

		//store data necessary for creation in the future
		virtual void PrepareCreation(const std::string& name, const std::string& itwinid) = 0;

		/// Create new Scene on server
		virtual void Create(
			const std::string& name, const std::string& itwinid,
			const std::string& accessToken) = 0;
		/// Retrieve the Scene from server
		virtual bool Get(const std::string& id, const std::string& accessToken) = 0;
		/// Delete the Scene on server
		virtual void Delete(const std::string& accessToken) = 0;
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
		virtual bool ShouldSave() const = 0;
		virtual void SetShoudlSave(bool shouldSave) const = 0;
		//links management
		virtual std::vector<std::shared_ptr<Link>> GetLinks() const =0;
		virtual void AddLink(std::shared_ptr<Link>) = 0;
		virtual void SetLinks(const std::vector<std::shared_ptr<Link>>&) = 0;

	};

	class ScenePersistence : public Tools::ExtensionSupport, public IScenePersistence
	{
	public:
		//store data necessary for creation in the future
		void PrepareCreation(const std::string& name, const std::string& itwinid) override;
		/// Create new Scene on server
		void Create(
			const std::string& name, const std::string& itwinid,
			const std::string& accessToken) override;
		/// Retrieve the Scene from server
		bool Get(const std::string& id, const std::string& accessToken) override;
		/// Delete the Scene on server
		void Delete(const std::string& accessToken) override;
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
		bool ShouldSave() const override;
		void SetShoudlSave(bool shouldSave) const override;

		//links management
		std::vector<std::shared_ptr<Link>> GetLinks() const override;
		void AddLink(std::shared_ptr<Link>) override;
		void SetLinks(const std::vector<std::shared_ptr<Link>>&) override;

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl() const;


		//links management
		void LoadLinks(const std::string& accessToken);
		void SaveLinks(const std::string& accessToken);
	};

	//global function to get all scenes from a Itwin
	std::vector<std::shared_ptr<IScenePersistence>> GetITwinScenes(
		const std::string& itwinid, const std::string& accessToken);
}