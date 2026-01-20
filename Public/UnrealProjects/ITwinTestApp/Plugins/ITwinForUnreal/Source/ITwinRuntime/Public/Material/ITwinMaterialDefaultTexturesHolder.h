/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialDefaultTexturesHolder.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/




#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include <ITwinMaterialDefaultTexturesHolder.generated.h>

namespace AdvViz::SDK
{
	enum class EChannelType : uint8_t;
}

/// Component of an AITwinIModel holding the default textures to nullify some glTF material effects.
UCLASS()
class ITWINRUNTIME_API UITwinMaterialDefaultTexturesHolder : public UActorComponent
{
	GENERATED_BODY()
public:
	UITwinMaterialDefaultTexturesHolder();

	//! Return a texture that nullifies the given channel (and thus can be used as default texture for it).
	UTexture2D* GetDefaultTextureForChannel(AdvViz::SDK::EChannelType Channel) const;


private:
	//! Default textures to nullify some material effects
	UPROPERTY()
	UTexture2D* NoColorTexture = nullptr;
	UPROPERTY()
	UTexture2D* NoNormalTexture = nullptr;
	UPROPERTY()
	UTexture2D* NoMetallicRoughnessTexture = nullptr;
};
