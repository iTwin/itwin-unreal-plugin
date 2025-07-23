/*--------------------------------------------------------------------------------------+
|
|     $Source: AnnotationsManager.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

MODULE_EXPORT namespace AdvViz::SDK 
{

	struct Annotation
	{
		std::array<double, 3> position;
		std::string text;
		std::optional<std::string> name;
		std::optional<std::string> colorTheme;
		std::optional<std::string> displayMode;
		bool ShouldSave() const { return shouldSave_; }
		void SetShouldSave(bool value) { shouldSave_ = value; }
		RefID id; //maybe

		bool shouldSave_ = false;


	};
	class IAnnotationsManager : public Tools::Factory<IAnnotationsManager>, public Tools::ExtensionSupport
	{
	public:
		/// Load the data from the server
		virtual void LoadDataFromServerDS(const std::string& decorationId) = 0;
		/// Save the data on the server
		virtual void SaveDataOnServerDS(const std::string& decorationId) = 0;


		//get all Annotations
		virtual std::vector<std::shared_ptr<Annotation> > GetAnnotations() = 0;

		virtual void AddAnnotation(const std::shared_ptr<Annotation>&) = 0;
		virtual void RemoveAnnotation(const std::shared_ptr<Annotation>&) = 0;

		/// Check if there are instances to save on the server
		virtual bool HasAnnotationToSave() const = 0;
	};

	class ADVVIZ_LINK AnnotationsManager : public IAnnotationsManager, Tools::TypeId<AnnotationsManager>
	{
	public:
		/// Load the data from the server
		void LoadDataFromServerDS(const std::string& decorationId) override;
		/// Save the data on the server
		void SaveDataOnServerDS(const std::string& decorationId) override;

		std::vector<std::shared_ptr<Annotation> > GetAnnotations()override;
		void AddAnnotation(const std::shared_ptr<Annotation>&) override;
		void RemoveAnnotation(const std::shared_ptr<Annotation>&)override;

		void SetHttp(std::shared_ptr<Http> const& http);

		/// Check if there are instances to save on the server
		virtual bool HasAnnotationToSave() const override;

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