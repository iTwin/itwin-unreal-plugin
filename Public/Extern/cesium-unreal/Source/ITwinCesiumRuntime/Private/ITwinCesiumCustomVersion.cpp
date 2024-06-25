// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid
    FITwinCesiumCustomVersion::GUID(0xEEBEC3F6, 0xADA54FC6, 0x8597852F, 0x3AF08280);

// Register the custom version with core
FCustomVersionRegistration GRegisterITwinCesiumCustomVersion(
    FITwinCesiumCustomVersion::GUID,
    FITwinCesiumCustomVersion::LatestVersion,
    TEXT("CesiumVer"));
