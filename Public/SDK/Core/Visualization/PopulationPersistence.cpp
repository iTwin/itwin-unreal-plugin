/*--------------------------------------------------------------------------------------+
|
|     $Source: PopulationPersistence.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "PopulationPersistence.h"

#include "AsyncHelpers.h"
#include "AsyncHttp.inl"
#include "SplinesManager.h"
#include "Core/Network/HttpGetWithLink.h"
#include "Core/Singleton/singleton.h"
#include "Config.h"
#include "Core/Tools/FactoryClassInternalHelper.h"


namespace AdvViz::SDK
{
    class PopulationInfo::Impl : public std::enable_shared_from_this<PopulationInfo::Impl>
        , public SavableItemWithID
    {
    public:
        SPopulationData serverSideData_;
        RefID splineId_ = RefID::Invalid();

        void SetId(const RefID& id) override
        {
            SavableItemWithID::SetId(id);
            if (id.HasDBIdentifier())
                serverSideData_.id = id.GetDBIdentifier();
        }
    };

    PopulationInfo::PopulationInfo() :impl_(new Impl())
    {}

    PopulationInfo::~PopulationInfo()
    {}

    DEFINEFACTORYGLOBALS(PopulationInfo);

    const RefID& PopulationInfo::GetId() const
    {
        return GetImpl().GetId();
    }

    void PopulationInfo::SetId(const RefID& id)
    {
        GetImpl().SetId(id);
    }

    const RefID& PopulationInfo::GetSplineId() const
    {
        return GetImpl().splineId_;
    }

    void PopulationInfo::SetSplineId(const RefID& id)
    {
        GetImpl().splineId_ = id;
        if (id.HasDBIdentifier())
        {
            GetImpl().serverSideData_.splineId = id.GetDBIdentifier();
            GetImpl().InvalidateDB();
        }
    }

    void PopulationInfo::SetPopulationType(int type) { GetImpl().serverSideData_.type = type; GetImpl().InvalidateDB(); }
    int PopulationInfo::GetPopulationType() const { return GetImpl().serverSideData_.type.value_or(0); }

    void PopulationInfo::SetDensity(double v) { GetImpl().serverSideData_.density = v; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetDensity() const { return GetImpl().serverSideData_.density.value_or(0.5); }

    void PopulationInfo::SetDistance(double v) { GetImpl().serverSideData_.distance = v; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetDistance() const { return GetImpl().serverSideData_.distance.value_or(3.0); }

    void PopulationInfo::SetScale(double v) { GetImpl().serverSideData_.scale = v; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetScale() const { return GetImpl().serverSideData_.scale.value_or(1.0); }

    void PopulationInfo::SetRotation(double v) { GetImpl().serverSideData_.rotation = v; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetRotation() const { return GetImpl().serverSideData_.rotation.value_or(0.0); }

    void PopulationInfo::SetGridRotation(double v) { GetImpl().serverSideData_.gridRotation = v; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetGridRotation() const { return GetImpl().serverSideData_.gridRotation.value_or(0.0); }

    void PopulationInfo::SetMode(int mode) { GetImpl().serverSideData_.mode = mode; GetImpl().InvalidateDB(); }
    int PopulationInfo::GetMode() const { return GetImpl().serverSideData_.mode.value_or(1); }

    void PopulationInfo::SetRotationMode(int mode) { GetImpl().serverSideData_.rotationMode = mode; GetImpl().InvalidateDB(); }
    int PopulationInfo::GetRotationMode() const { return GetImpl().serverSideData_.rotationMode.value_or(0); }

    void PopulationInfo::SetIsVisible(bool b) { GetImpl().serverSideData_.isEnabled = b; GetImpl().InvalidateDB(); }
    bool PopulationInfo::IsVisible() const { return GetImpl().serverSideData_.isEnabled.value_or(true); }

    void PopulationInfo::SetIsScaleRandomized(bool b) { GetImpl().serverSideData_.isScaleRandomized = b; GetImpl().InvalidateDB(); }
    bool PopulationInfo::IsScaleRandomized() const { return GetImpl().serverSideData_.isScaleRandomized.value_or(false); }

    void PopulationInfo::SetIsRotationRandomized(bool b) { GetImpl().serverSideData_.isRotationRandomized = b; GetImpl().InvalidateDB(); }
    bool PopulationInfo::IsRotationRandomized() const { return GetImpl().serverSideData_.isRotationRandomized.value_or(false); }

    void PopulationInfo::SetIsSpacingRandomized(bool b) { GetImpl().serverSideData_.isSpacingRandomized = b; GetImpl().InvalidateDB(); }
    bool PopulationInfo::IsSpacingRandomized() const { return GetImpl().serverSideData_.isSpacingRandomized.value_or(false); }

    void PopulationInfo::SetAvoidOverlapping(bool b) { GetImpl().serverSideData_.avoidOverlapping = b; GetImpl().InvalidateDB(); }
    bool PopulationInfo::IsAvoidOverlapping() const { return GetImpl().serverSideData_.avoidOverlapping.value_or(false); }

    void PopulationInfo::SetScaleRange(double minVal, double maxVal) { GetImpl().serverSideData_.scaleRangeMin = minVal; GetImpl().serverSideData_.scaleRangeMax = maxVal; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetScaleRangeMin() const { return GetImpl().serverSideData_.scaleRangeMin.value_or(0.9); }
    double PopulationInfo::GetScaleRangeMax() const { return GetImpl().serverSideData_.scaleRangeMax.value_or(1.1); }

    void PopulationInfo::SetRotationRange(double minVal, double maxVal) { GetImpl().serverSideData_.rotationRangeMin = minVal; GetImpl().serverSideData_.rotationRangeMax = maxVal; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetRotationRangeMin() const { return GetImpl().serverSideData_.rotationRangeMin.value_or(-180.0); }
    double PopulationInfo::GetRotationRangeMax() const { return GetImpl().serverSideData_.rotationRangeMax.value_or(180.0); }

    void PopulationInfo::SetDistanceRange(double minVal, double maxVal) { GetImpl().serverSideData_.distanceRangeMin = minVal; GetImpl().serverSideData_.distanceRangeMax = maxVal; GetImpl().InvalidateDB(); }
    double PopulationInfo::GetDistanceRangeMin() const { return GetImpl().serverSideData_.distanceRangeMin.value_or(2.5); }
    double PopulationInfo::GetDistanceRangeMax() const { return GetImpl().serverSideData_.distanceRangeMax.value_or(3.5); }

    void PopulationInfo::SetObjects(const std::vector<std::string>& objs)
    {
        GetImpl().serverSideData_.objects = objs;
        GetImpl().InvalidateDB();
    }

    void PopulationInfo::GetObjects(std::vector<std::string>& objs) const
    {
        objs.clear();
        if (GetImpl().serverSideData_.objects.has_value())
            objs = GetImpl().serverSideData_.objects.value();
    }

    ESaveStatus PopulationInfo::GetSaveStatus() const
    {
        return GetImpl().GetSaveStatus();
    }
    void PopulationInfo::SetSaveStatus(ESaveStatus status)
    {
        GetImpl().SetSaveStatus(status);
    }

    void PopulationInfo::SetServerSideData(const IPopulationInfo::SPopulationData& data)
    {
        GetImpl().serverSideData_ = data;
    }

    const IPopulationInfo::SPopulationData& PopulationInfo::GetServerSideData() const
    {
        return GetImpl().serverSideData_;
    }

    PopulationInfo::Impl& PopulationInfo::GetImpl()
    {
        return *impl_;
    }

    const PopulationInfo::Impl& PopulationInfo::GetImpl() const
    {
        return *impl_;
    }


    class PopulationManager::Impl : public std::enable_shared_from_this<PopulationManager::Impl>
    {
    public:
        std::shared_ptr<Http> http_;
        //std::weak_ptr<IInstancesManager> instanceManager_;
        std::weak_ptr<ISplinesManager> splinesManager_;

        struct SThreadSafeData {
            std::unordered_map<RefID, IPopulationInfoPtr> infosMap_;
            std::unordered_map<RefID, IPopulationInfoPtr> removedInfosMap_;
        };

        Tools::RWLockableObject<SThreadSafeData> thdata_;

        std::shared_ptr< std::atomic_bool > isThisValid_;

        struct SJsonIds { std::vector<std::string> ids; };

        Impl()
        {
            isThisValid_ = std::make_shared<std::atomic_bool>(true);
            SetHttp(GetDefaultHttp());
        }

        ~Impl()
        {
            *isThisValid_ = false;
        }

        std::shared_ptr<Http> const& GetHttp() const { return http_; }
        void SetHttp(std::shared_ptr<Http> const& http) { http_ = http; }

        IPopulationInfoPtr FindPopulationInfoByDBId(const std::string& id) const
        {
            auto thdata = thdata_.GetRAutoLock();
            for (const auto& [_, popInfoPtr] : thdata->infosMap_)
            {
                auto popInfo = popInfoPtr->GetRAutoLock();
                if (popInfo->HasDBIdentifier() && popInfo->GetDBIdentifier() == id)
                    return popInfoPtr;
            }
            return IPopulationInfoPtr();
        }

        IPopulationInfoPtr FindPopulationInfoBySplineRefId(const RefID& id) const
        {
            auto thdata = thdata_.GetRAutoLock();
            for (const auto& [_, popInfoPtr] : thdata->infosMap_)
            {
                auto popInfo = popInfoPtr->GetRAutoLock();
                if (popInfo->GetSplineId() == id)
                    return popInfoPtr;
            }
            return IPopulationInfoPtr();
        }

        IPopulationInfoPtr AddPopulationInfo()
        {
            IPopulationInfo* PopInfo(IPopulationInfo::New());
            IPopulationInfoPtr popPtr = MakeSharedLockableDataPtr<IPopulationInfo>(PopInfo);
            auto thdata = thdata_.GetAutoLock();
            thdata->infosMap_[PopInfo->GetId()] = popPtr;
            return popPtr;
        }

        void RemovePopulationInfo(const RefID& id)
        {
            auto thdata = thdata_.GetAutoLock();
            thdata->removedInfosMap_[id] = thdata->infosMap_[id];
            thdata->infosMap_.erase(id);
        }

        IPopulationInfoPtr GetPopulationInfo(const RefID& id) const
        {
            auto thdata = thdata_.GetRAutoLock();
            auto it = thdata->infosMap_.find(id);
            return (it != thdata->infosMap_.end()) ? it->second : IPopulationInfoPtr();
        }

        void GetPopulationIds(std::set<AdvViz::SDK::RefID>& ids) const
        {
            ids.clear();
            auto thdata = thdata_.GetRAutoLock();
            for (const auto& [key, _] : thdata->infosMap_)
            {
                ids.insert(key);
            }
        }

        bool HasPopulationsToSave() const
        {
            auto thdata = thdata_.GetRAutoLock();
            for (const auto& [_, itPtr] : thdata->infosMap_)
            {
                auto it = itPtr->GetRAutoLock();
                if (it->ShouldSave())
                {
                    return true;
                }
            }
            for (const auto& [_, itPtr] : thdata->removedInfosMap_)
            {
                auto it = itPtr->GetRAutoLock();
                if (it->HasDBIdentifier())
                {
                    return true;
                }
            }
            return false;
        }

        void LoadDataFromServer(const std::string& decorationId);
        void AsyncLoadDataFromServer(const std::string& decorationId,
            const std::function<void(IPopulationInfoPtr&)>& onPopulationLoaded,
            const std::function<void(expected<void, std::string> const&)>& onComplete);
        void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc);
    };

    void PopulationManager::Impl::LoadDataFromServer(const std::string& decorationId)
    {
        auto ret = HttpGetWithLink<IPopulationInfo::SPopulationData>(GetHttp(),
            "decorations/" + decorationId + "/splinepopulations",
            {} /* extra headers*/,
            [this](IPopulationInfo::SPopulationData const& row) -> expected<void, std::string>
        {
            if (!row.id)
                return make_unexpected("Server returned no id for population.");
            auto popInfoPtr = AddPopulationInfo();
            auto popInfo = popInfoPtr->GetAutoLock();
            popInfo->SetServerSideData(row);
            popInfo->SetDBIdentifier(row.id.value());
            // init spline RefId
            auto splinesManager(splinesManager_.lock());
            if (row.splineId.has_value())
            {
                if (auto splinePtr = splinesManager->GetSplineByDBId(row.splineId.value()))
                {
                    auto spline = splinePtr->GetRAutoLock();
                    popInfo->SetSplineId(spline->GetId());
                }
            }
            popInfo->SetShouldSave(false);
            return {};
        });

        if (!ret)
        {
            BE_LOGW("ITwinDecoration", "Loading of spline populations failed. " << ret.error());
        }
    }

    void PopulationManager::Impl::AsyncLoadDataFromServer(const std::string& decorationId,
        const std::function<void(IPopulationInfoPtr&)>& onPopulationLoaded,
        const std::function<void(expected<void, std::string> const&)>& onComplete)
    {
        auto SThis = this->shared_from_this();
        AsyncHttpGetWithLink<IPopulationInfo::SPopulationData>(GetHttp(),
            "decorations/" + decorationId + "/splinepopulations",
            {} /* extra headers*/,
            [SThis, onPopulationLoaded](IPopulationInfo::SPopulationData const& row) -> expected<void, std::string>
        {
            if (!row.id)
                return make_unexpected("Server returned no id for population.");
            auto popInfoPtr = SThis->AddPopulationInfo();
            auto popInfo = popInfoPtr->GetAutoLock();
            popInfo->SetServerSideData(row);
            auto refID = popInfo->GetId();
            refID.SetDBIdentifier(row.id.value());
            popInfo->SetId(refID);
            // init spline RefId
            auto splinesManager(SThis->splinesManager_.lock());
            if (!splinesManager)
                return make_unexpected("Splines manager is not set.");
            if (row.splineId.has_value())
            {
                auto splinePtr = splinesManager->GetSplineByDBId(row.splineId.value());
                if (splinePtr)
                {
                    auto spline = splinePtr->GetRAutoLock();
                    popInfo->SetSplineId(spline->GetId());
                    popInfo->SetShouldSave(false);
                }
                else
                {
                    BE_LOGW("ITwinDecoration", "Could not find spline with DB id " << row.splineId.value() << " for population with id " << row.id.value());
                }
            }
            if (onPopulationLoaded)
                onPopulationLoaded(popInfoPtr);
            return {};
        },
            onComplete
        );
    }

    void PopulationManager::Impl::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
    {
        std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
            std::make_shared<AsyncRequestGroupCallback>(
                std::move(onDataSavedFunc), isThisValid_);

        struct SJsonPopulationVect
        {
            std::vector<IPopulationInfo::SPopulationData> SplinePopulations;
        };
        SJsonPopulationVect jInPost, jInPut;

        std::vector<RefID> newIndices;
        std::vector<RefID> updatedIndices;

        auto thdata = thdata_.GetRAutoLock();
        auto infosMap_ = thdata->infosMap_;
        // Sort populations for requests (addition/update)
        for (auto const& elem : infosMap_)
        {
            auto infoPtr = elem.second->GetAutoLock();
            // init spline database id for new splines
            auto splinesManager(splinesManager_.lock());
            if (!splinesManager)
                continue;
            auto splinePtr = splinesManager->GetSplineById(infoPtr->GetSplineId());
            if (splinePtr)
            {
                auto spline = splinePtr->GetRAutoLock();
                infoPtr->SetSplineId(spline->GetId());

                if (!infoPtr->HasDBIdentifier())
                {
                    jInPost.SplinePopulations.emplace_back(infoPtr->GetServerSideData());
                    newIndices.push_back(elem.first);
                    infoPtr->OnStartSave();
                }
                else if (infoPtr->ShouldSave())
                {
                    jInPut.SplinePopulations.emplace_back(infoPtr->GetServerSideData());
                    updatedIndices.push_back(elem.first);
                    infoPtr->OnStartSave();
                }
            }
        }

        // Post (new populations)
        if (!jInPost.SplinePopulations.empty())
        {
            AsyncPostJsonJBody<SJsonIds>(GetHttp(), callbackPtr,
                [this, newIndices](
                    long httpCode,
                    const Tools::TSharedLockableData<SJsonIds>& joutPtr)
            {
                const bool bSuccess = (httpCode == 200 || httpCode == 201);
                if (bSuccess)
                {
                    auto unlockedJout = joutPtr->GetAutoLock();
                    SJsonIds& jOutPost = unlockedJout.Get();
                    if (newIndices.size() == jOutPost.ids.size())
                    {
                        for (size_t i = 0; i < newIndices.size(); ++i)
                        {
                            if (auto popInfoPtr = GetPopulationInfo(newIndices[i]))
                            {
                                auto popInfo = popInfoPtr->GetAutoLock();
                                popInfo->SetDBIdentifier(jOutPost.ids[i]);
                                popInfo->OnSaved();
                            }
                        }
                    }
                }
                else
                {
                    BE_LOGW("ITwinDecoration", "Saving new spline populations failed. Http status: " << httpCode);
                }
                return bSuccess;
            },
                "decorations/" + decorationId + "/splinepopulations",
                jInPost);
        }

        // Put (updated populations)
        if (!jInPut.SplinePopulations.empty())
        {
            struct SJsonPopulationOutUpd
            {
                int64_t numUpdated = 0;
            };
            AsyncPutJsonJBody<SJsonPopulationOutUpd>(GetHttp(), callbackPtr,
                [this, updatedIndices](
                    long httpCode,
                    const Tools::TSharedLockableData<SJsonPopulationOutUpd>& joutPtr)
            {
                const bool bSuccess = (httpCode == 200 || httpCode == 201);
                if (bSuccess)
                {
                    auto unlockedJout = joutPtr->GetAutoLock();
                    SJsonPopulationOutUpd& jOutPut = unlockedJout.Get();
                    if (updatedIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
                    {
                        for (RefID const& popId : updatedIndices)
                        {
                            if (auto popInfoPtr = GetPopulationInfo(popId))
                            {
                                auto popInfo = popInfoPtr->GetAutoLock();
                                popInfo->OnSaved();
                            }
                        }
                    }
                }
                else
                {
                    BE_LOGW("ITwinDecoration", "Updating spline populations failed. Http status: " << httpCode);
                }
                return bSuccess;
            },
                "decorations/" + decorationId + "/splinepopulations",
                jInPut);
        }

        // delete obsolete populations
        SJsonIds jIn;
        std::vector<RefID> deletedPopIds;
        auto removedInfosMap_ = thdata->removedInfosMap_;
        jIn.ids.reserve(removedInfosMap_.size());
        deletedPopIds.reserve(removedInfosMap_.size());
        for (auto const& elem : removedInfosMap_)
        {
            auto infoPtr = elem.second->GetAutoLock();
            auto const& refId = infoPtr->GetId();
            deletedPopIds.push_back(refId);
            if (refId.HasDBIdentifier())
                jIn.ids.push_back(refId.GetDBIdentifier());
        }

        if (!jIn.ids.empty())
        {
            AsyncDeleteJsonNoOutput(GetHttp(), callbackPtr,
                [this, deletedPopIds](long httpCode)
            {
                const bool bSuccess = (httpCode == 200 || httpCode == 201 || httpCode == 204 /* No-Content*/);
                if (bSuccess)
                {
                    auto thdata = thdata_.GetAutoLock();
                    auto removedInfosMap_ = thdata->removedInfosMap_;
                    for (RefID const& deletedId : deletedPopIds)
                        removedInfosMap_.erase(deletedId);
                }
                else
                {
                    BE_LOGW("ITwinDecoration", "Deleting spline populations failed. Http status: " << httpCode);
                }
                return bSuccess;
            },
                "decorations/" + decorationId + "/splinepopulations",
                jIn);
        }

        callbackPtr->OnFirstLevelRequestsRegistered();
    }


    PopulationManager::PopulationManager() : impl_(new Impl)
    {
    }

    /*void PopulationManager::SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager)
    {
        GetImpl().instanceManager_ = instanceManager;
    }*/

    void PopulationManager::SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager)
    {
        GetImpl().splinesManager_ = splinesManager;
    }

    size_t PopulationManager::GetNumberOfPopulations() const
    {
        auto thdata = GetImpl().thdata_.GetRAutoLock();
        return thdata->infosMap_.size();
    }

    IPopulationInfoPtr PopulationManager::FindPopulationInfoByDBId(const std::string& id) const
    {
        return GetImpl().FindPopulationInfoByDBId(id);
    }

    IPopulationInfoPtr PopulationManager::FindPopulationInfoBySplineRefId(const RefID& id) const
    {
        return GetImpl().FindPopulationInfoBySplineRefId(id);
    }

    IPopulationInfoPtr PopulationManager::AddPopulationInfo()
    {
        return GetImpl().AddPopulationInfo();
    }

    void PopulationManager::RemovePopulationInfo(const RefID& id)
    {
        GetImpl().RemovePopulationInfo(id);
    }

    IPopulationInfoPtr PopulationManager::GetPopulationInfo(const RefID& id) const
    {
        return GetImpl().GetPopulationInfo(id);
    }

    void PopulationManager::GetPopulationIds(std::set<AdvViz::SDK::RefID>& ids) const
    {
        GetImpl().GetPopulationIds(ids);
    }

    void PopulationManager::LoadDataFromServer(const std::string& decorationId)
    {
        GetImpl().LoadDataFromServer(decorationId);
    }

    void PopulationManager::AsyncLoadDataFromServer(const std::string& decorationId,
        const std::function<void(IPopulationInfoPtr&)>& onPopulationLoaded,
        const std::function<void(expected<void, std::string> const&)>& onComplete)
    {
        GetImpl().AsyncLoadDataFromServer(decorationId, onPopulationLoaded, onComplete);
    }

    void PopulationManager::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
    {
        GetImpl().AsyncSaveDataOnServer(decorationId, std::move(onDataSavedFunc));
    }

    bool PopulationManager::HasPopulationsToSave() const
    {
        return GetImpl().HasPopulationsToSave();
    }

    PopulationManager::Impl& PopulationManager::GetImpl()
    {
        return *impl_;
    }

    const PopulationManager::Impl& PopulationManager::GetImpl() const
    {
        return *impl_;
    }

    DEFINEFACTORYGLOBALS(PopulationManager);
}
