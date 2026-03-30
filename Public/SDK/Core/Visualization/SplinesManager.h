/*--------------------------------------------------------------------------------------+
|
|     $Source: SplinesManager.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Core/Tools/Tools.h>
#include <Core/Visualization/Spline.h>

#include <functional>

MODULE_EXPORT namespace AdvViz::SDK 
{
	class Http;

	class ISplinesManager : public Tools::Factory<ISplinesManager>, public Tools::ExtensionSupport
	{
	public:
		/// Load the data from the server
		virtual void LoadDataFromServer(const std::string& decorationId) = 0;
		virtual void AsyncLoadDataFromServer(const std::string& decorationId,
			const std::function<void(ISplinePtr&)>& onSplineLoaded,
			const std::function<void(ISplinePointPtr&)>& onSplinePointLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete) = 0;
		/// Save the data on the server
		virtual void AsyncSaveDataOnServer(const std::string& decorationId,	std::function<void(bool)>&& onDataSavedFunc) = 0;

		virtual size_t GetNumberOfSplines() const = 0;
		virtual ISplinePtr GetSpline(const size_t index) const = 0;
		virtual ISplinePtr GetSplineById(const RefID& id) const = 0;
		virtual ISplinePtr GetSplineByDBId(const std::string& id) const = 0;
		virtual ISplinePtrVect GetSplines() const = 0;

		virtual ISplinePtr AddSpline() = 0;

		virtual void RemoveSpline(const size_t index) = 0;
		virtual void RemoveSpline(const ISplinePtr& spline) = 0;

		/// Restore a spline (supposedly removed before).
		virtual void RestoreSpline(const ISplinePtr& spline) = 0;

		virtual bool HasSplines() const = 0;
		virtual bool HasSplinesToSave() const = 0;

		//! Returns the (unique) spline ID matching the given identifier on the server, if a spline with this
		//! identifier was previously loaded, or else the invalid RefID.
		virtual RefID GetLoadedSplineId(std::string const& splineDBIdentifier) const = 0;
	};

	class SplinesManager : public ISplinesManager, public Tools::TypeId<SplinesManager>
	{
	public:
		/// Load the data from the server
		void LoadDataFromServer(const std::string& decorationId) override;
		void AsyncLoadDataFromServer(const std::string& decorationId, 
			const std::function<void(ISplinePtr&)>& onSplineLoaded,
			const std::function<void(ISplinePointPtr&)>& onSplinePointLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete) override;
		/// Save the data on the server
		void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc) override;

		size_t GetNumberOfSplines() const override;
		ISplinePtr GetSpline(const size_t index) const override;
		ISplinePtr GetSplineById(const RefID& id) const override;
		ISplinePtr GetSplineByDBId(const std::string& id) const override;
		ISplinePtrVect GetSplines() const override;
		ISplinePtr AddSpline() override;

		void RemoveSpline(const size_t index) override;
		void RemoveSpline(const ISplinePtr& spline) override;

		void RestoreSpline(const ISplinePtr& spline) override;

		bool HasSplines() const override;
		bool HasSplinesToSave() const override;

		RefID GetLoadedSplineId(std::string const& splineDBIdentifier) const override;

		/// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> const& http);

		SplinesManager(SplinesManager&) = delete;
		SplinesManager(SplinesManager&&) = delete;
		virtual ~SplinesManager();
		SplinesManager();

		using Tools::TypeId<SplinesManager>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || ISplinesManager::IsTypeOf(i); }
		
	protected:
		class Impl;
		const std::shared_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};
}