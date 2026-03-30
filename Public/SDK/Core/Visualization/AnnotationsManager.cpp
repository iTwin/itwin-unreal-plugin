/*--------------------------------------------------------------------------------------+
|
|     $Source: AnnotationsManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "AnnotationsManager.h"

#include "Core/Network/HttpGetWithLink.h"
#include "SavableItemManager.h"
#include "SavableItemManager.inl"

#include "../Singleton/singleton.h"

namespace AdvViz::SDK
{
	struct SThreadSafeData
	{
		std::vector<AnnotationPtr> annotations_;
		std::vector<AnnotationPtr> removedAnnotations_;
		RefID::DBToIDMap annotationIDMap_;
	};


	struct SJsonAnnotation
	{
		std::array<double, 3> position;
		std::string text;
		std::optional<int> fontSize;
		std::optional<std::string> name;
		std::optional<std::string> colorTheme;
		std::optional<std::string> displayMode;
		std::optional<std::string> id;
	};

	SJsonAnnotation ToJsonAnnotation(const Annotation& src)
	{
		SJsonAnnotation dst;
		if (src.HasDBIdentifier())
		{
			dst.id = src.GetDBIdentifier();
		}
		dst.name = src.name;
		dst.text = src.text;
		dst.fontSize = src.fontSize;
		dst.position = src.position;
		dst.colorTheme = src.colorTheme;
		dst.displayMode = src.displayMode;
		return dst;
	}

	struct SJsonAnnotationVect
	{
		std::vector<SJsonAnnotation> annotations;
	};

	template <>
	struct SavableItemJsonHelper<Annotation>
	{
		using JsonVec = SJsonAnnotationVect;

		void AppendItem(JsonVec& jsonVec, Annotation const& annot)
		{
			jsonVec.annotations.emplace_back(ToJsonAnnotation(annot));
		}
	};

	class AnnotationsManager::Impl : public SavableItemManager
	{
	public:
		Tools::RWLockableObject<SThreadSafeData> thdata_;

		void Clear()
		{
			auto thdata = thdata_.GetAutoLock();
			thdata->annotations_.clear();
			thdata->removedAnnotations_.clear();
			thdata->annotationIDMap_.clear();
		}

		AnnotationPtr AddAnnotation()
		{
			Annotation* annotation(new Annotation);
			AnnotationPtr annotationPtr = MakeSharedLockableDataPtr<Annotation>(annotation);
			auto thdata = thdata_.GetAutoLock();
			thdata->annotations_.push_back(annotationPtr);
			return annotationPtr;
		}

		void FromJsonAnnotation(AnnotationPtr& dstPtr, const SJsonAnnotation& src)
		{
			auto dst = dstPtr->GetAutoLock();
			if (src.id)
			{
				auto thdata = thdata_.GetAutoLock();
				dst->SetId(RefID::FromDBIdentifier(*src.id, thdata->annotationIDMap_));
			}
			dst->name = src.name;
			dst->position = src.position;
			dst->text = src.text;
			dst->fontSize = src.fontSize;
			dst->colorTheme = src.colorTheme;
			dst->displayMode = src.displayMode;
			dst->SetShouldSave(false);
		}

		void LoadAnnotations(const std::string& decorationId)
		{
			auto ret = HttpGetWithLink<SJsonAnnotation>(GetHttp(),
				"decorations/" + decorationId + "/annotations",
				{} /* extra headers*/,
				[this](SJsonAnnotation const& row) -> expected<void, std::string>
				{
					if (!row.id)
						return make_unexpected("Server returned no id for annotation.");
					auto annotation = AddAnnotation();
					FromJsonAnnotation(annotation, row);
					return {};
				});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Loading of annotations failed. " << ret.error());
			}
		}

		void AsyncLoadAnnotations(const std::string& decorationId, 
			std::function<void(AdvViz::SDK::AnnotationPtr&)> OnAnnotationLoaded, 
			std::function<void(expected<void, std::string> const&)> OnLoadFinished)
		{
			AsyncHttpGetWithLink<SJsonAnnotation>(GetHttp(),
				"decorations/" + decorationId + "/annotations",
				{} /* extra headers*/,
				[this, OnAnnotationLoaded](SJsonAnnotation const& row) -> expected<void, std::string>
				{
					if (!row.id)
						return make_unexpected("Server returned no id for annotation.");
					auto annotation = AddAnnotation();
					FromJsonAnnotation(annotation, row);
					if (OnAnnotationLoaded)
						OnAnnotationLoaded(annotation);
					return {};
				},
				OnLoadFinished
			);
		}

		AnnotationPtr GetAnnotationById(const RefID& id) const
		{
			auto thdata = thdata_.GetAutoLock();
			auto it = std::find_if(thdata->annotations_.begin(), thdata->annotations_.end(),
				[&id](AnnotationPtr const& itemPtr) {
				auto item = itemPtr->GetRAutoLock();
				return item->GetId() == id;
			});
			if (it != thdata->annotations_.end())
				return *it;
			return {};
		}

		/////////////////////////////////////////////////////////////////////////////////
		/// for use in TAsyncSaveItems
		AnnotationPtr GetItemById(RefID const& id) const
		{
			return GetAnnotationById(id);
		}

		void OnItemDeletedOnDB(RefID const& deletedId) override
		{
			auto thdata = thdata_.GetAutoLock();
			std::erase_if(thdata->removedAnnotations_, [&deletedId](const auto& annotPtr)
			{
				auto annot = annotPtr->GetRAutoLock();
				return annot->GetId() == deletedId;
			});
		}
		/////////////////////////////////////////////////////////////////////////////////

		void AsyncSaveAnnotations(const std::string& decorationId, std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
		{
			auto thdata = thdata_.GetAutoLock();
			TAsyncSaveItems<Impl, Annotation>(*this,
				"decorations/" + decorationId + "/annotations",
				thdata->annotations_,
				callbackPtr);
		}

		void AsyncDeleteAnnotations(const std::string& decorationId, std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
		{
			auto thdata = thdata_.GetAutoLock();
			TAsyncDeleteItems<Impl, Annotation>(*this,
				"decorations/" + decorationId + "/annotations",
				thdata->removedAnnotations_,
				callbackPtr);
		}

		void AsyncSaveDataOnServer(const std::string& decorationId,
			std::function<void(bool)>&& onDataSavedFunc = {});


		void RemoveAnnotation(const AnnotationPtr& annotation)
		{
			if (annotation)
			{
				auto thdata = thdata_.GetAutoLock();
				BE_ASSERT(std::find(thdata->removedAnnotations_.begin(), thdata->removedAnnotations_.end(), annotation) == thdata->removedAnnotations_.end());
				thdata->removedAnnotations_.push_back(annotation);
				std::erase(thdata->annotations_, annotation);
			}
		}

		void RestoreAnnotation(const AnnotationPtr& annotation)
		{
			if (annotation)
			{
				auto thdata = thdata_.GetAutoLock();
				auto it = std::find(thdata->annotations_.begin(), thdata->annotations_.end(), annotation);
				auto& removedAnnotations_ = thdata->removedAnnotations_;
				auto& annotations_ = thdata->annotations_;
				BE_ASSERT(it == annotations_.end());
				if (it == annotations_.end())
				{
					annotations_.push_back(annotation);
				}
				std::erase(removedAnnotations_, annotation);
			}
		}

		bool HasAnnotationToSave() const
		{
			auto thdata = thdata_.GetRAutoLock();
			for (const auto& itPtr : thdata->annotations_)
			{
				auto it = itPtr->GetRAutoLock();
				if (it->ShouldSave())
				{
					return true;
				}
			}
			for (const auto& itPtr : thdata->removedAnnotations_)
			{
				auto it = itPtr->GetRAutoLock();
				if (it->HasDBIdentifier())
				{
					return true;
				}
			}
			return false;
		}

	};

	void AnnotationsManager::Impl::AsyncSaveDataOnServer(const std::string& decorationId,
		std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
			std::make_shared<AsyncRequestGroupCallback>(
				std::move(onDataSavedFunc), isThisValid_);

		// Save the annotations
		AsyncSaveAnnotations(decorationId, callbackPtr);

		// Delete obsolete annotations
		AsyncDeleteAnnotations(decorationId, callbackPtr);

		callbackPtr->OnFirstLevelRequestsRegistered();
	}


	void AnnotationsManager::LoadDataFromServer(const std::string& decorationId)
	{
		GetImpl().Clear();
		GetImpl().LoadAnnotations(decorationId);
	}

	void AnnotationsManager::AsyncLoadDataFromServer(const std::string& decorationId,
		std::function<void(AdvViz::SDK::AnnotationPtr&)> OnAnnotationLoaded,
		std::function<void(expected<void, std::string> const&)> OnLoadFinished)
	{
		GetImpl().Clear();
		GetImpl().AsyncLoadAnnotations(decorationId, OnAnnotationLoaded, OnLoadFinished);
	}

	void AnnotationsManager::AsyncSaveDataOnServer(const std::string& decorationId,
		std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		GetImpl().AsyncSaveDataOnServer(decorationId, std::move(onDataSavedFunc));
	}


	std::vector<AnnotationPtr > AdvViz::SDK::AnnotationsManager::GetAnnotations()
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->annotations_;
	}

	void AnnotationsManager::AddAnnotation(const AnnotationPtr& annotation)
	{
		if (annotation)
		{
			auto annot = annotation->GetAutoLock();
			if (!annot->HasDBIdentifier())
				annot->SetShouldSave(true);
		}
		auto thdata = GetImpl().thdata_.GetAutoLock();
		thdata->annotations_.push_back(annotation);
	}

	void AnnotationsManager::RemoveAnnotation(const AnnotationPtr& annotation)
	{
		GetImpl().RemoveAnnotation(annotation);
	}

	void AnnotationsManager::RestoreAnnotation(const AnnotationPtr& annotation)
	{
		GetImpl().RestoreAnnotation(annotation);
	}

	bool AnnotationsManager::HasAnnotationToSave() const
	{
		return GetImpl().HasAnnotationToSave();
	}

	void AnnotationsManager::SetHttp(std::shared_ptr<Http> const& http)
	{
		GetImpl().SetHttp(http);
	}

	AnnotationsManager::AnnotationsManager()
		: impl_(new Impl())
	{

	}

	AnnotationsManager::~AnnotationsManager()
	{}

	AnnotationsManager::Impl& AnnotationsManager::GetImpl()
	{
		return *impl_;
	}

	const AnnotationsManager::Impl& AnnotationsManager::GetImpl() const
	{
		return *impl_;
	}

	template<>
	Tools::Factory<IAnnotationsManager>::Globals::Globals()
	{
		newFct_ = []() {return static_cast<IAnnotationsManager*>(new AnnotationsManager()); };
	}

	template<>
	Tools::Factory<IAnnotationsManager>::Globals& Tools::Factory<IAnnotationsManager>::GetGlobals()
	{
		return singleton<Tools::Factory<IAnnotationsManager>::Globals>();
	}
}
