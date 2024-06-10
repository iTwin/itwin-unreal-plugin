/*--------------------------------------------------------------------------------------+
|
|     $Source: Scene.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"

MODULE_EXPORT namespace SDK::Core {

	class IDecorationEnvironment : public Tools::Factory<IDecorationEnvironment, std::string>, public std::enable_shared_from_this<IDecorationEnvironment>
	{
	public:
		/// Get DecorationEnvironment identifier
		virtual const std::string& GetId() = 0;
	};

	class DecorationEnvironment : public Tools::ExtensionSupport, public IDecorationEnvironment
	{
	public:
		DecorationEnvironment(const std::string& id);
		const std::string& GetId() override;
		virtual ~DecorationEnvironment();
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	class IDecorationLayer : public Tools::Factory<IDecorationLayer, std::string>, public std::enable_shared_from_this<IDecorationLayer>
	{
	public:
		/// Get DecorationLayer identifier
		virtual const std::string& GetId() = 0;

	};

	class DecorationLayer : public Tools::ExtensionSupport, public IDecorationLayer
	{
	public:
		DecorationLayer(const std::string& id);
		const std::string& GetId() override;
		virtual ~DecorationLayer();
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	class IScene : public Tools::Factory<IScene>, public std::enable_shared_from_this<IScene>
	{
	public:
		/// Create new scene on sever
		virtual void Create(const std::string& name) = 0;
		/// Retreive the scene from sever
		virtual void Get(const std::string& id) = 0;
		/// Delete the scene on sever
		virtual void Delete(bool deleteLayers = false) = 0;
		/// Get scene identifier
		virtual const std::string& GetId() = 0;

		/// Get scene Decoration Environment
		virtual const std::shared_ptr<IDecorationEnvironment>& GetDecorationEnvironment() = 0;
		/// Get scene Decoration Layers
		virtual const std::vector<std::shared_ptr<IDecorationLayer>>& GetDecorationLayers() = 0;
	};

	class Scene : public Tools::ExtensionSupport, public IScene
	{
	public:
		/// Create new scene on sever
		void Create(const std::string& name) override;
		/// Retreive the scene from sever
		void Get(const std::string& id) override;
		/// Delete the scene on sever
		void Delete(bool deleteLayers = false) override;
		/// Get scene identifier
		const std::string& GetId() override;

		// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		/// Get scene Decoration Environment
		const std::shared_ptr<IDecorationEnvironment>& GetDecorationEnvironment() override;
		/// Get scene Decoration Layers
		const std::vector<std::shared_ptr<IDecorationLayer>>& GetDecorationLayers() override;

		Scene(Scene&) = delete;
		Scene(Scene&&) = delete;
		virtual ~Scene();
		Scene();

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};


}