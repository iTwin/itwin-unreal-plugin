/*--------------------------------------------------------------------------------------+
|
|     $Source: AnnotationsManager.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "AnnotationsManager.h"

#include "Core/Network/HttpGetWithLink.h"
#include "Config.h"
#include "InstancesGroup.h"
#include "SplinesManager.h"
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
{
	class AnnotationsManager::Impl
	{
	public:
		std::shared_ptr<Http> http_;
		std::vector<std::shared_ptr<Annotation>> annotations_;
		std::vector<std::shared_ptr<Annotation>> removedAnnotations_;
		RefID::DBToIDMap annotationIDMap_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void Clear()
		{
			annotations_.clear();
			removedAnnotations_.clear();
			annotationIDMap_.clear();
		}

		// Some structures used in I/O functions
		struct SJsonIds { std::vector<std::string> ids; };

#define ANNOTATION_STRUCT_MEMBERS \
		std::array<double, 3> position;\
		std::string text;\
		std::optional<std::string> name;\
		std::optional<std::string> colorTheme;\
		std::optional<std::string> displayMode;\


		struct SJsonAnnotation
		{
			ANNOTATION_STRUCT_MEMBERS
			std::optional < std::string >id;
		};

		std::shared_ptr<Annotation> AddAnnotation()
		{
			std::shared_ptr<Annotation> annotation(new Annotation);
			annotations_.push_back(annotation);
			return annotation;
		}

		void FromJsonAnnotation(Annotation& dst, const SJsonAnnotation& src)
		{
			if (src.id)
			{
				dst.id = RefID::FromDBIdentifier(*src.id, annotationIDMap_);
			}
			dst.name = src.name;
			dst.position = src.position;
			dst.text = src.text;
			dst.colorTheme = src.colorTheme;
			dst.displayMode = src.displayMode;
		}
		SJsonAnnotation ToJsonAnnotation(const Annotation& src)
		{
			SJsonAnnotation dst;
			auto const& srcId = src.id;
			if (srcId.HasDBIdentifier())
			{
				dst.id = srcId.GetDBIdentifier();
			}
			dst.name = src.name;
			dst.text = src.text;
			dst.position = src.position;
			dst.colorTheme = src.colorTheme;
			dst.displayMode = src.displayMode;
			return dst;
		}
		void LoadAnnotations(const std::string& decorationId)
		{
			auto ret = HttpGetWithLink<SJsonAnnotation>(GetHttp(),
				"decorations/" + decorationId + "/annotations",
				{} /* extra headers*/,
				[this](SJsonAnnotation const& row) -> expected<void, std::string>
				{
					if (!row.id)
						return make_unexpected(std::string("Server returned no id for annotation."));
					auto annotation = AddAnnotation();
					FromJsonAnnotation(*annotation, row);
					annotation->SetShouldSave(false);
					return {};
				});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Loading of splines failed. " << ret.error());
			}
		}

		void SaveAnnotations(const std::string& decorationId)
		{
			struct SJsonAnnotationVect
			{
				std::vector<SJsonAnnotation> annotations;
			};
			SJsonAnnotationVect jInPost, jInPut;

			size_t index = 0;
			std::vector<size_t> newIndices;
			std::vector<size_t> updatedIndices;

			// Sort splines for requests (addition/update)
			for (auto const& item : annotations_)
			{
				if (!item->id.HasDBIdentifier())
				{
					jInPost.annotations.emplace_back(ToJsonAnnotation(*item));
					newIndices.push_back(index);
				}
				else if (item->ShouldSave())
				{
					jInPut.annotations.emplace_back(ToJsonAnnotation(*item));
					updatedIndices.push_back(index);
				}
				++index;
			}

			// Post (new)
			if (!jInPost.annotations.empty())
			{
				SJsonIds jOutPost;
				long status = GetHttp()->PostJsonJBody(
					jOutPost, "decorations/" + decorationId + "/annotations", jInPost);

				if (status == 200 || status == 201)
				{
					if (newIndices.size() == jOutPost.ids.size())
					{
						for (size_t i = 0; i < newIndices.size(); ++i)
						{
							Annotation& item = *annotations_[newIndices[i]];

							// Update the DB identifier only.
							RefID Id = item.id;
							Id.SetDBIdentifier(jOutPost.ids[i]);
							item.id = Id;
							item.SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new annotations failed. Http status: " << status);
				}
			}

			// Put (updated)
			if (!jInPut.annotations.empty())
			{
				struct SJsonSplineOutUpd { int64_t numUpdated = 0; };
				SJsonSplineOutUpd jOutPut;
				long status = GetHttp()->PutJsonJBody(
					jOutPut, "decorations/" + decorationId + "/annotations", jInPut);

				if (status == 200 || status == 201)
				{
					if (updatedIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for (size_t i = 0; i < updatedIndices.size(); ++i)
						{
							annotations_[updatedIndices[i]]->SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating annotations failed. Http status: " << status);
				}
			}
		}

		void DeleteAnnotations(const std::string& decorationId)
		{
			struct SJsonEmpty {};
			SJsonIds jIn;
			SJsonEmpty jOut;

			jIn.ids.reserve(removedAnnotations_.size());
			for (auto const& item : removedAnnotations_)
			{
				auto const& Id = item->id;
				if (Id.HasDBIdentifier())
					jIn.ids.push_back(Id.GetDBIdentifier());
			}

			if (jIn.ids.empty())
			{
				return;
			}

			long status = GetHttp()->DeleteJsonJBody(
				jOut, "decorations/" + decorationId + "/annotations", jIn);

			if (status == 200 || status == 201)
			{
				removedAnnotations_.clear();
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Deleting annotations failed. Http status: " << status);
			}
		}

		void RemoveAnnotation(const std::shared_ptr<Annotation>& annotation)
		{
			if (annotation)
			{
				removedAnnotations_.push_back(annotation);
				std::erase(annotations_, annotation);
			}
		}


		bool HasAnnotationToSave() const
		{
			for (const auto& it : annotations_)
			{
					if (!it->id.HasDBIdentifier()
					|| it->ShouldSave()
					)
					{
						return true;
					}
			}
			for (const auto& it : removedAnnotations_)
			{
				if (it->id.HasDBIdentifier())
				{
					return true;
				}
			}
			return false;
		}


	};

	void AnnotationsManager::LoadDataFromServerDS(const std::string& decorationId)
	{
		GetImpl().Clear();
		GetImpl().LoadAnnotations(decorationId);
	}

	void AnnotationsManager::SaveDataOnServerDS(const std::string& decorationId)
	{
		// Save the annotations
		GetImpl().SaveAnnotations(decorationId);

		// Delete obsolete annotations
		GetImpl().DeleteAnnotations(decorationId);
	}


	std::vector<std::shared_ptr<AdvViz::SDK::Annotation> > AdvViz::SDK::AnnotationsManager::GetAnnotations()
	{
		return GetImpl().annotations_;
	}

	void AdvViz::SDK::AnnotationsManager::AddAnnotation(const std::shared_ptr<Annotation>& annotation)
	{
		GetImpl().annotations_.push_back(annotation);
	}

	void AdvViz::SDK::AnnotationsManager::RemoveAnnotation(const std::shared_ptr<Annotation>& annotation )
	{
		GetImpl().RemoveAnnotation(annotation);
	}

	bool AdvViz::SDK::AnnotationsManager::HasAnnotationToSave() const
	{
		return GetImpl().HasAnnotationToSave();
	}

	void AnnotationsManager::SetHttp(std::shared_ptr<Http> const& http)
	{
		GetImpl().SetHttp(http);
	}

	AnnotationsManager::AnnotationsManager():impl_(new Impl())
	{
		GetImpl().SetHttp(GetDefaultHttp());
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