/*--------------------------------------------------------------------------------------+
|
|     $Source: AnnotationsManager.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#ifndef SDK_CPPMODULES
	#include <string>
	#include <vector>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif
#include "Core/Network/Network.h"
#include "RefID.h"
#include "Core/Tools/Tools.h"
#include <Core/Visualization/SavableItem.h>

MODULE_EXPORT namespace AdvViz::SDK 
{
	using namespace Tools;
	struct Annotation : public SavableItemWithID
	{
		std::array<double, 3> position;
		std::string text;
		std::optional<int> fontSize;
		std::optional<std::string> name;
		std::optional<std::string> colorTheme;
		std::optional<std::string> displayMode;
	};

	typedef TSharedLockableDataPtr<Annotation> AnnotationPtr;

	class IAnnotationsManager : public Factory<IAnnotationsManager>, public ExtensionSupport
	{
	public:
		/// Load the data from the server
		virtual void LoadDataFromServer(const std::string& decorationId) = 0;
		virtual void AsyncLoadDataFromServer(const std::string& decorationId,
			std::function<void(AdvViz::SDK::AnnotationPtr&)> OnAnnotationLoaded,
			std::function<void(expected<void, std::string> const&)> OnLoadFinished) = 0;
		/// Save the data on the server
		virtual void AsyncSaveDataOnServer(const std::string& decorationId,
			std::function<void(bool)>&& onDataSavedFunc = {}) = 0;


		/// Get all Annotations
		virtual std::vector<AnnotationPtr > GetAnnotations() = 0;

		virtual void AddAnnotation(const AnnotationPtr&) = 0;
		virtual void RemoveAnnotation(const AnnotationPtr&) = 0;
		virtual void RestoreAnnotation(const AnnotationPtr&) = 0;

		/// Check if there are annotations to save on the server
		virtual bool HasAnnotationToSave() const = 0;
	};

	class ADVVIZ_LINK AnnotationsManager : public IAnnotationsManager, TypeId<AnnotationsManager>
	{
	public:
		/// Load the data from the server
		void LoadDataFromServer(const std::string& decorationId) override;
		void AsyncLoadDataFromServer(const std::string& decorationId,
			std::function<void(AdvViz::SDK::AnnotationPtr&)> OnAnnotationLoaded,
			std::function<void(expected<void, std::string> const&)> OnLoadFinished) override;

		/// Save the data on the server
		void AsyncSaveDataOnServer(const std::string& decorationId,
			std::function<void(bool)>&& onDataSavedFunc = {}) override;

		std::vector<AnnotationPtr > GetAnnotations() override;
		void AddAnnotation(const AnnotationPtr&) override;
		void RemoveAnnotation(const AnnotationPtr&) override;
		void RestoreAnnotation(const AnnotationPtr&) override;

		void SetHttp(std::shared_ptr<Http> const& http);

		/// Check if there are annotations to save on the server
		bool HasAnnotationToSave() const override;

		AnnotationsManager(AnnotationsManager&) = delete;
		AnnotationsManager(AnnotationsManager&&) = delete;
		virtual ~AnnotationsManager();
		AnnotationsManager();

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};
}
