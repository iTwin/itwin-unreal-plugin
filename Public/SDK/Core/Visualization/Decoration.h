/*--------------------------------------------------------------------------------------+
|
|     $Source: Decoration.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#ifndef SDK_CPPMODULES
#ifndef MODULE_EXPORT
#define MODULE_EXPORT
#endif // !MODULE_EXPORT
#endif
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"

namespace AdvViz::SDK 
{
	class IDecoration : public Tools::Factory<IDecoration>, public Tools::ExtensionSupport
	{
	public:
		/// Create new decoration on server
		virtual void Create(
			const std::string& name, const std::string& itwinid) = 0;
		/// Retrieve the decoration from server
		virtual void Get(const std::string& id) = 0;
		/// Delete the decoration on server
		virtual void Delete() = 0;
		/// Get decoration identifier
		virtual const std::string& GetId() = 0;

		virtual void SetGCSTransform(const Tools::IGCSTransformPtr& transform) = 0;
		virtual const Tools::IGCSTransformPtr& GetGCSTransform() const = 0;

		/// Set Geo Coordinate System
		virtual void SetGCS(const GCS& v) = 0;
		/// Get Geo Coordinate System
		virtual const std::optional<GCS>& GetGCS()const = 0;
	};

	class ADVVIZ_LINK  Decoration : public IDecoration, Tools::TypeId<Decoration>
	{
	public:
		/// Create new decoration on server
		void Create(
			const std::string& name, const std::string& itwinid) override;
		/// Retrieve the decoration from server
		void Get(const std::string& id) override;
		/// Delete the decoration on server
		void Delete() override;
		/// Get decoration identifier
		const std::string& GetId() override;

		/// Set Geo Coordinate System
		void SetGCS(const GCS& v) override;
		/// Get Geo Coordinate System
		const std::optional<GCS>& GetGCS()const override;

		virtual void SetGCSTransform(const Tools::IGCSTransformPtr& transform) override;
		virtual const Tools::IGCSTransformPtr& GetGCSTransform() const override;

		// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		using Tools::TypeId<Decoration>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IDecoration::IsTypeOf(i); }

		Decoration(Decoration&) = delete;
		Decoration(Decoration&&) = delete;
		virtual ~Decoration();
		Decoration();

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};

	ADVVIZ_LINK std::vector<std::shared_ptr<IDecoration>> GetITwinDecorations(const std::string& itwinid);
}