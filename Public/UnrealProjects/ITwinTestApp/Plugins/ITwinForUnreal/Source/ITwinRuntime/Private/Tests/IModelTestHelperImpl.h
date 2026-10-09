/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelTestHelperImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <Tests/IModelTestHelper.h>
#include <Tests/WebTestHelpers.h>

struct FITwinExportInfo;


/// Implementation details for tests based on iModels, Reality Data, and the decoration service.
class FIModelTestHelperImpl
{
public:
	using FInitOptions = FIModelTestOptions;

	FIModelTestHelperImpl() = default;
	virtual ~FIModelTestHelperImpl();

	//! Fill the export info with the iTwin ID, scene ID, and other information needed to initialize the
	//! decoration service and/or the iModel to test.
	virtual void FillTestInfo(FITwinExportInfo& OutExportInfo,
		FString& OutSceneId,
		std::filesystem::path& OutRelativeCacheFolder,
		FInitOptions const& InOptions) const = 0;

	bool Init(FInitOptions const& Options = {});

	//! Return the URL of the mock server used in tests, or an empty string if no mock server is used.
	FString GetMockServerUrl() const;

	inline AITwinDecorationHelper* GetDecorationHelper() const;
	inline AITwinIModel* GetIModel() const;

	class FIOAsyncCallback : public FITwinIOAsyncCallback
	{
	public:
		FIOAsyncCallback(FIModelTestHelperImpl& InOwner);
		virtual bool IsDone() const override;
	private:
		FIModelTestHelperImpl& Owner;
	};

	FITwinIOAsyncCallbackPtr GetAsyncCallback() const { return AsyncCallback; }

	virtual void BindEvents() = 0;
	virtual void UnBindEvents() = 0;

	void SetTestCallback(std::function<void()>&& InCallback) { TestCallback = std::move(InCallback); }
	void ExecuteTestCallback();

	virtual int32 ExpectedLoadEvents() const = 0;
	void OnLoadEventReceived();

	void SetupCesiumCameraManager();
	void DetectFilledSceneMapping();
	void OnIModelLoaded(bool bSuccess, FString StringId);

	virtual void DetectCustomEvents() {}

	void Cleanup();
	void CleanupIfDone();

	virtual TUniquePtr<FITwinTilesetAccess> MakeTilesetAccess() const;


protected:
#if WITH_EDITOR
	FIModelTestHelper::FIModelLoadingResult IModelLoadingResult;
#endif

	FInitOptions CurrentOptions;

	FDelegateHandle OnDecorationLoadedHandle;

	FITwinIOAsyncCallbackPtr AsyncCallback;

	std::function<void()> TestCallback;
	int32 LoadEventsReceived = 0;
	bool bShouldLoadScene = false;
	bool bHasSetupCesiumCameraManager = false;
	bool bHasFilledSceneMapping = false;

	bool bHasPushedAllowTickInEditor = false;
};


inline AITwinDecorationHelper* FIModelTestHelperImpl::GetDecorationHelper() const
{
#if WITH_EDITOR
	return IModelLoadingResult.DecoHelper;
#else
	return nullptr;
#endif
}

inline AITwinIModel* FIModelTestHelperImpl::GetIModel() const
{
#if WITH_EDITOR
	return IModelLoadingResult.IModel;
#else
	return nullptr;
#endif
}

#endif // WITH_TESTS
