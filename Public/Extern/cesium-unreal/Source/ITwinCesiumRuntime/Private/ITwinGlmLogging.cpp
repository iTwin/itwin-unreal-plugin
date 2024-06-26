// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinGlmLogging.h"
#include "ITwinCesiumRuntime.h"

#include <glm/glm.hpp>
#include <string>

void ITwinGlmLogging::logVector(const std::string& name, const glm::dvec3& vector) {
  UE_LOG(
      LogITwinCesium,
      Verbose,
      TEXT("%s: %16.6f %16.6f %16.6f"),
      *FString(name.c_str()),
      vector.x,
      vector.y,
      vector.z);
}

void ITwinGlmLogging::logMatrix(const std::string& name, const glm::dmat4& matrix) {
  UE_LOG(LogITwinCesium, Verbose, TEXT("%s:"), *FString(name.c_str()));
  UE_LOG(
      LogITwinCesium,
      Verbose,
      TEXT(" %16.6f %16.6f %16.6f %16.6f"),
      matrix[0][0],
      matrix[1][0],
      matrix[2][0],
      matrix[3][0]);
  UE_LOG(
      LogITwinCesium,
      Verbose,
      TEXT(" %16.6f %16.6f %16.6f %16.6f"),
      matrix[0][1],
      matrix[1][1],
      matrix[2][1],
      matrix[3][1]);
  UE_LOG(
      LogITwinCesium,
      Verbose,
      TEXT(" %16.6f %16.6f %16.6f %16.6f"),
      matrix[0][2],
      matrix[1][2],
      matrix[2][2],
      matrix[3][2]);
  UE_LOG(
      LogITwinCesium,
      Verbose,
      TEXT(" %16.6f %16.6f %16.6f %16.6f"),
      matrix[0][3],
      matrix[1][3],
      matrix[2][3],
      matrix[3][3]);
}
