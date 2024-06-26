// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "CesiumAsync/ITaskProcessor.h"
#include "HAL/Platform.h"

class ITWINCESIUMRUNTIME_API ITwinUnrealTaskProcessor
    : public CesiumAsync::ITaskProcessor {
public:
  virtual void startTask(std::function<void()> f) override;
};
