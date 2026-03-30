/*--------------------------------------------------------------------------------------+
|
|     $Source: SplinesManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SplinesManager.h"

#include "Core/Network/HttpGetWithLink.h"
#include "../Singleton/singleton.h"
#include "SavableItemManager.h"
#include "SavableItemManager.inl"

namespace AdvViz::SDK
{

	struct SJsonSpline
	{
		std::optional<std::string> id;
		std::optional<std::string> name;
		std::string usage;
		std::vector<std::string> pointIDs;
		std::array<double, 12> transform;
		std::optional<std::string> userData;

		std::optional<std::vector<SplineLinkedModel>> linkedModels;
		std::optional<bool> enableEffect;
		std::optional<bool> invertEffect;
		std::optional<bool> closedLoop;
	};

	std::string GetStringFromUsage(const ESplineUsage usage)
	{
		switch (usage)
		{
		default:
			BE_ISSUE("unhandled usage case", static_cast<uint8_t>(usage));
			[[fallthrough]];
		case ESplineUsage::Undefined:
			return "Undefined";
		case ESplineUsage::MapCutout:
			return "MapCutout";
		case ESplineUsage::TrafficPath:
			return "TrafficPath";
		case ESplineUsage::PopulationZone:
			return "PopulationZone";
		case ESplineUsage::PopulationPath:
			return "PopulationPath";
		case ESplineUsage::AnimPath:
			return "AnimPath";
		}
	}

	ESplineUsage GetUsageFromString(const std::string& strUsage)
	{
		if (strUsage == "Undefined")
			return ESplineUsage::Undefined;
		else if (strUsage == "MapCutout")
			return ESplineUsage::MapCutout;
		else if (strUsage == "TrafficPath")
			return ESplineUsage::TrafficPath;
		else if (strUsage == "PopulationZone")
			return ESplineUsage::PopulationZone;
		else if (strUsage == "PopulationPath")
			return ESplineUsage::PopulationPath;
		else if (strUsage == "AnimPath")
			return ESplineUsage::AnimPath;

		BE_ISSUE("unknown spline usage", strUsage);
		return ESplineUsage::Undefined;
	}

	std::string GetStringFromTangentMode(const ESplineTangentMode mode)
	{
		switch (mode)
		{
		case ESplineTangentMode::Linear:
			return "Linear";
		case ESplineTangentMode::Smooth:
			return "Smooth";
		case ESplineTangentMode::Custom:
			return "Custom";
		default:
			BE_ISSUE("unhandled tangent mode case", static_cast<uint8_t>(mode));
			return {};
		}
	}

	ESplineTangentMode GetTangentModeFromString(const std::string& strMode)
	{
		if (strMode == "Linear")
		{
			return ESplineTangentMode::Linear;
		}
		else if (strMode == "Smooth")
		{
			return ESplineTangentMode::Smooth;
		}
		else if (strMode == "Custom")
		{
			return ESplineTangentMode::Custom;
		}
		BE_ISSUE("unknown tangent mode", strMode);
		return ESplineTangentMode::Linear;
	}

	SJsonSpline ToJsonSpline(const ISpline& src)
	{
		SJsonSpline dst;
		auto const& srcId = src.GetId();
		if (srcId.HasDBIdentifier())
		{
			dst.id = srcId.GetDBIdentifier();
		}
		auto const& srcName = src.GetName();
		if (!srcName.empty())
		{
			dst.name = srcName;
		}
		dst.usage = GetStringFromUsage(src.GetUsage());
		dst.closedLoop = src.IsClosedLoop();
		for (auto const& pointPtr : src.GetPoints())
		{
			auto point = pointPtr->GetRAutoLock();
			RefID const& pointId = point->GetId();
			if (pointId.HasDBIdentifier())
			{
				dst.pointIDs.push_back(pointId.GetDBIdentifier());
			}
			else
			{
				BE_ISSUE("points should be saved before splines");
			}
		}
		dst.transform = src.GetTransform();

		auto const& linkedModels = src.GetLinkedModels();
		if (!linkedModels.empty())
		{
			auto& dstLinkedModels = dst.linkedModels.emplace();
			dstLinkedModels = linkedModels;
		}
		if (src.GetInvertEffect())
		{
			dst.invertEffect = true;
		}
		if (!src.IsEnabledEffect())
		{
			dst.enableEffect = false;
		}
		return dst;
	}

	struct SJsonSplineVect
	{
		std::vector<SJsonSpline> splines;
	};

	template <>
	struct SavableItemJsonHelper<ISpline>
	{
		using JsonVec = SJsonSplineVect;

		void AppendItem(JsonVec& jsonVec, ISpline const& spline)
		{
			jsonVec.splines.emplace_back(ToJsonSpline(spline));
		}
	};

	static inline ISplinePtr FindSplineById(ISplinePtrVect const& splines, const RefID& id)
	{
		// TODO_JDE - optimize lookup with map
		auto it = std::find_if(splines.begin(), splines.end(),
			[&id](ISplinePtr const& splPtr) {
			auto spl = splPtr->GetRAutoLock();
			return spl->GetId() == id;
		});
		if (it != splines.end())
			return *it;
		return {};
	}

	class SplinesManager::Impl : public SavableItemManager, public std::enable_shared_from_this<Impl>
	{
	public:


		struct SThreadSafeData
		{
			ISplinePtrVect splines_;
			ISplinePtrVect removedSplines_;
			RefID::DBToIDMap splineIDMap_;
			RefID::DBToIDMap splinePointIDMap_;
		};

		Tools::RWLockableObject<SThreadSafeData> thdata_;

		void Clear()
		{
			auto dataLock = thdata_.GetAutoLock();
			dataLock->splines_.clear();
			dataLock->splineIDMap_.clear();
			dataLock->splinePointIDMap_.clear();
		}

		#define POINT_STRUCT_MEMBERS \
			std::array<double, 3> position;\
			std::array<double, 3> upVector;\
			std::string inTangentMode;\
			std::array<double, 3> inTangent;\
			std::string outTangentMode;\
			std::array<double, 3> outTangent;

		struct SJsonPoint
		{
			POINT_STRUCT_MEMBERS
		};

		struct SJsonPointWithId
		{
			POINT_STRUCT_MEMBERS
			std::string id;
		};

		// key: integer coming from a RefID
		// value: index in the (flat) vector 'pointIDs'
		using SRefIdIndexMap = std::map<uint64_t, size_t>;

		template<class T>
		static void CopyPoint(T& dst, const ISplinePoint& src)
		{
			dst.position = src.GetPosition();
			dst.upVector = src.GetUpVector();
			dst.inTangent = src.GetInTangent();
			dst.outTangent = src.GetOutTangent();
			dst.inTangentMode = GetStringFromTangentMode(src.GetInTangentMode());
			dst.outTangentMode = GetStringFromTangentMode(src.GetOutTangentMode());
		}

		template<class T>
		static void CopyPoint(ISplinePointPtr& dstPtr, const T& src)
		{
			auto point = dstPtr->GetAutoLock();
			point->SetPosition(src.position);
			point->SetUpVector(src.upVector);
			point->SetInTangent(src.inTangent);
			point->SetOutTangent(src.outTangent);
			point->SetInTangentMode(GetTangentModeFromString(src.inTangentMode));
			point->SetOutTangentMode(GetTangentModeFromString(src.outTangentMode));
		}

		void FromJsonSpline(ISplinePtr& dstPtr, const SJsonSpline& src)
		{
			auto dataLock = thdata_.GetAutoLock();
			auto dst = dstPtr->GetAutoLock();
			if (src.id)
			{
				dst->SetId(RefID::FromDBIdentifier(*src.id, dataLock->splineIDMap_));
			}
			if (src.name)
			{
				dst->SetName(*src.name);
			}
			dst->SetUsage(GetUsageFromString(src.usage));
			if (src.closedLoop)
			{
				dst->SetClosedLoop(*src.closedLoop);
			}
			dst->SetTransform(src.transform);
			for (size_t i = 0; i < src.pointIDs.size(); ++i)
			{
				if (!src.pointIDs[i].empty())
				{
					auto pointPtr = dst->AddPoint();
					auto point = pointPtr->GetAutoLock();
					point->SetId(RefID::FromDBIdentifier(src.pointIDs[i], dataLock->splinePointIDMap_));
				}
			}
			std::vector<SplineLinkedModel> linkedModels;
			if (src.linkedModels)
			{
				linkedModels = *src.linkedModels;
			}
			// For compatibility with earlier versions: by default, cut-out was applied to the Google
			// tileset only.
			if (dst->GetUsage() == ESplineUsage::MapCutout
				&& linkedModels.empty())
			{
				linkedModels.push_back({ .modelType = "GlobalMapLayer" });
			}
			dst->SetLinkedModels(linkedModels);
			dst->EnableEffect(src.enableEffect.value_or(true));
			dst->SetInvertEffect(src.invertEffect.value_or(false));
			dst->SetShouldSave(false);
		}

		void LoadSplines(const std::string& decorationId)
		{
			auto ret = HttpGetWithLink<SJsonSpline>(GetHttp(),
				"decorations/" + decorationId + "/splines",
				{} /* extra headers*/,
				[this](SJsonSpline const& row) -> expected<void, std::string>
				{
					if (!row.id)
						return make_unexpected("Server returned no id for spline.");
					ISplinePtr spline = AddSpline();
					FromJsonSpline(spline, row);
					return {};
				});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Loading of splines failed. " << ret.error());
			}
		}

		void LoadSplinePoints(const std::string& decorationId)
		{
			std::map<RefID, ISplinePointPtr> mapIdToPoint;

			auto ret = HttpGetWithLink<SJsonPointWithId>(GetHttp(),
				"decorations/" + decorationId + "/splinepoints",
				{} /* extra headers*/,
				[this, &mapIdToPoint](SJsonPointWithId const& row) -> expected<void, std::string>
			{
				if (row.id.empty())
					return {};

				ISplinePoint* point(ISplinePoint::New());
				auto dataLock = thdata_.GetAutoLock();
				point->SetId(RefID::FromDBIdentifier(row.id, dataLock->splinePointIDMap_));
				auto pointPtr = MakeSharedLockableDataPtr(point);
				CopyPoint<SJsonPointWithId>(pointPtr, row);
				mapIdToPoint[point->GetId()] = pointPtr;

				return {};
			});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Loading of spline points failed. " << ret.error());
			}

			auto thdata = thdata_.GetAutoLock();
			auto& splines_ = thdata->splines_;
			// Put the loaded points in splines (their current points only have valid IDs but no valid data).
			for (auto& splinePtr : splines_)
			{
				auto spline = splinePtr->GetAutoLock();
				size_t const numPoints = spline->GetNumberOfPoints();
				for (size_t i = 0; i < numPoints; ++i)
				{
					ISplinePointPtr pointPtr = spline->GetPoint(i);
					auto point = pointPtr->GetAutoLock();
					auto itPoint = mapIdToPoint.find(point->GetId());
					if (itPoint != mapIdToPoint.end())
					{
						spline->SetPoint(i, itPoint->second);
					}
				}
			}
		}

		void LoadDataFromServer(const std::string& decorationId)
		{
			Clear();
			LoadSplines(decorationId);
			LoadSplinePoints(decorationId);
		}

		void AsyncLoadSplines(const std::string& decorationId, 
			const std::function<void(ISplinePtr&)>& onSplineLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete)
		{
			auto thisPtr = shared_from_this();
			AsyncHttpGetWithLink<SJsonSpline>(GetHttp(),
				"decorations/" + decorationId + "/splines",
				{} /* extra headers*/,
				[thisPtr, onSplineLoaded](SJsonSpline const& row) -> expected<void, std::string>
				{
					if (!row.id)
						return make_unexpected("Server returned no id for spline.");
					ISplinePtr spline = thisPtr->AddSpline();
					thisPtr->FromJsonSpline(spline, row);
					onSplineLoaded(spline);
					return {};
				},
				onComplete);
		}

		void AsyncLoadSplinePoints(const std::string& decorationId, 
			const std::function<void(ISplinePointPtr&)>& onSplinePointLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete
			)
		{
			std::shared_ptr<std::map<std::string, ISplinePointPtr>> mapIdToPoint = std::make_shared<std::map<std::string, ISplinePointPtr>>();
			auto thisPtr = shared_from_this();
			AsyncHttpGetWithLink<SJsonPointWithId>(GetHttp(),
				"decorations/" + decorationId + "/splinepoints",
				{} /* extra headers*/,
				[thisPtr, mapIdToPoint, onSplinePointLoaded](SJsonPointWithId const& row) -> expected<void, std::string>
				{
					if (row.id.empty())
						return {};

					ISplinePoint* point(ISplinePoint::New());
					auto dataLock = thisPtr->thdata_.GetAutoLock();
					point->SetId(RefID::FromDBIdentifier(row.id, dataLock->splinePointIDMap_));
					auto pointPtr = MakeSharedLockableDataPtr(point);
					thisPtr->CopyPoint<SJsonPointWithId>(pointPtr, row);
					(*mapIdToPoint)[row.id] = pointPtr;
					onSplinePointLoaded(pointPtr);
					return {};
				},
				[thisPtr, mapIdToPoint, onComplete](expected<void, std::string> const& ret)
				{
					if (!ret)
					{
						BE_LOGW("ITwinDecoration", "Loading of spline points failed. " << ret.error());
						onComplete(ret);
						return;
					}

					auto thdata = thisPtr->thdata_.GetAutoLock();
					auto& splines_ = thdata->splines_;
					// Put the loaded points in splines (their current points only have valid IDs but no valid data).
					for (auto& splinePtr : splines_)
					{
						auto spline = splinePtr->GetAutoLock();
						size_t const numPoints = spline->GetNumberOfPoints();
						for (size_t i = 0; i < numPoints; ++i)
						{
							ISplinePointPtr pointPtr = spline->GetPoint(i);
							auto point = pointPtr->GetAutoLock();
							auto itPoint = mapIdToPoint->find(point->GetDBIdentifier());
							if (itPoint != mapIdToPoint->end())
							{
								spline->SetPoint(i, itPoint->second);
							}
						}
					}
					onComplete(ret);
				}
				);
		}
		void AsyncLoadDataFromServer(const std::string& decorationId,
			const std::function<void(ISplinePtr&)> &onSplineLoaded,
			const std::function<void(ISplinePointPtr&)> &onSplinePointLoaded,
			const std::function<void(expected<void, std::string> const&)> &onComplete)
		{
			Clear();
			auto thisPtr = shared_from_this();
			AsyncLoadSplines(decorationId,
				onSplineLoaded,
				[thisPtr, decorationId, onSplinePointLoaded, onComplete](expected<void, std::string> const& ret)
				{
					if (!ret)
					{
						BE_LOGW("ITwinDecoration", "Loading of splines failed. " << ret.error());
						onComplete(ret);
						return;
					}
					thisPtr->AsyncLoadSplinePoints(decorationId,
						onSplinePointLoaded,
						onComplete);
				});
		}

		void AsyncSaveSplinePoints(const std::string& decorationId, std::function<void(bool)>&& onPointsSavedFunc)
		{
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
				std::make_shared<AsyncRequestGroupCallback>(
					std::move(onPointsSavedFunc), isThisValid_);

			struct SJsonPointVect { std::vector<SJsonPoint> splinePoints; };
			struct SJsonPointWithIdVect { std::vector<SJsonPointWithId> splinePoints; };
			SJsonPointVect jInPost;
			SJsonPointWithIdVect jInPut;
			std::vector<std::pair<RefID, RefID>> newPointIds;
			std::vector<std::pair<RefID, RefID>> updatedPointIds;

			auto thdata = thdata_.GetAutoLock();
			auto& splines_ = thdata->splines_;
			// Sort points for requests (addition/update)
			for (auto const& splinePtr : splines_)
			{
				auto spline = splinePtr->GetRAutoLock();	
				for (ISplinePointPtr const& pointPtr : spline->GetPoints())
				{
					auto point = pointPtr->GetAutoLock();
					if (!point->HasDBIdentifier())
					{
						SJsonPoint jPoint;
						CopyPoint<SJsonPoint>(jPoint, point);
						jInPost.splinePoints.push_back(jPoint);
						newPointIds.push_back(std::make_pair(spline->GetId(), point->GetId()));
						point->OnStartSave();
					}
					else if (point->ShouldSave())
					{
						SJsonPointWithId jPointWId;
						CopyPoint<SJsonPointWithId>(jPointWId, point);
						jPointWId.id = point->GetDBIdentifier();
						jInPut.splinePoints.push_back(jPointWId);
						updatedPointIds.push_back(std::make_pair(spline->GetId(), point->GetId()));
						point->OnStartSave();
					}
				}
			}

			// Post (new points)
			if (!jInPost.splinePoints.empty())
			{
				AsyncPostJsonJBody<SJsonIds>(GetHttp(), callbackPtr,
					[this, newPointIds](long httpCode,
										const Tools::TSharedLockableData<SJsonIds>& joutPtr)
					{
						const bool bSuccess = (httpCode == 200 || httpCode == 201);
						if (bSuccess)
						{
							auto thdata = thdata_.GetAutoLock();
							auto& splines = thdata->splines_;
							auto unlockedJout = joutPtr->GetAutoLock();
							SJsonIds& jOutPost = unlockedJout.Get();
							if (newPointIds.size() == jOutPost.ids.size())
							{
								for (size_t i = 0; i < newPointIds.size(); ++i)
								{
									auto const& index = newPointIds[i];
									if (auto splinePtr = FindSplineById(splines, index.first))
									{
										auto spline = splinePtr->GetAutoLock();
										RefID pointId = index.second;
										// Update the DB identifier only.
										pointId.SetDBIdentifier(jOutPost.ids[i]);

										ISplinePointPtr pointPtr = spline->GetPointById(pointId);
										if (pointPtr)
										{
											auto point = pointPtr->GetAutoLock();
											point->SetId(pointId);
											point->OnSaved();
											spline->SetShouldSave(true); // the point is added so the spline won't be saved unless we tell her to.
										}
									}
								}
							}
						}
						else
						{
							BE_LOGW("ITwinDecoration", "Saving new points failed. Http status: " << httpCode);
						}
						return bSuccess;
					},
						"decorations/" + decorationId + "/splinepoints",
						jInPost);
			}

			// Put (updated points)
			if (!jInPut.splinePoints.empty())
			{
				struct SJsonPointOutUpd
				{
					int64_t numUpdated = 0;
				};
				AsyncPutJsonJBody<SJsonPointOutUpd>(GetHttp(), callbackPtr,
					[this, updatedPointIds](long httpCode,
											const Tools::TSharedLockableData<SJsonPointOutUpd>& joutPtr)
				{
					const bool bSuccess = (httpCode == 200 || httpCode == 201);
					if (bSuccess)
					{
						auto unlockedJout = joutPtr->GetAutoLock();
						auto thdata = thdata_.GetAutoLock();
						auto const& splines = thdata->splines_;
						SJsonPointOutUpd& jOutPut = unlockedJout.Get();
						if (updatedPointIds.size() == static_cast<size_t>(jOutPut.numUpdated))
						{
							for (auto const& index : updatedPointIds)
							{
								if (auto splinePtr = FindSplineById(splines, index.first))
								{
									auto spline = splinePtr->GetAutoLock();
									ISplinePointPtr pointPtr = spline->GetPointById(index.second);
									if (pointPtr)
									{
										auto point = pointPtr->GetAutoLock();
										point->OnSaved();
									}
								}
							}
						}
					}
					else
					{
						BE_LOGW("ITwinDecoration", "Updating points failed. Http status: " << httpCode);
					}
					return bSuccess;
				},
					"decorations/" + decorationId  + "/splinepoints",
					jInPut);
			}

			callbackPtr->OnFirstLevelRequestsRegistered();
		}

		void AsyncSaveSplines(const std::string& decorationId, std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
		{
			auto thdata = thdata_.GetAutoLock();
			TAsyncSaveItems<Impl, ISpline>(*this,
				"decorations/" + decorationId + "/splines",
				thdata->splines_,
				callbackPtr
			);
		}

		void AsyncDeleteSplinePoints(const std::string& decorationId, std::function<void(bool)>&& onPointsDeletedFunc)
		{
			BE_ASSERT(IsValidThreadForAsyncSaving());

			SJsonIds jIn;
			std::vector<std::pair<RefID, RefID>> removedPointIds;
			
			auto thdata = thdata_.GetAutoLock();
			auto& splines_ = thdata->splines_;

			// Get the ids of removed points in splines that are still used.
			for (auto const& splinePtr : splines_)
			{
				auto spline = splinePtr->GetRAutoLock();
				const ISplinePointPtrVect& points = spline->GetRemovedPoints();
				for (auto const& pointPtr : points)
				{
					auto point = pointPtr->GetRAutoLock();
					auto const& pointId = point->GetId();
					if (pointId.HasDBIdentifier())
					{
						jIn.ids.push_back(pointId.GetDBIdentifier());
						removedPointIds.push_back(
							std::make_pair(spline->GetId(), point->GetId()));
					}
				}
			}

			if (jIn.ids.empty())
			{
				if (onPointsDeletedFunc)
					onPointsDeletedFunc(true);
				return;
			}

			// Delete the points from the server
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
				std::make_shared<AsyncRequestGroupCallback>(
					std::move(onPointsDeletedFunc), isThisValid_);

			AsyncDeleteJsonNoOutput(GetHttp(), callbackPtr,
				[this, removedPointIds](long httpCode)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201 || httpCode == 204 /* No-Content*/);
				if (bSuccess)
				{
					auto thdata = thdata_.GetAutoLock();
					auto const& splines = thdata->splines_;
					for (auto const& removed : removedPointIds)
					{
						if (auto splinePtr = FindSplineById(splines, removed.first))
						{
							auto spline = splinePtr->GetAutoLock();
							spline->UnregisterRemovedPointById(removed.second);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Deleting spline points failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/splinepoints",
				jIn);

			callbackPtr->OnFirstLevelRequestsRegistered();
		}

		void AsyncDeleteSplines(const std::string& decorationId, std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
		{
			auto thdata = thdata_.GetAutoLock();
			TAsyncDeleteItems<Impl, ISpline>(*this,
				"decorations/" + decorationId + "/splines",
				thdata->removedSplines_,
				callbackPtr);
		}

		void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
		{
			// Helper used to call onDataSavedFunc when *all* requests are processed.
			std::shared_ptr<AsyncRequestGroupCallback> saveAllCallback =
				std::make_shared<AsyncRequestGroupCallback>(
					std::move(onDataSavedFunc), isThisValid_);

			// Save the points first so that they receive their ids from the server,
			// which are then stored in each spline.
			saveAllCallback->AddRequestToWait();
			AsyncSaveSplinePoints(decorationId,
				[this, decorationId, saveAllCallback](bool bSuccess) mutable
			{
				AsyncSaveSplines(decorationId, saveAllCallback);
				saveAllCallback->OnRequestDone(bSuccess);
			});

			// Delete obsolete points and then splines
			saveAllCallback->AddRequestToWait();
			AsyncDeleteSplinePoints(decorationId,
				[this, decorationId, saveAllCallback](bool bSuccess)
			{
				AsyncDeleteSplines(decorationId, saveAllCallback);
				saveAllCallback->OnRequestDone(bSuccess);
			});

			saveAllCallback->OnFirstLevelRequestsRegistered();
		}

		size_t GetNumberOfSplines() const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto& splines_ = thdata->splines_;
			return splines_.size();
		}

		ISplinePtr GetSpline(const size_t index) const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto& splines_ = thdata->splines_;
			if (index < splines_.size())
			{
				return splines_[index];
			}
			return ISplinePtr();
		}

		ISplinePtr GetSplineById(const RefID& id) const
		{
			auto thdata = thdata_.GetRAutoLock();
			return FindSplineById(thdata->splines_, id);
		}

		/////////////////////////////////////////////////////////////////////////////////
		/// for use in TAsyncSaveItems
		ISplinePtr GetItemById(RefID const& id) const
		{
			return GetSplineById(id);
		}

		void OnItemDeletedOnDB(RefID const& deletedId) override
		{
			auto thdata = thdata_.GetAutoLock();
			std::erase_if(thdata->removedSplines_, [&deletedId](const auto& removedSplinePtr)
			{
				auto removedSpline = removedSplinePtr->GetRAutoLock();
				return removedSpline->GetId() == deletedId;
			});
		}
		/////////////////////////////////////////////////////////////////////////////////

		ISplinePtr GetSplineByDBId(const std::string& id) const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto& splines_ = thdata->splines_;
			auto it = std::find_if(splines_.begin(), splines_.end(),
				[&id](ISplinePtr const& spl) {
					auto splLocked = spl->GetRAutoLock();
					return splLocked->HasDBIdentifier() && splLocked->GetDBIdentifier() == id;
				});
			if (it != splines_.end())
				return *it;
			return {};
		}

		ISplinePtrVect GetSplines() const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto& splines_ = thdata->splines_;
			return splines_;
		}

		ISplinePtr AddSpline()
		{
			auto thdata = thdata_.GetAutoLock();
			auto& splines_ = thdata->splines_;
			ISplinePtr spline = MakeSharedLockableDataPtr(ISpline::New());
			splines_.push_back(spline);
			return spline;
		}

		void RemoveSpline(const size_t index)
		{
			auto thdata = thdata_.GetAutoLock();
			auto& splines_ = thdata->splines_;
			if (index < splines_.size())
			{
				AdvViz::SDK::ISplinePtrVect::const_iterator it = splines_.cbegin() + index;
				thdata->removedSplines_.push_back(*it);
				splines_.erase(it);
			}
		}

		void RemoveSpline(const ISplinePtr& spline)
		{
			auto thdata = thdata_.GetAutoLock();
			auto& splines_ = thdata->splines_;
			size_t index = 0;
			bool found = false;
			for (auto it = splines_.cbegin(); it != splines_.cend(); ++it, ++index)
			{
				if (it->get() && it->get() == spline.get())
				{
					found = true;
					break;
				}
			}
			if (found)
			{
				RemoveSpline(index);
			}
		}

		void RestoreSpline(const ISplinePtr& splinePtr)
		{
			auto thdata = thdata_.GetAutoLock();
			auto& removedSplines_ = thdata->removedSplines_;
			auto& splines_ = thdata->splines_;
			auto spline = splinePtr->GetRAutoLock();

			std::erase_if(removedSplines_, [&spline](const auto& removedSplinePtr)
			{
				auto removedSpline = removedSplinePtr->GetRAutoLock();
				return removedSpline->GetId() == spline->GetId();
			});
			auto existingSpline = GetSplineById(spline->GetId());
			if (!existingSpline)
			{
				splines_.push_back(splinePtr);
			}
		}

		bool HasSplines() const
		{
			auto thdata = thdata_.GetRAutoLock();
			return !thdata->splines_.empty();
		}

		bool HasSplinesToSave() const
		{
			auto thdata = thdata_.GetRAutoLock();
			for (const auto& splinePtr : thdata->splines_)
			{
				auto spline = splinePtr->GetRAutoLock();
				if (spline->ShouldSave())
					return true;
			}
			for (auto const& splinePtr : thdata->removedSplines_)
			{
				auto const& spline = splinePtr->GetRAutoLock();
				auto const& splineId = spline->GetId();
				if (splineId.HasDBIdentifier())
					return true;
			}
			return false;
		}

		RefID GetLoadedSplineId(std::string const& splineDBIdentifier) const
		{
			auto thdata = thdata_.GetRAutoLock();
			return RefID::FindFromDBIdentifier(splineDBIdentifier, thdata->splineIDMap_);
		}
	};

	void SplinesManager::LoadDataFromServer(const std::string& decorationId)
	{
		GetImpl().LoadDataFromServer(decorationId);
	}

	void SplinesManager::AsyncLoadDataFromServer(const std::string& decorationId, 
		const std::function<void(ISplinePtr&)>& onSplineLoaded,
		const std::function<void(ISplinePointPtr&)>& onSplinePointLoaded,
		const std::function<void(expected<void, std::string> const&)>& onComplete)
	{
		GetImpl().AsyncLoadDataFromServer(decorationId, onSplineLoaded, onSplinePointLoaded, onComplete);
	}

	void SplinesManager::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
	{
		GetImpl().AsyncSaveDataOnServer(decorationId, std::move(onDataSavedFunc));
	}

	size_t SplinesManager::GetNumberOfSplines() const
	{
		return GetImpl().GetNumberOfSplines();
	}

	ISplinePtr SplinesManager::GetSpline(const size_t index) const
	{
		return GetImpl().GetSpline(index);
	}

	ISplinePtr SplinesManager::GetSplineById(const RefID& id) const
	{
		return GetImpl().GetSplineById(id);
	}

	ISplinePtr SplinesManager::GetSplineByDBId(const std::string& id) const
	{
		return GetImpl().GetSplineByDBId(id);
	}

	ISplinePtrVect SplinesManager::GetSplines() const
	{
		return GetImpl().GetSplines();
	}

	ISplinePtr SplinesManager::AddSpline()
	{
		return GetImpl().AddSpline();
	}

	void SplinesManager::RemoveSpline(const size_t index)
	{
		GetImpl().RemoveSpline(index);
	}

	void SplinesManager::RemoveSpline(const ISplinePtr& spline)
	{
		GetImpl().RemoveSpline(spline);
	}

	void SplinesManager::RestoreSpline(const ISplinePtr& spline)
	{
		GetImpl().RestoreSpline(spline);
	}

	bool SplinesManager::HasSplines() const
	{
		return GetImpl().HasSplines();
	}

	bool SplinesManager::HasSplinesToSave() const
	{
		return GetImpl().HasSplinesToSave();
	}

	RefID SplinesManager::GetLoadedSplineId(std::string const& splineDBIdentifier) const
	{
		return GetImpl().GetLoadedSplineId(splineDBIdentifier);
	}

	void SplinesManager::SetHttp(std::shared_ptr<Http> const& http)
	{
		GetImpl().SetHttp(http);
	}

	SplinesManager::SplinesManager():impl_(new Impl())
	{
	}

	SplinesManager::~SplinesManager() 
	{}

	SplinesManager::Impl& SplinesManager::GetImpl()
	{
		return *impl_;
	}

	const SplinesManager::Impl& SplinesManager::GetImpl() const
	{
		return *impl_;
	}

	template<>
	Tools::Factory<ISplinesManager>::Globals::Globals()
	{
		newFct_ = []() {return static_cast<ISplinesManager*>(new SplinesManager());};
	}

	template<>
	Tools::Factory<ISplinesManager>::Globals& Tools::Factory<ISplinesManager>::GetGlobals()
	{
		return singleton<Tools::Factory<ISplinesManager>::Globals>();
	}

}
