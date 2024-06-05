// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "CesiumAsync/AsyncSystem.h"
#include "CesiumAsync/IAssetAccessor.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include <cstddef>

class ITWINCESIUMRUNTIME_API ITwinUnrealAssetAccessor
    : public CesiumAsync::IAssetAccessor {
public:
    ITwinUnrealAssetAccessor();

  virtual CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
  get(const CesiumAsync::AsyncSystem& asyncSystem,
      const std::string& url,
      const std::vector<CesiumAsync::IAssetAccessor::THeader>& headers)
      override;

  virtual CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>>
  request(
      const CesiumAsync::AsyncSystem& asyncSystem,
      const std::string& verb,
      const std::string& url,
      const std::vector<CesiumAsync::IAssetAccessor::THeader>& headers,
      const gsl::span<const std::byte>& contentPayload) override;

  virtual void tick() noexcept override;

private:
  CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>> getFromFile(
      const CesiumAsync::AsyncSystem& asyncSystem,
      const std::string& url,
      const std::vector<CesiumAsync::IAssetAccessor::THeader>& headers);

  FString _userAgent;
  TMap<FString, FString> _cesiumRequestHeaders;
};
