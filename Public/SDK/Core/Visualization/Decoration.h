/*--------------------------------------------------------------------------------------+
|
|     $Source: Decoration.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"

MODULE_EXPORT namespace SDK::Core 
{
	class IDecoration : public Tools::Factory<IDecoration>, public std::enable_shared_from_this<IDecoration>
	{
	public:
		/// Create new decoration on server
		virtual void Create(
			const std::string& name, const std::string& itwinid,
			const std::string& accessToken) = 0;
		/// Retreive the decoration from server
		virtual void Get(const std::string& id, const std::string& accessToken) = 0;
		/// Delete the decoration on server
		virtual void Delete() = 0;
		/// Get decoration identifier
		virtual const std::string& GetId() = 0;
	};

	class Decoration : public Tools::ExtensionSupport, public IDecoration
	{
	public:
		/// Create new decoration on server
		void Create(
			const std::string& name, const std::string& itwinid,
			const std::string& accessToken) override;
		/// Retreive the decoration from server
		void Get(const std::string& id, const std::string& accessToken) override;
		/// Delete the decoration on server
		void Delete() override;
		/// Get decoration identifier
		const std::string& GetId() override;

		// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		Decoration(Decoration&) = delete;
		Decoration(Decoration&&) = delete;
		virtual ~Decoration();
		Decoration();

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	std::vector<std::shared_ptr<IDecoration>> GetITwinDecorations(
		const std::string& itwinid, const std::string& accessToken);
}