/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSavedView.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinServiceActor.h>
#include <Templates/PimplPtr.h>

#include <ITwinSavedView.generated.h>

struct FSavedView;
struct FSavedViewInfo;

UCLASS()
class ITWINRUNTIME_API AITwinSavedView : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere,
		BlueprintReadOnly)
	FString SavedViewId;

	AITwinSavedView();
	virtual void Destroyed() override;

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateSavedView();

	//! Only saved views created with the plugin can be deleted,
	//! legacy saved views are not deletable.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void DeleteSavedView();

	//! UpdateSavedView() needs to be called at least once before calling RenameSavedView()
	//! to set the savedView transform.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void MoveToSavedView();

	//! UpdateSavedView() needs to be called at least once before calling RenameSavedView()
	//! to set the savedView transform.
	//! Note: Only saved views created with the plugin can be renamed, legacy saved views are not editable.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void RenameSavedView();

	//! UpdateSavedView() needs to be called at least once before renaming the saved view
	//! to set its transform.
	//! Note: Only saved views created with the plugin can be renamed, legacy saved views are not editable.
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString DisplayName;

	#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	bool IsMovingToSavedView() const;
	#endif

	//! Only saved views created with the plugin can be renamed, legacy saved views are not editable.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void RetakeSavedView();

	UFUNCTION(Category = "iTwin",
		meta = (WorldContext = "WorldContextObject"),
		BlueprintCallable)
	static void HideElements(const UObject* WorldContextObject, FSavedView const& SavedView);

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;

	UFUNCTION()
	void OnTimelineTick(const float& output);

	// TODO_GCO: might just work instead of calling TickComponent from the global iModel ticker,
	// IFF I end up understanding why the OnTimelineTick delegate is not called... (see comment in
	// AITwinSavedView::MoveToSavedView...)
	//virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver:
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& InSavedViewId, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;

	/// overridden from FITwinDefaultWebServicesObserver
	virtual const TCHAR* GetObserverName() const override;
};
