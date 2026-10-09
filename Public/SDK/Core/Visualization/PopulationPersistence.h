/*--------------------------------------------------------------------------------------+
|
|     $Source: PopulationPersistence.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <functional>
#include <string>
#include <set>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include <Core/Visualization/SavableItem.h>

MODULE_EXPORT namespace AdvViz::SDK
{
    class ISplinesManager;
    //class IInstancesManager;
    class Http;

    using namespace Tools;

    class IPopulationInfo : public Tools::Factory<IPopulationInfo>
        , public Tools::ExtensionSupport
        , public ISavableItem
    {
    public:
        struct SPopulationData
        {
            std::optional<std::string> id;          // population id defined by the server
            std::optional<std::string> splineId;    // spline id defined by the server
            std::optional<int> type;                // EITwinPopulationType (Area=0, Path=1)
            std::optional<int> mode;                // EITwinSplinePopulationMode (for Area)
            std::optional<int> rotationMode;        // EITwinPathPopulationRotationMode (for Path)
            std::optional<double> density;
            std::optional<double> distance;
            std::optional<double> scale;
            std::optional<double> rotation;
            std::optional<double> gridRotation;
            std::optional<bool> isEnabled;
            std::optional<bool> isScaleRandomized;
            std::optional<bool> isRotationRandomized;
            std::optional<bool> isSpacingRandomized;
            std::optional<bool> avoidOverlapping;
            std::optional<double> scaleRangeMin;
            std::optional<double> scaleRangeMax;
            std::optional<double> rotationRangeMin;
            std::optional<double> rotationRangeMax;
            std::optional<double> distanceRangeMin;
            std::optional<double> distanceRangeMax;
            std::optional<std::vector<std::string>> objects;
            std::optional<std::string> instGroupId;
        };

        virtual const RefID& GetSplineId() const = 0;
        virtual void SetSplineId(const RefID& id) = 0;

        virtual void SetPopulationType(int type) = 0;
        virtual int GetPopulationType() const = 0;

        virtual void SetDensity(double v) = 0;
        virtual double GetDensity() const = 0;

        virtual void SetDistance(double v) = 0;
        virtual double GetDistance() const = 0;

        virtual void SetScale(double v) = 0;
        virtual double GetScale() const = 0;

        virtual void SetRotation(double v) = 0;
        virtual double GetRotation() const = 0;

        virtual void SetGridRotation(double v) = 0;
        virtual double GetGridRotation() const = 0;

        virtual void SetMode(int mode) = 0;
        virtual int GetMode() const = 0;

        virtual void SetRotationMode(int mode) = 0;
        virtual int GetRotationMode() const = 0;

        virtual void SetIsVisible(bool b) = 0;
        virtual bool IsVisible() const = 0;

        virtual void SetIsScaleRandomized(bool b) = 0;
        virtual bool IsScaleRandomized() const = 0;

        virtual void SetIsRotationRandomized(bool b) = 0;
        virtual bool IsRotationRandomized() const = 0;

        virtual void SetIsSpacingRandomized(bool b) = 0;
        virtual bool IsSpacingRandomized() const = 0;

        virtual void SetAvoidOverlapping(bool b) = 0;
        virtual bool IsAvoidOverlapping() const = 0;

        virtual void SetScaleRange(double minVal, double maxVal) = 0;
        virtual double GetScaleRangeMin() const = 0;
        virtual double GetScaleRangeMax() const = 0;

        virtual void SetRotationRange(double minVal, double maxVal) = 0;
        virtual double GetRotationRangeMin() const = 0;
        virtual double GetRotationRangeMax() const = 0;

        virtual void SetDistanceRange(double minVal, double maxVal) = 0;
        virtual double GetDistanceRangeMin() const = 0;
        virtual double GetDistanceRangeMax() const = 0;

        virtual void SetObjects(const std::vector<std::string>& objs) = 0;
        virtual void GetObjects(std::vector<std::string>& objs) const = 0;

        virtual void SetServerSideData(const SPopulationData& data) = 0;
        virtual const SPopulationData& GetServerSideData() const = 0;
    };

    using IPopulationInfoPtr = TSharedLockableDataPtr<IPopulationInfo>;
    using IPopulationInfoWPtr = TSharedLockableDataWPtr<IPopulationInfo>;

    class IPopulationManager : public Tools::Factory<IPopulationManager>
    {
    public:
        //virtual void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) = 0;
        virtual void SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager) = 0;

        virtual size_t GetNumberOfPopulations() const = 0;
        virtual IPopulationInfoPtr FindPopulationInfoByDBId(const std::string& id) const = 0;
        virtual IPopulationInfoPtr FindPopulationInfoBySplineRefId(const RefID& id) const = 0;
        virtual IPopulationInfoPtr AddPopulationInfo() = 0;
        virtual void RemovePopulationInfo(const RefID& id) = 0;
        virtual IPopulationInfoPtr GetPopulationInfo(const RefID& id) const = 0;
        virtual void GetPopulationIds(std::set<AdvViz::SDK::RefID>& ids) const = 0;

        virtual void LoadDataFromServer(const std::string& decorationId) = 0;
        virtual void AsyncLoadDataFromServer(const std::string& decorationId,
            const std::function<void(IPopulationInfoPtr&)>& onPopulationLoaded,
            const std::function<void(expected<void, std::string> const&)>& onComplete) = 0;
        virtual void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc) = 0;

        virtual bool HasPopulationsToSave() const = 0;
    };

    typedef std::shared_ptr<IPopulationManager> IPopulationManagerPtr;

    class ADVVIZ_LINK PopulationInfo : public IPopulationInfo, public Tools::TypeId<PopulationInfo>
    {
    public:
        PopulationInfo();
        virtual ~PopulationInfo();

        // overridden from ISavableItem
        const RefID& GetId() const override;
        void SetId(const RefID& id) override;
        ESaveStatus GetSaveStatus() const override;
        void SetSaveStatus(ESaveStatus status) override;

        const RefID& GetSplineId() const override;
        void SetSplineId(const RefID& id) override;

        void SetPopulationType(int type) override;
        int GetPopulationType() const override;

        void SetDensity(double v) override;
        double GetDensity() const override;

        void SetDistance(double v) override;
        double GetDistance() const override;

        void SetScale(double v) override;
        double GetScale() const override;

        void SetRotation(double v) override;
        double GetRotation() const override;

        void SetGridRotation(double v) override;
        double GetGridRotation() const override;

        void SetMode(int mode) override;
        int GetMode() const override;

        void SetRotationMode(int mode) override;
        int GetRotationMode() const override;

        void SetIsVisible(bool b) override;
        bool IsVisible() const override;

        void SetIsScaleRandomized(bool b) override;
        bool IsScaleRandomized() const override;

        void SetIsRotationRandomized(bool b) override;
        bool IsRotationRandomized() const override;

        void SetIsSpacingRandomized(bool b) override;
        bool IsSpacingRandomized() const override;

        void SetAvoidOverlapping(bool b) override;
        bool IsAvoidOverlapping() const override;

        void SetScaleRange(double minVal, double maxVal) override;
        double GetScaleRangeMin() const override;
        double GetScaleRangeMax() const override;

        void SetRotationRange(double minVal, double maxVal) override;
        double GetRotationRangeMin() const override;
        double GetRotationRangeMax() const override;

        void SetDistanceRange(double minVal, double maxVal) override;
        double GetDistanceRangeMin() const override;
        double GetDistanceRangeMax() const override;

        void SetObjects(const std::vector<std::string>& objs) override;
        void GetObjects(std::vector<std::string>& objs) const override;

        void SetServerSideData(const SPopulationData& data) override;
        const SPopulationData& GetServerSideData() const override;

        using Tools::TypeId<PopulationInfo>::GetTypeId;
        std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
        bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IPopulationInfo::IsTypeOf(i); }

        class Impl;
        Impl& GetImpl();
        const Impl& GetImpl() const;

    private:
        const std::shared_ptr<Impl> impl_;
    };

    class ADVVIZ_LINK PopulationManager : public IPopulationManager, public Tools::TypeId<PopulationManager>
    {
    public:
        PopulationManager();

        //void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) override;
        void SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager) override;

        size_t GetNumberOfPopulations() const override;
        IPopulationInfoPtr FindPopulationInfoByDBId(const std::string& id) const override;
        IPopulationInfoPtr FindPopulationInfoBySplineRefId(const RefID& id) const override;
        IPopulationInfoPtr AddPopulationInfo() override;
        void RemovePopulationInfo(const RefID& id) override;
        IPopulationInfoPtr GetPopulationInfo(const RefID& id) const override;
        void GetPopulationIds(std::set<AdvViz::SDK::RefID>& ids) const override;

        void LoadDataFromServer(const std::string& decorationId) override;
        void AsyncLoadDataFromServer(const std::string& decorationId,
            const std::function<void(IPopulationInfoPtr&)>& onPopulationLoaded,
            const std::function<void(expected<void, std::string> const&)>& onComplete) override;
        void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc) override;

        bool HasPopulationsToSave() const override;

        using Tools::TypeId<PopulationManager>::GetTypeId;
        std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
        bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IPopulationManager::IsTypeOf(i); }

        class Impl;
        Impl& GetImpl();
        const Impl& GetImpl() const;
    private:
        const std::shared_ptr<Impl> impl_;
    };
}
