/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwin.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <ITwinFwd.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include <Templates/PimplPtr.h>

#include <ITwinDigitalTwin.generated.h>

class FITwinSynchro4DAnimator;
struct FIModelInfos;
class UITwinWebServices;

UCLASS()
class ITWINRUNTIME_API AITwinDigitalTwin : public AActor, public IITwinWebServicesObserver
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	TObjectPtr<AITwinServerConnection> ServerConnection;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString ITwinId;

	AITwinDigitalTwin();
	#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif
	virtual void Destroyed() override;

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateITwin();

	/// Determine what Elements are under the mouse cursor and log information about them. Only the first few
	/// encountered Elements are processed to avoid overflowing the logs.
	/// \param pMaxUniqueElementsHit Pointer to a variable storing the maximum number of Elements to identify
	///		(passing nullptr is allowed and means "1")
	void IdentifyElementsUnderCursor(uint32 const* pMaxUniqueElementsHit);

private:
	void UpdateWebServices();

	/// overridden from IITwinWebServicesObserver:
	virtual void OnAuthorizationDone(bool bSuccess, FString const& Error) override;
	virtual void OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos) override;
	virtual void OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos) override;
	virtual void OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos) override;
	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) override;
	virtual void OnExportStarted(bool bSuccess, FString const& ExportId) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;

	UPROPERTY()
	TObjectPtr<UITwinWebServices> WebServices;
};
