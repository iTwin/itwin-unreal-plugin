/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAppIdHelper.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <ITwinAppIdHelper.generated.h>

//! Utility to be used in the Editor mode to set the iTwin app ID manually in the UI.
//! Useful when you do not have c++ code or blueprint at hand to set the app ID.
//! Typical use:
//! - Create a new level
//! - Drag & drop an ITwinAppIdHelper in the level
//! - Set its AppId property.
//! This will set the app ID for this session, and if the level is saved and reloaded
//! (eg. in another session), it will also set the app ID.
UCLASS()
class ITWINRUNTIME_API AITwinAppIdHelper : public AActor
{
	GENERATED_BODY()
public:
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
private:
	//! Use this property only to set the app ID, not to get it,
	//! as the stored value may be wrong if the app ID has been changed "externally"
	//! by calling eg. AITwinServerConnection::SetITwinAppID().
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString AppId;
};
