/*--------------------------------------------------------------------------------------+
|
|     $Source: SplinesManager.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SplinesManager.h"
#include "Core/Network/HttpGetWithLink.h"
#include "Config.h"
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
{
	class SplinesManager::Impl
	{
	public:
		std::shared_ptr<Http> http_;
		SharedSplineVect splines_;
		SharedSplineVect removedSplines_;
		RefID::DBToIDMap splineIDMap_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void Clear()
		{
			splines_.clear();
			splineIDMap_.clear();
		}

		// Some structures used in I/O functions
		struct SJsonIds { std::vector<std::string> ids; };

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

		struct SJsonEmpty {};

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

		template<class T>
		void CopyPoint(T& dst, const ISplinePoint& src)
		{
			dst.position = src.GetPosition();
			dst.upVector = src.GetUpVector();
			dst.inTangent = src.GetInTangent();
			dst.outTangent = src.GetOutTangent();
			dst.inTangentMode = GetStringFromTangentMode(src.GetInTangentMode());
			dst.outTangentMode = GetStringFromTangentMode(src.GetOutTangentMode());
		}

		template<class T>
		void CopyPoint(ISplinePoint& dst, const T& src)
		{
			dst.SetPosition(src.position);
			dst.SetUpVector(src.upVector);
			dst.SetInTangent(src.inTangent);
			dst.SetOutTangent(src.outTangent);
			dst.SetInTangentMode(GetTangentModeFromString(src.inTangentMode));
			dst.SetOutTangentMode(GetTangentModeFromString(src.outTangentMode));
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
			for (auto const& point : src.GetPoints())
			{
				std::string const& pointId = point->GetId();
				if (!pointId.empty())
				{
					dst.pointIDs.push_back(pointId);
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

		void FromJsonSpline(ISpline& dst, const SJsonSpline& src)
		{
			if (src.id)
			{
				dst.SetId(RefID::FromDBIdentifier(*src.id, splineIDMap_));
			}
			if (src.name)
			{
				dst.SetName(*src.name);
			}
			dst.SetUsage(GetUsageFromString(src.usage));
			if (src.closedLoop)
			{
				dst.SetClosedLoop(*src.closedLoop);
			}
			dst.SetTransform(src.transform);
			for (size_t i = 0; i < src.pointIDs.size(); ++i)
			{
				if (!src.pointIDs[i].empty())
				{
					dst.AddPoint()->SetId(src.pointIDs[i]);
				}
			}
			std::vector<SplineLinkedModel> linkedModels;
			if (src.linkedModels)
			{
				linkedModels = *src.linkedModels;
			}
			// For compatibility with earlier versions: by default, cut-out was applied to the Google
			// tileset only.
			if (dst.GetUsage() == ESplineUsage::MapCutout
				&& linkedModels.empty())
			{
				linkedModels.push_back({ .modelType = "GlobalMapLayer" });
			}
			dst.SetLinkedModels(linkedModels);
			dst.EnableEffect(src.enableEffect.value_or(true));
			dst.SetInvertEffect(src.invertEffect.value_or(false));
		}

		void LoadSplines(const std::string& decorationId)
		{
			auto ret = HttpGetWithLink<SJsonSpline>(GetHttp(),
				"decorations/" + decorationId + "/splines",
				{} /* extra headers*/,
				[this](SJsonSpline const& row) -> expected<void, std::string>
				{
					if (!row.id)
						return make_unexpected(std::string("Server returned no id for spline."));
					SharedSpline spline = AddSpline();
					FromJsonSpline(*spline, row);
					spline->SetShouldSave(false);
					return {};
				});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Loading of splines failed. " << ret.error());
			}
		}

		void LoadSplinePoints(const std::string& decorationId)
		{
			std::map<std::string, SharedSplinePoint> mapIdToPoint;

			auto ret = HttpGetWithLink<SJsonPointWithId>(GetHttp(),
				"decorations/" + decorationId + "/splinepoints",
				{} /* extra headers*/,
				[this, &mapIdToPoint](SJsonPointWithId const& row) -> expected<void, std::string>
			{
				if (row.id.empty())
					return {};

				SharedSplinePoint point(ISplinePoint::New());
				point->SetId(row.id);
				CopyPoint<SJsonPointWithId>(*point, row);
				mapIdToPoint[row.id] = point;

				return {};
			});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Loading of spline points failed. " << ret.error());
			}

			// Put the loaded points in splines (their current points only have valid IDs but no valid data).
			for (auto& spline : splines_)
			{
				size_t const numPoints = spline->GetNumberOfPoints();
				for (size_t i = 0; i < numPoints; ++i)
				{
					SharedSplinePoint point = spline->GetPoint(i);
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

		void SaveSplinePoints(const std::string& decorationId)
		{
			struct SJsonPointVect { std::vector<SJsonPoint> splinePoints; };
			struct SJsonPointWithIdVect { std::vector<SJsonPointWithId> splinePoints; };
			SJsonPointVect jInPost;
			SJsonPointWithIdVect jInPut;

			size_t splineCount = 0;
			size_t pointCount = 0;
			std::vector<std::pair<size_t, size_t>> newPointIndices;
			std::vector<std::pair<size_t, size_t>> updatedPointIndices;

			// Sort points for requests (addition/update)
			for (auto const& spline : splines_)
			{
				for (SharedSplinePoint const& point : spline->GetPoints())
				{
					if (point->GetId().empty())
					{
						SJsonPoint jPoint;
						CopyPoint<SJsonPoint>(jPoint, *point);
						jInPost.splinePoints.push_back(jPoint);
						newPointIndices.push_back(std::make_pair(splineCount, pointCount));
					}
					else if (point->ShouldSave())
					{
						SJsonPointWithId jPointWId;
						CopyPoint<SJsonPointWithId>(jPointWId, *point);
						jPointWId.id = point->GetId();
						jInPut.splinePoints.push_back(jPointWId);
						updatedPointIndices.push_back(std::make_pair(splineCount, pointCount));
					}
					++pointCount;
				}
				++splineCount;
				pointCount = 0;
			}

			// Post (new points)
			if (!jInPost.splinePoints.empty())
			{
				SJsonIds jOutPost;
				long status = GetHttp()->PostJsonJBody(
					jOutPost, "decorations/" + decorationId + "/splinepoints", jInPost);

				if (status == 200 || status == 201)
				{
					if (newPointIndices.size() == jOutPost.ids.size())
					{
						for (size_t i = 0; i < newPointIndices.size(); ++i)
						{
							const std::pair<size_t, size_t>& index = newPointIndices[i];
							SharedSplinePoint point = splines_[index.first]->GetPoint(index.second);
							point->SetId(jOutPost.ids[i]);
							point->SetShouldSave(false);
							splines_[index.first]->SetShouldSave(true); // the point is added so the spline won't be saved unless we tell her to.
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new points failed. Http status: " << status);
				}
			}

			// Put (updated points)
			if (!jInPut.splinePoints.empty())
			{
				struct SJsonPointOutUpd { int64_t numUpdated; };
				SJsonPointOutUpd jOutPut;
				long status = GetHttp()->PutJsonJBody(
					jOutPut, "decorations/" + decorationId  + "/splinepoints", jInPut);

				if (status == 200 || status == 201)
				{
					if (updatedPointIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for (size_t i = 0; i < updatedPointIndices.size(); ++i)
						{
							const std::pair<size_t, size_t>& index = updatedPointIndices[i];
							SharedSplinePoint point = splines_[index.first]->GetPoint(index.second);
							point->SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating points failed. Http status: " << status);
				}
			}
		}

		void SaveSplines(const std::string& decorationId)
		{
			struct SJsonSplineVect
			{
				std::vector<SJsonSpline> splines;
			};
			SJsonSplineVect jInPost, jInPut;

			size_t splineIndex = 0;
			std::vector<size_t> newSplineIndices;
			std::vector<size_t> updatedSplineIndices;

			// Sort splines for requests (addition/update)
			for (auto const& spline : splines_)
			{
				if (!spline->GetId().HasDBIdentifier())
				{
					jInPost.splines.emplace_back(ToJsonSpline(*spline));
					newSplineIndices.push_back(splineIndex);
				}
				else if (spline->ShouldSave())
				{
					jInPut.splines.emplace_back(ToJsonSpline(*spline));
					updatedSplineIndices.push_back(splineIndex);
				}
				++splineIndex;
			}

			// Post (new splines)
			if (!jInPost.splines.empty())
			{
				SJsonIds jOutPost;
				long status = GetHttp()->PostJsonJBody(
					jOutPost, "decorations/" + decorationId + "/splines", jInPost);

				if (status == 200 || status == 201)
				{
					if (newSplineIndices.size() == jOutPost.ids.size())
					{
						for (size_t i = 0; i < newSplineIndices.size(); ++i)
						{
							ISpline& spline = *splines_[newSplineIndices[i]];

							// Update the DB identifier only.
							RefID splineId = spline.GetId();
							splineId.SetDBIdentifier(jOutPost.ids[i]);
							spline.SetId(splineId);

							spline.SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new splines failed. Http status: " << status);
				}
			}

			// Put (updated splines)
			if (!jInPut.splines.empty())
			{
				struct SJsonSplineOutUpd { int64_t numUpdated = 0; };
				SJsonSplineOutUpd jOutPut;
				long status = GetHttp()->PutJsonJBody(
					jOutPut, "decorations/" + decorationId  + "/splines", jInPut);

				if (status == 200 || status == 201)
				{
					if (updatedSplineIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for (size_t i = 0; i < updatedSplineIndices.size(); ++i)
						{
							splines_[updatedSplineIndices[i]]->SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating splines failed. Http status: " << status);
				}
			}
		}

		void DeleteSplinePoints(const std::string& decorationId)
		{
			SJsonIds jIn;
			SJsonEmpty jOut;

			// Get the ids of removed points in splines that are still used.
			for (auto const& spline : splines_)
			{
				const SharedSplinePointVect& points = spline->GetRemovedPoints();
				for (auto const& point : points)
				{
					std::string pointId = point->GetId();
					if (!pointId.empty())
						jIn.ids.push_back(pointId);
				}
			}

			if (jIn.ids.empty())
			{
				return;
			}

			// Delete the points from the server
			long status = GetHttp()->DeleteJsonJBody(
				jOut, "decorations/" + decorationId + "/splinepoints", jIn);

			if (status == 200 || status == 201)
			{
				for (auto const& spline : splines_)
				{
					spline->ClearRemovedPoints();
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Deleting spline points failed. Http status: " << status);
			}
		}

		void DeleteSplines(const std::string& decorationId)
		{
			SJsonIds jIn;
			SJsonEmpty jOut;

			jIn.ids.reserve(removedSplines_.size());
			for (auto const& spline : removedSplines_)
			{
				auto const& splineId = spline->GetId();
				if (splineId.HasDBIdentifier())
					jIn.ids.push_back(splineId.GetDBIdentifier());
			}

			if (jIn.ids.empty())
			{
				return;
			}

			long status = GetHttp()->DeleteJsonJBody(
				jOut, "decorations/" + decorationId + "/splines", jIn);

			if (status == 200 || status == 201)
			{
				removedSplines_.clear();
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Deleting splines failed. Http status: " << status);
			}
		}

		void SaveDataOnServer(const std::string& decorationId)
		{
			// Save the points first so that they receive their ids from the server,
			// which are the stored in each spline.
			SaveSplinePoints(decorationId);
			SaveSplines(decorationId);

			// Delete obsolete points and splines
			DeleteSplinePoints(decorationId);
			DeleteSplines(decorationId);
		}

		size_t GetNumberOfSplines() const
		{
			return splines_.size();
		}

		SharedSpline GetSpline(const size_t index) const
		{
			if (index < splines_.size())
			{
				return splines_[index];
			}
			return SharedSpline();
		}

		SharedSpline GetSplineById(const RefID& id) const
		{
			// TODO_JDE - optimize lookup with map
			auto it = std::find_if(splines_.begin(), splines_.end(),
				[&id](SharedSpline const& spl) {
				return spl->GetId() == id;
			});
			if (it != splines_.end())
				return *it;
			return {};
		}

		SharedSpline GetSplineByDBId(const std::string& id) const
		{
			auto it = std::find_if(splines_.begin(), splines_.end(),
				[&id](SharedSpline const& spl) {
					return spl->GetId().HasDBIdentifier() && spl->GetId().GetDBIdentifier() == id;
				});
			if (it != splines_.end())
				return *it;
			return {};
		}

		SharedSplineVect const& GetSplines() const
		{
			return splines_;
		}

		SharedSpline AddSpline()
		{
			SharedSpline spline(ISpline::New());
			splines_.push_back(spline);
			return spline;
		}

		void RemoveSpline(const size_t index)
		{
			if (index < splines_.size())
			{
				AdvViz::SDK::SharedSplineVect::const_iterator it = splines_.cbegin() + index;
				removedSplines_.push_back(*it);
				splines_.erase(it);
			}
		}

		void RemoveSpline(const SharedSpline& spline)
		{
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

		void RestoreSpline(const SharedSpline& spline)
		{
			std::erase_if(removedSplines_, [&spline](const auto& removedSpline)
			{
				return removedSpline->GetId() == spline->GetId();
			});
			auto existingSpline = GetSplineById(spline->GetId());
			if (!existingSpline)
			{
				splines_.push_back(spline);
			}
		}

		bool HasSplines() const
		{
			return !splines_.empty();
		}

		bool HasSplinesToSave() const
		{
			for (const auto& spline : splines_)
			{
				if (spline->ShouldSave())
					return true;
			}
			for (auto const& spline : removedSplines_)
			{
				auto const& splineId = spline->GetId();
				if (splineId.HasDBIdentifier())
					return true;
			}
			return false;
		}

		RefID GetLoadedSplineId(std::string const& splineDBIdentifier) const
		{
			return RefID::FindFromDBIdentifier(splineDBIdentifier, splineIDMap_);
		}
	};

	void SplinesManager::LoadDataFromServer(const std::string& decorationId)
	{
		GetImpl().LoadDataFromServer(decorationId);
	}

	void SplinesManager::SaveDataOnServer(const std::string& decorationId)
	{
		GetImpl().SaveDataOnServer(decorationId);
	}

	size_t SplinesManager::GetNumberOfSplines() const
	{
		return GetImpl().GetNumberOfSplines();
	}

	SharedSpline SplinesManager::GetSpline(const size_t index) const
	{
		return GetImpl().GetSpline(index);
	}

	SharedSpline SplinesManager::GetSplineById(const RefID& id) const
	{
		return GetImpl().GetSplineById(id);
	}

	SharedSpline SplinesManager::GetSplineByDBId(const std::string& id) const
	{
		return GetImpl().GetSplineByDBId(id);
	}

	SharedSplineVect const& SplinesManager::GetSplines() const
	{
		return GetImpl().GetSplines();
	}

	SharedSpline SplinesManager::AddSpline()
	{
		return GetImpl().AddSpline();
	}

	void SplinesManager::RemoveSpline(const size_t index)
	{
		GetImpl().RemoveSpline(index);
	}

	void SplinesManager::RemoveSpline(const SharedSpline& spline)
	{
		GetImpl().RemoveSpline(spline);
	}

	void SplinesManager::RestoreSpline(const SharedSpline& spline)
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
		GetImpl().SetHttp(GetDefaultHttp());
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
